#include <vnm_plot/core/chrome_renderer.h>
#include <vnm_plot/core/primitive_renderer.h>
#include <vnm_plot/core/color_palette.h>
#include <vnm_plot/core/constants.h>
#include <vnm_plot/core/plot_config.h>
#include <vnm_plot/core/algo.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace vnm::plot {
using namespace detail;

namespace {

bool is_integer_multiple(double parent, double child)
{
    if (!(parent > 0.0) || !(child > 0.0)) {
        return false;
    }

    const double ratio = parent / child;
    const double rounded = std::round(ratio);
    if (rounded < 1.0) {
        return false;
    }

    const double tol = std::min(1e-3, std::max(1e-8, std::abs(ratio) * 1e-12));
    return std::abs(ratio - rounded) <= tol;
}

grid_layer_params_t build_time_grid(
    double t_min,
    double t_max,
    double width_px,
    double font_px,
    const std::function<void(const std::string&)>& log_debug)
{
    grid_layer_params_t levels;
    const double range = t_max - t_min;
    if (!(range > 0.0) || !(width_px > 0.0)) {
        return levels;
    }

    const double cell_span_min = font_px * k_cell_span_min_factor;
    const double fade_den = std::max<double>(1e-6, font_px * (k_cell_span_max_factor - k_cell_span_min_factor));
    const auto compute_alpha = [&](float spacing_px) -> float {
        const double fade = (double(spacing_px) - cell_span_min) / fade_den;
        return static_cast<float>(std::clamp(fade, 0.0, 1.0) * k_grid_line_alpha_base);
    };

    const double px_per_unit = width_px / range;
    const auto steps = build_time_steps_covering(range);
    int idx = std::max(0, find_time_step_start_index(steps, range));
    while (idx + 1 < static_cast<int>(steps.size()) && steps[idx] * px_per_unit < cell_span_min) {
        ++idx;
    }

    double last_step = 0.0;
    bool logged_non_multiple = false;
    for (; idx >= 0 && levels.count < grid_layer_params_t::k_max_levels; --idx) {
        const double step = steps[idx];
        const float spacing_px = static_cast<float>(step * px_per_unit);
        if (spacing_px < cell_span_min) {
            break;
        }
        if (last_step > 0.0 && !is_integer_multiple(last_step, step)) {
            if (!logged_non_multiple && log_debug) {
                log_debug(
                    "vnm_plot: dropping non-multiple time grid step; adjust build_time_steps_covering() "
                    "subdivision levels to exact multiples.");
                logged_non_multiple = true;
            }
            continue;
        }
        const double shift_units = get_shift(step, t_min);
        levels.spacing_px[levels.count] = spacing_px;
        levels.start_px[levels.count] = static_cast<float>(shift_units * px_per_unit);
        const float a = compute_alpha(spacing_px);
        levels.alpha[levels.count] = a;
        levels.thickness_px[levels.count] = 0.6f + 0.6f * (a / k_grid_line_alpha_base);
        ++levels.count;
        last_step = step;
    }

    return levels;
}

grid_layer_params_t flip_grid_levels_y(const grid_layer_params_t& levels, float height_px)
{
    grid_layer_params_t flipped = levels;
    for (int i = 0; i < flipped.count; ++i) {
        flipped.start_px[i] = height_px - flipped.start_px[i];
    }
    return flipped;
}

glm::vec2 to_gl_origin(
    const frame_context_t& ctx,
    const glm::vec2& top_left,
    const glm::vec2& size)
{
    const float win_h = static_cast<float>(ctx.win_h);
    return {top_left.x, win_h - (top_left.y + size.y)};
}

} // anonymous namespace

grid_layer_params_t Chrome_renderer::calculate_grid_params(
    double min,
    double max,
    double pixel_span,
    double font_px)
{
    grid_layer_params_t levels;
    const double range = max - min;
    if (!(range > 0.0) || !(pixel_span > 0.0)) {
        return levels;
    }

    static const std::array<int, 2> om_div = {5, 2};
    double step = 1.0;
    int idx = 16;
    double probe = 0.0;
    for (; (probe = step * om_div[circular_index(om_div, idx)]) < range; ++idx) {
        step = probe;
    }
    for (; (probe = step / om_div[circular_index(om_div, idx - 1)]) > range; --idx) {
        step = probe;
    }

    const double cell_span_min = font_px * k_cell_span_min_factor;
    const double cell_span_max = font_px * k_cell_span_max_factor;
    const double fade_den = std::max<double>(1e-6, cell_span_max - cell_span_min);
    const double px_per_unit = pixel_span / range;

    const auto compute_alpha = [&](float spacing_px) -> float {
        const double fade = (double(spacing_px) - cell_span_min) / fade_den;
        return static_cast<float>(std::clamp(fade, 0.0, 1.0) * k_grid_line_alpha_base);
    };

    while (levels.count < grid_layer_params_t::k_max_levels && step * px_per_unit >= cell_span_min) {
        const float spacing_px = static_cast<float>(step * px_per_unit);
        const double shift_units = get_shift(step, min);
        levels.spacing_px[levels.count] = spacing_px;
        levels.start_px[levels.count] = static_cast<float>(pixel_span - shift_units * px_per_unit);
        const float a = compute_alpha(spacing_px);
        levels.alpha[levels.count] = a;
        levels.thickness_px[levels.count] = 0.6f + 0.6f * (a / k_grid_line_alpha_base);
        ++levels.count;

        step /= om_div[circular_index(om_div, --idx)];
    }

    return levels;
}

void Chrome_renderer::render_grid_and_backgrounds(
    const frame_context_t& ctx,
    Primitive_renderer& prims)
{
    vnm::plot::Profiler* profiler = ctx.config ? ctx.config->profiler : nullptr;
    VNM_PLOT_PROFILE_SCOPE(
        profiler,
        "renderer.frame.chrome.grid_and_backgrounds");

    // Skip GL calls if configured (for pure CPU profiling)
    const bool skip_gl = ctx.config && ctx.config->skip_gl_calls;

    const auto& pl = ctx.layout;
    const bool dark_mode = ctx.config ? ctx.config->dark_mode : false;
    const Color_palette palette = dark_mode ? Color_palette::dark() : Color_palette::light();

    const glm::vec4 h_label_color = palette.h_label_background;
    const glm::vec4 v_label_color = palette.v_label_background;
    const double grid_visibility = ctx.config ? ctx.config->grid_visibility : 1.0;
    const glm::vec4 grid_rgb = glm::vec4(
        palette.grid_line.r,
        palette.grid_line.g,
        palette.grid_line.b,
        palette.grid_line.a * static_cast<float>(grid_visibility));
    const glm::vec4 tick_rgb = palette.grid_line;  // Tick marks stay visible
    const glm::vec4 preview_background = palette.preview_background;
    const glm::vec4 separator_color = palette.separator;

    // Background panes and separators (batch_rect is CPU work)
    prims.batch_rect(preview_background,
        glm::vec4(0.f, float(ctx.win_h) - float(ctx.adjusted_preview_height), float(ctx.win_w), float(ctx.win_h)));

    prims.batch_rect(h_label_color,
        glm::vec4(0.f, float(pl.usable_height),
                  float(ctx.win_w), float(pl.usable_height + ctx.base_label_height_px)));
    prims.batch_rect(v_label_color,
        glm::vec4(float(pl.usable_width), 0.f,
                  float(pl.usable_width + pl.v_bar_width), float(pl.usable_height)));
    prims.batch_rect(separator_color,
        glm::vec4(0.f, float(ctx.win_h) - float(ctx.adjusted_reserved_height),
                  float(ctx.win_w), float(ctx.win_h - ctx.adjusted_reserved_height + 1.0)));
    prims.batch_rect(separator_color,
        glm::vec4(0.f, float(ctx.win_h) - float(ctx.adjusted_preview_height + 1.0),
                  float(ctx.win_w), float(ctx.win_h) - float(ctx.adjusted_preview_height)));

    if (!skip_gl) {
        prims.flush_rects(ctx.pmv);
    }
    else {
        prims.clear_rect_batch();
    }

    // Grid - CPU calculations
    const glm::vec2 main_top_left{0.0f, 0.0f};
    const glm::vec2 main_size{float(pl.usable_width), float(pl.usable_height)};
    const glm::vec2 main_origin = to_gl_origin(ctx, main_top_left, main_size);

    const grid_layer_params_t vertical_levels = calculate_grid_params(
        double(ctx.v0), double(ctx.v1), pl.usable_height, ctx.adjusted_font_px);
    const grid_layer_params_t horizontal_levels = build_time_grid(
        ctx.t0,
        ctx.t1,
        pl.usable_width,
        ctx.adjusted_font_px,
        ctx.config ? ctx.config->log_debug : std::function<void(const std::string&)>());

    const grid_layer_params_t vertical_levels_gl = flip_grid_levels_y(vertical_levels, main_size.y);

    if (!skip_gl && grid_visibility > 0.0) {
        prims.draw_grid_shader(main_origin, main_size, grid_rgb, vertical_levels_gl, horizontal_levels);
    }

    const auto match_level_properties = [](float pos, const grid_layer_params_t& levels) -> std::pair<float, float> {
        float alpha = k_grid_line_alpha_base;
        float thick = 0.8f;
        for (int i = 0; i < levels.count; ++i) {
            const float spacing = levels.spacing_px[i];
            if (spacing <= 0.0f) {
                continue;
            }
            double wrapped = std::fmod(double(pos) - double(levels.start_px[i]), double(spacing));
            if (wrapped < 0.0) {
                wrapped += double(spacing);
            }
            const double dist = std::min(wrapped, double(spacing) - wrapped);
            if (dist <= 0.35) {
                alpha = levels.alpha[i];
                thick = levels.thickness_px[i];
                break;
            }
        }
        return {alpha, thick};
    };

    const auto build_vertical_tick_levels = [&](const std::vector<v_label_t>& labels, const grid_layer_params_t& main_levels) {
        grid_layer_params_t ticks;
        for (const auto& label : labels) {
            if (ticks.count >= grid_layer_params_t::k_max_levels) {
                break;
            }
            const float pos = label.y;
            const auto props = match_level_properties(pos, main_levels);
            ticks.spacing_px[ticks.count] = 1e6f;
            ticks.start_px[ticks.count] = pos;
            ticks.alpha[ticks.count] = props.first;
            ticks.thickness_px[ticks.count] = props.second;
            ++ticks.count;
        }
        return ticks;
    };

    const auto build_horizontal_tick_levels = [&](const std::vector<h_label_t>& labels, const grid_layer_params_t& main_levels) {
        grid_layer_params_t ticks;
        for (const auto& label : labels) {
            if (ticks.count >= grid_layer_params_t::k_max_levels) {
                break;
            }
            const float pos = label.position.x;
            const auto props = match_level_properties(pos, main_levels);
            ticks.spacing_px[ticks.count] = 1e6f;
            ticks.start_px[ticks.count] = pos;
            ticks.alpha[ticks.count] = props.first;
            ticks.thickness_px[ticks.count] = props.second;
            ++ticks.count;
        }
        return ticks;
    };

    grid_layer_params_t empty_levels;
    const grid_layer_params_t vertical_tick_levels = build_vertical_tick_levels(pl.v_labels, vertical_levels);
    const grid_layer_params_t horizontal_tick_levels = build_horizontal_tick_levels(pl.h_labels, horizontal_levels);
    const grid_layer_params_t vertical_tick_levels_gl = flip_grid_levels_y(vertical_tick_levels, main_size.y);

    if (!skip_gl && pl.v_bar_width > 0.5 && vertical_tick_levels_gl.count > 0) {
        const glm::vec2 top_left{float(pl.usable_width), 0.0f};
        const glm::vec2 size{float(pl.v_bar_width), float(pl.usable_height)};
        const glm::vec2 origin = to_gl_origin(ctx, top_left, size);
        prims.draw_grid_shader(origin, size, tick_rgb, vertical_tick_levels_gl, empty_levels);
    }

    if (!skip_gl && ctx.base_label_height_px > 0.5 && horizontal_tick_levels.count > 0) {
        const glm::vec2 top_left{0.0f, float(pl.usable_height)};
        const glm::vec2 size{float(pl.usable_width), float(ctx.base_label_height_px)};
        const glm::vec2 origin = to_gl_origin(ctx, top_left, size);
        prims.draw_grid_shader(origin, size, tick_rgb, empty_levels, horizontal_tick_levels);
    }
}

void Chrome_renderer::render_preview_overlay(
    const frame_context_t& ctx,
    Primitive_renderer& prims)
{
    vnm::plot::Profiler* profiler = ctx.config ? ctx.config->profiler : nullptr;
    VNM_PLOT_PROFILE_SCOPE(
        profiler,
        "renderer.frame.chrome.preview_overlay");

    // Skip GL calls if configured (for pure CPU profiling)
    const bool skip_gl = ctx.config && ctx.config->skip_gl_calls;

    if (ctx.adjusted_preview_height <= 0.) {
        return;
    }

    const double t_avail_span = ctx.t_available_max - ctx.t_available_min;
    if (t_avail_span <= 0) {
        return;
    }

    const bool dark_mode = ctx.config ? ctx.config->dark_mode : false;
    const Color_palette palette = dark_mode ? Color_palette::dark() : Color_palette::light();
    const glm::vec4 cover_color = palette.preview_cover;
    const glm::vec4 cover_color2 = palette.preview_cover_secondary;
    const glm::vec4 separator_color = palette.separator;

    // CPU calculations
    const double x0 = ctx.win_w * (ctx.t0 - ctx.t_available_min) / t_avail_span;
    const double x1 = ctx.win_w * (1.0 - (ctx.t_available_max - ctx.t1) / t_avail_span);

    const double dd = x1 - x0;
    const double blh = ctx.base_label_height_px;
    const double win_w = ctx.win_w;
    const double ptop = ctx.layout.usable_height + blh;
    const double pbtm = ctx.win_h;

    const double pband_h = std::min(k_preview_band_max_px, ctx.adjusted_preview_height);

    // batch_rect is CPU work
    if (dd >= k_preview_min_window_px) {
        prims.batch_rect(cover_color, {0, float(ptop + pband_h), float(x0), float(pbtm - pband_h)});
        prims.batch_rect(cover_color, {float(x1), float(ptop + pband_h), float(win_w), float(pbtm - pband_h)});
    }
    else {
        const double xx = 0.5 * (k_preview_min_window_px - dd);
        prims.batch_rect(cover_color, {float(0 - xx), float(ptop + pband_h), float(x0 - xx), float(pbtm - pband_h)});
        prims.batch_rect(cover_color, {float(x1 + xx), float(ptop + pband_h), float(win_w + xx), float(pbtm - pband_h)});
        prims.batch_rect(cover_color2, {float(x0 - xx), float(ptop + pband_h), float(x0), float(pbtm - pband_h)});
        prims.batch_rect(cover_color2, {float(x1), float(ptop + pband_h), float(x1 + xx), float(pbtm - pband_h)});
    }

    prims.batch_rect(cover_color, {0, float(ptop), float(win_w), float(ptop + pband_h)});
    prims.batch_rect(cover_color, {0, float(pbtm - pband_h), float(win_w), float(pbtm)});

    prims.batch_rect(separator_color, {float(x0 - 1), float(ptop + pband_h - 1), float(x1 + 1), float(ptop + pband_h)});
    prims.batch_rect(separator_color, {float(x0 - 1), float(pbtm - pband_h), float(x1 + 1), float(pbtm - pband_h + 1)});
    prims.batch_rect(separator_color, {float(x0 - 1), float(ptop + pband_h), float(x0), float(pbtm - pband_h)});
    prims.batch_rect(separator_color, {float(x1), float(ptop + pband_h), float(x1 + 1), float(pbtm - pband_h)});

    // Note: flush_rects is called by the caller (benchmark) when skip_gl is false.
    // When skip_gl is true, the caller will not call flush_rects and we leave the batch
    // to be cleared later.
    if (skip_gl) {
        prims.clear_rect_batch();
    }
}

} // namespace vnm::plot
