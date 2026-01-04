#include <vnm_plot/core/text_renderer.h>
#include <vnm_plot/core/constants.h>
#include <vnm_plot/core/font_renderer.h>

#include <glatter/glatter.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_set>

namespace vnm::plot::core {

namespace {

template <typename Labels, typename DrawFunc>
bool update_and_draw_faded_labels(
    const Labels& labels,
    Text_renderer::axis_fade_tracker_t& tracker,
    float fade_duration_ms,
    DrawFunc&& draw_fn)
{
    const auto now = std::chrono::steady_clock::now();
    float dt_ms = 0.0f;
    if (tracker.initialized) {
        dt_ms = std::chrono::duration_cast<std::chrono::duration<float, std::milli>>(now - tracker.last_update).count();
    }
    tracker.last_update = now;
    tracker.initialized = true;

    const float step = fade_duration_ms > 0.0f
        ? std::clamp(dt_ms / fade_duration_ms, 0.0f, 1.0f)
        : 1.0f;

    std::unordered_set<double> current_values;
    current_values.reserve(labels.size());

    for (const auto& label : labels) {
        const double v = label.value;
        current_values.insert(v);

        auto it = tracker.states.find(v);
        if (it == tracker.states.end()) {
            Text_renderer::label_fade_state_t state;
            state.alpha = 0.0f;
            state.direction = 1;
            state.text = label.text;
            tracker.states.emplace(v, std::move(state));
        }
        else {
            auto& state = it->second;
            state.text = label.text;
            if (state.direction < 0) {
                state.direction = 1;
            }
        }
    }

    for (auto& entry : tracker.states) {
        if (current_values.find(entry.first) == current_values.end()) {
            auto& state = entry.second;
            if (state.direction >= 0) {
                state.direction = -1;
            }
        }
    }

    bool any_active = false;
    constexpr float k_alpha_eps = 1e-6f;
    for (auto it = tracker.states.begin(); it != tracker.states.end(); ) {
        auto& state = it->second;

        if (state.direction != 0 && step > 0.0f) {
            state.alpha += step * static_cast<float>(state.direction);
        }
        state.alpha = std::clamp(state.alpha, 0.0f, 1.0f);

        if (state.direction > 0 && state.alpha >= 1.0f - k_alpha_eps) {
            state.alpha = 1.0f;
            state.direction = 0;
        }
        if (state.direction < 0 && state.alpha <= 0.0f + k_alpha_eps) {
            state.alpha = 0.0f;
        }

        if (state.direction < 0 && state.alpha <= 0.0f) {
            it = tracker.states.erase(it);
            continue;
        }

        if (state.alpha > 0.0f) {
            draw_fn(it->first, state);
        }
        if (state.direction != 0) {
            any_active = true;
        }

        ++it;
    }

    return any_active;
}

} // anonymous namespace

Text_renderer::Text_renderer(Font_renderer* fr)
    : m_fonts(fr)
{
}

bool Text_renderer::render(const frame_context_t& ctx, bool fade_v_labels, bool fade_h_labels)
{
    if (!m_fonts) {
        return false;
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    bool any_active = false;
    any_active |= render_axis_labels(ctx, fade_v_labels);
    any_active |= render_info_overlay(ctx, fade_h_labels);

    glDisable(GL_BLEND);
    return any_active;
}

bool Text_renderer::render_axis_labels(const frame_context_t& ctx, bool fade_labels)
{
    const auto& pl = ctx.layout;
    const bool dark_mode = ctx.config ? ctx.config->dark_mode : false;
    const glm::vec4 font_color = dark_mode ? glm::vec4(1.f, 1.f, 1.f, 1.f) : glm::vec4(0.f, 0.f, 0.f, 1.f);

    const float right_edge_x = static_cast<float>(pl.usable_width + pl.v_bar_width - constants::k_v_label_horizontal_padding_px);
    const float min_x = static_cast<float>(pl.usable_width + constants::k_text_margin_px);
    const float baseline_off = m_fonts->baseline_offset_px();
    const double v_span = double(ctx.v1) - double(ctx.v0);

    glEnable(GL_SCISSOR_TEST);
    const GLint scissor_y = static_cast<GLint>(lround(double(ctx.win_h) - double(pl.usable_height)));
    glScissor(
        static_cast<GLint>(lround(pl.usable_width)), scissor_y,
        static_cast<GLsizei>(lround(pl.v_bar_width)), static_cast<GLsizei>(lround(pl.usable_height))
    );

    const auto draw_label = [&](double value, const label_fade_state_t& state) {
        if (!(v_span > 0.0) || !(pl.usable_height > 0.0)) {
            return;
        }

        const double px_per_unit = pl.usable_height / v_span;
        const float label_y = static_cast<float>(pl.usable_height - (value - double(ctx.v0)) * px_per_unit);

        const float baseline_target =
            label_y - constants::k_scissor_pad_px
                    - constants::k_v_label_vertical_nudge_px * static_cast<float>(ctx.adjusted_font_px);
        const float pen_y = baseline_target - baseline_off;

        const float text_width = m_fonts->measure_text_px(state.text.c_str());
        float pen_x = right_edge_x - text_width;
        if (pen_x < min_x) {
            pen_x = min_x;
        }

        const float snapped_x = std::floor(pen_x + constants::k_pixel_snap);
        const float snapped_y = std::floor(pen_y + constants::k_pixel_snap);

        glm::vec4 color = font_color;
        color.a *= state.alpha;
        m_fonts->batch_text(snapped_x, snapped_y, state.text.c_str());
        m_fonts->draw_and_flush(ctx.pmv, color);
    };

    bool any_active = false;
    if (fade_labels) {
        any_active = update_and_draw_faded_labels(
            pl.v_labels,
            m_vertical_fade,
            k_label_fade_duration_ms,
            draw_label);
    }
    else {
        const auto now = std::chrono::steady_clock::now();
        m_vertical_fade.states.clear();
        for (const auto& label : pl.v_labels) {
            label_fade_state_t state;
            state.alpha = 1.0f;
            state.direction = 0;
            state.text = label.text;
            m_vertical_fade.states.emplace(label.value, state);
            draw_label(label.value, state);
        }
        m_vertical_fade.last_update = now;
        m_vertical_fade.initialized = true;
    }

    glDisable(GL_SCISSOR_TEST);
    return any_active;
}

bool Text_renderer::render_info_overlay(const frame_context_t& ctx, bool fade_labels)
{
    const auto& pl = ctx.layout;
    const bool dark_mode = ctx.config ? ctx.config->dark_mode : false;
    const glm::vec4 font_color = dark_mode ? glm::vec4(1.f, 1.f, 1.f, 1.f) : glm::vec4(0.f, 0.f, 0.f, 1.f);
    const double t_span = ctx.t1 - ctx.t0;

    const auto draw_label = [&](double t, const label_fade_state_t& state) {
        if (!(t_span > 0.0) || !(pl.usable_width > 0.0)) {
            return;
        }

        const double px_per_unit = pl.usable_width / t_span;
        const float x_anchor = static_cast<float>((t - ctx.t0) * px_per_unit);
        const float pen_x = x_anchor + constants::k_text_margin_px;
        const float pen_y = static_cast<float>(pl.usable_height + constants::k_h_label_vertical_nudge_px * ctx.adjusted_font_px);

        glm::vec4 color = font_color;
        color.a *= state.alpha;
        m_fonts->batch_text(pen_x, pen_y, state.text.c_str());
        m_fonts->draw_and_flush(ctx.pmv, color);
    };

    bool any_active = false;
    if (fade_labels) {
        any_active = update_and_draw_faded_labels(
            pl.h_labels,
            m_horizontal_fade,
            k_label_fade_duration_ms,
            draw_label);
    }
    else {
        const auto now = std::chrono::steady_clock::now();
        m_horizontal_fade.states.clear();
        for (const auto& label : pl.h_labels) {
            label_fade_state_t state;
            state.alpha = 1.0f;
            state.direction = 0;
            state.text = label.text;
            m_horizontal_fade.states.emplace(label.value, state);
            draw_label(label.value, state);
        }
        m_horizontal_fade.last_update = now;
        m_horizontal_fade.initialized = true;
    }

    if (ctx.show_info) {
        const float overlay_baseline = m_fonts->compute_numeric_bottom();
        char buf[64];

        const double rh = ctx.adjusted_reserved_height;
        float llt = static_cast<float>(ctx.win_h) - static_cast<float>(rh)
                  - static_cast<float>(ctx.adjusted_font_px * 4 * constants::k_line_spacing)
                  + overlay_baseline;

        // High
        std::snprintf(buf, sizeof(buf), "High: %.*f", constants::k_value_decimals, ctx.v1);
        m_fonts->batch_text(constants::k_overlay_left_px, llt, buf);

        // Low
        llt += static_cast<float>(ctx.adjusted_font_px * constants::k_line_spacing);
        std::snprintf(buf, sizeof(buf), "Low:  %.*f", constants::k_value_decimals, ctx.v0);
        m_fonts->batch_text(constants::k_overlay_left_px, llt, buf);

        // From timestamp
        llt += static_cast<float>(ctx.adjusted_font_px * constants::k_line_spacing);
        const char* prefix_from = "From: ";
        m_fonts->batch_text(constants::k_overlay_left_px, llt, prefix_from);
        const float offset_from = m_fonts->measure_text_px(prefix_from);

        const bool timestamp_style_changed = (pl.h_labels_subsecond != m_last_subsecond);
        const bool timestamp_values_changed =
            (std::abs(ctx.t0 - m_last_t0) > 1e-9) || (std::abs(ctx.t1 - m_last_t1) > 1e-9);

        if (timestamp_style_changed || timestamp_values_changed || m_cached_from_ts.empty() || m_cached_to_ts.empty()) {
            // Use format_timestamp callback from config if available
            if (ctx.config && ctx.config->format_timestamp) {
                m_cached_from_ts = ctx.config->format_timestamp(ctx.t0, t_span);
                m_cached_to_ts = ctx.config->format_timestamp(ctx.t1, t_span);
            }
            else {
                // Default simple formatting
                std::snprintf(buf, sizeof(buf), "%.3f", ctx.t0);
                m_cached_from_ts = buf;
                std::snprintf(buf, sizeof(buf), "%.3f", ctx.t1);
                m_cached_to_ts = buf;
            }
            m_last_t0 = ctx.t0;
            m_last_t1 = ctx.t1;
            m_last_subsecond = pl.h_labels_subsecond;
        }
        m_fonts->batch_text(constants::k_overlay_left_px + offset_from, llt, m_cached_from_ts.c_str());

        // To timestamp
        llt += static_cast<float>(ctx.adjusted_font_px * constants::k_line_spacing);
        const char* prefix_to = "To:   ";
        m_fonts->batch_text(constants::k_overlay_left_px, llt, prefix_to);
        const float offset_to = m_fonts->measure_text_px(prefix_to);
        m_fonts->batch_text(constants::k_overlay_left_px + offset_to, llt, m_cached_to_ts.c_str());
    }

    m_fonts->draw_and_flush(ctx.pmv, font_color);
    return any_active;
}

} // namespace vnm::plot::core
