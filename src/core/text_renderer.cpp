#include <vnm_plot/core/text_renderer.h>
#include <vnm_plot/core/constants.h>
#include <vnm_plot/core/plot_config.h>
#include <vnm_plot/core/font_renderer.h>
#include <vnm_plot/core/time_units.h>
#include "rhi_helpers.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_set>

namespace vnm::plot {
using detail::k_h_label_vertical_nudge_px;
using detail::k_line_spacing;
using detail::k_overlay_left_px;
using detail::k_pixel_snap;
using detail::k_scissor_pad_px;
using detail::k_text_margin_px;
using detail::k_v_label_horizontal_padding_px;
using detail::k_v_label_vertical_nudge_px;
using detail::k_value_decimals;

namespace {

using detail::to_int_rounded;
using detail::to_positive_int;

constexpr float k_text_shadow_alpha = 0.76f;
constexpr float k_text_shadow_min_radius_px = 1.05f;
constexpr float k_text_shadow_max_radius_px = 2.25f;
constexpr float k_text_shadow_radius_factor = 0.19f;

glm::vec4 text_color_for_theme(bool dark_mode)
{
    return dark_mode
        ? glm::vec4(1.f, 1.f, 1.f, 1.f)
        : glm::vec4(0.f, 0.f, 0.f, 1.f);
}

text_shadow_t text_shadow_for_background(const glm::vec4& background, double font_px)
{
    text_shadow_t shadow;
    shadow.color = background;
    shadow.color.a *= k_text_shadow_alpha;
    shadow.radius_px = std::clamp(
        static_cast<float>(font_px) * k_text_shadow_radius_factor,
        k_text_shadow_min_radius_px,
        k_text_shadow_max_radius_px);
    return shadow;
}

template <typename Labels, typename Tracker, typename KeyFunc, typename DrawFunc>
bool update_and_draw_faded_labels(
    const Labels& labels,
    Tracker& tracker,
    float fade_duration_ms,
    KeyFunc&& key_fn,
    DrawFunc&& draw_fn)
{
    using key_t = typename Tracker::key_type;

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

    std::unordered_set<key_t> current_values;
    current_values.reserve(labels.size());

    for (const auto& label : labels) {
        const key_t key = key_fn(label);
        current_values.insert(key);

        auto it = tracker.states.find(key);
        if (it == tracker.states.end()) {
            Text_renderer::label_fade_state_t state;
            state.alpha = 0.0f;
            state.direction = 1;
            state.text = label.text;
            tracker.states.emplace(key, std::move(state));
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

    bool any_active = false;
    any_active |= render_axis_labels(ctx, fade_v_labels);
    any_active |= render_info_overlay(ctx, fade_h_labels);
    return any_active;
}

bool Text_renderer::prepare(const frame_context_t& ctx, bool fade_v_labels, bool fade_h_labels)
{
    if (!m_fonts) {
        return false;
    }

    m_fonts->rhi_begin_frame();
    bool any_active = false;
    any_active |= render_axis_labels(ctx, fade_v_labels);
    any_active |= render_info_overlay(ctx, fade_h_labels);
    m_fonts->rhi_finalize_frame(ctx);
    return any_active;
}

void Text_renderer::record(const frame_context_t& ctx)
{
    if (m_fonts) {
        m_fonts->rhi_record_frame(ctx);
    }
}

bool Text_renderer::render_axis_labels(const frame_context_t& ctx, bool fade_labels)
{
    const auto& pl = ctx.layout;
    const bool dark_mode = ctx.dark_mode;
    const glm::vec4 font_color = text_color_for_theme(dark_mode);

    const float right_edge_x = static_cast<float>(pl.usable_width + pl.v_bar_width - k_v_label_horizontal_padding_px);
    const float min_x = static_cast<float>(pl.usable_width + k_text_margin_px);
    const float baseline_off = m_fonts->baseline_offset_px();
    const double v_span = double(ctx.v1) - double(ctx.v0);

    text_scissor_t rhi_scissor;
    if (ctx.rhi) {
        int scissor_x = 0;
        int scissor_y = 0;
        int scissor_w = 0;
        int scissor_h = 0;
        if (!to_int_rounded(pl.usable_width, scissor_x) ||
            !to_int_rounded(double(ctx.win_h) - double(pl.usable_height), scissor_y) ||
            !to_positive_int(pl.v_bar_width, scissor_w) ||
            !to_positive_int(pl.usable_height, scissor_h)) {
            return true;
        }
        rhi_scissor.enabled = true;
        rhi_scissor.x      = scissor_x;
        rhi_scissor.y      = scissor_y;
        rhi_scissor.width  = scissor_w;
        rhi_scissor.height = scissor_h;
    }

    const auto draw_label = [&](double value, const label_fade_state_t& state) {
        if (!(v_span > 0.0) || !(pl.usable_height > 0.0)) {
            return;
        }

        const double px_per_unit = pl.usable_height / v_span;
        const float label_y = static_cast<float>(pl.usable_height - (value - double(ctx.v0)) * px_per_unit);

        const float baseline_target =
            label_y - k_scissor_pad_px
                    - k_v_label_vertical_nudge_px * static_cast<float>(ctx.adjusted_font_px);
        const float pen_y = baseline_target - baseline_off;

        const float text_width = m_fonts->measure_text_px(state.text.c_str());
        float pen_x = right_edge_x - text_width;
        if (pen_x < min_x) {
            pen_x = min_x;
        }

        const float snapped_x = std::floor(pen_x + k_pixel_snap);
        const float snapped_y = std::floor(pen_y + k_pixel_snap);

        m_fonts->batch_text(snapped_x, snapped_y, state.text.c_str());
        if (fade_labels) {
            if (ctx.rhi) {
                glm::vec4 color = font_color;
                color.a *= state.alpha;
                m_fonts->rhi_queue_draw(ctx, ctx.pmv, color, rhi_scissor);
            }
        }
    };

    bool any_active = false;
    if (fade_labels) {
        any_active = update_and_draw_faded_labels(
            pl.v_labels,
            m_vertical_fade,
            k_label_fade_duration_ms,
            [](const v_label_t& label) { return label.value; },
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

    if (!fade_labels) {
        if (ctx.rhi) {
            m_fonts->rhi_queue_draw(ctx, ctx.pmv, font_color, rhi_scissor);
        }
    }
    return any_active;
}

bool Text_renderer::render_info_overlay(const frame_context_t& ctx, bool fade_labels)
{
    const auto& pl = ctx.layout;
    const bool dark_mode = ctx.dark_mode;
    const glm::vec4 font_color = text_color_for_theme(dark_mode);
    const text_shadow_t overlay_shadow =
        text_shadow_for_background(ctx.plot_body_background, ctx.adjusted_font_px);
    const auto t_span = positive_span_ns_as_long_double(ctx.t0, ctx.t1);

    const auto draw_label = [&](std::int64_t t_ns, const label_fade_state_t& state) {
        if (!t_span || !(pl.usable_width > 0.0)) {
            return;
        }

        const long double px_per_unit =
            static_cast<long double>(pl.usable_width) / *t_span;
        const long double t_delta = span_ns_as_long_double(ctx.t0, t_ns);
        const float x_anchor = static_cast<float>(
            t_delta * px_per_unit);
        const float pen_x = x_anchor + k_text_margin_px;
        const float pen_y = static_cast<float>(pl.usable_height + k_h_label_vertical_nudge_px * ctx.adjusted_font_px);

        m_fonts->batch_text(pen_x, pen_y, state.text.c_str());
        if (fade_labels) {
            if (ctx.rhi) {
                glm::vec4 color = font_color;
                color.a *= state.alpha;
                m_fonts->rhi_queue_draw(ctx, ctx.pmv, color);
            }
        }
    };

    bool any_active = false;
    if (fade_labels) {
        any_active = update_and_draw_faded_labels(
            pl.h_labels,
            m_horizontal_fade,
            k_label_fade_duration_ms,
            [](const h_label_t& label) { return label.value; },
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
            const std::int64_t key = label.value;
            m_horizontal_fade.states.emplace(key, state);
            draw_label(key, state);
        }
        m_horizontal_fade.last_update = now;
        m_horizontal_fade.initialized = true;
    }

    if (!fade_labels) {
        if (ctx.rhi) {
            m_fonts->rhi_queue_draw(ctx, ctx.pmv, font_color);
        }
    }

    const bool show_value_range =
        (ctx.visible_info_flags & k_visible_info_value_range) != 0;
    const bool show_time_range =
        (ctx.visible_info_flags & k_visible_info_time_range) != 0;
    const int overlay_line_count = (show_value_range ? 2 : 0) + (show_time_range ? 2 : 0);

    if (overlay_line_count > 0) {
        const float overlay_baseline = m_fonts->compute_numeric_bottom();
        char buf[64];

        const double rh = ctx.adjusted_reserved_height;
        float llt = static_cast<float>(ctx.win_h) - static_cast<float>(rh)
                  - static_cast<float>(ctx.adjusted_font_px * overlay_line_count * k_line_spacing)
                  + overlay_baseline;

        const auto format_info_value = [&](double value) {
            if (ctx.config && ctx.config->format_value) {
                value_format_context_t context;
                context.role = Value_format_role::INFO_OVERLAY;
                context.suggested_fixed_digits = k_value_decimals;
                const std::string text = ctx.config->format_value(value, context);
                if (!text.empty()) {
                    return text;
                }
            }
            std::snprintf(buf, sizeof(buf), "%.*f", k_value_decimals, value);
            return std::string(buf);
        };

        if (show_value_range) {
            const std::string high_text = "High: " + format_info_value(ctx.v1);
            m_fonts->batch_text(k_overlay_left_px, llt, high_text.c_str());

            llt += static_cast<float>(ctx.adjusted_font_px * k_line_spacing);
            const std::string low_text = "Low:  " + format_info_value(ctx.v0);
            m_fonts->batch_text(k_overlay_left_px, llt, low_text.c_str());

            llt += static_cast<float>(ctx.adjusted_font_px * k_line_spacing);
        }

        if (show_time_range) {
            const char* prefix_from = "From: ";
            m_fonts->batch_text(k_overlay_left_px, llt, prefix_from);
            const float offset_from = m_fonts->measure_text_px(prefix_from);

            const bool timestamp_style_changed = (pl.h_labels_subsecond != m_last_subsecond);
            const bool timestamp_values_changed =
                (ctx.t0 != m_last_t0) || (ctx.t1 != m_last_t1);
            const std::uint64_t timestamp_revision = ctx.config
                ? ctx.config->format_timestamp_revision
                : 0;
            const bool timestamp_formatter_changed =
                timestamp_revision != m_last_timestamp_revision;

            if (timestamp_style_changed || timestamp_values_changed
                || timestamp_formatter_changed || m_cached_from_ts.empty()
                || m_cached_to_ts.empty())
            {
                const auto format_ts = (ctx.config && ctx.config->format_timestamp)
                    ? ctx.config->format_timestamp
                    : default_format_timestamp;
                m_cached_from_ts = format_ts(ctx.t0, 0);
                m_cached_to_ts = format_ts(ctx.t1, 0);
                m_last_t0 = ctx.t0;
                m_last_t1 = ctx.t1;
                m_last_timestamp_revision = timestamp_revision;
                m_last_subsecond = pl.h_labels_subsecond;
            }
            m_fonts->batch_text(k_overlay_left_px + offset_from, llt, m_cached_from_ts.c_str());

            llt += static_cast<float>(ctx.adjusted_font_px * k_line_spacing);
            const char* prefix_to = "To:   ";
            m_fonts->batch_text(k_overlay_left_px, llt, prefix_to);
            const float offset_to = m_fonts->measure_text_px(prefix_to);
            m_fonts->batch_text(k_overlay_left_px + offset_to, llt, m_cached_to_ts.c_str());
        }
    }

    if (overlay_line_count > 0) {
        if (ctx.rhi) {
            m_fonts->rhi_queue_draw(ctx, ctx.pmv, font_color, {}, overlay_shadow);
        }
    }
    return any_active;
}

} // namespace vnm::plot
