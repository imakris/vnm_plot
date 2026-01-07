// vnm_plot Benchmark - Shared Frame Rendering Implementation

#include "benchmark_frame.h"

#include <glatter/glatter.h>

// Undef X11 Status macro that conflicts with snapshot_result_t::Status
#ifdef Status
#undef Status
#endif
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace vnm::benchmark {

std::string format_benchmark_timestamp(double ts, double /*range*/)
{
    // Format timestamp as seconds with 1 decimal.
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", ts);
    return buf;
}

void update_view_range_from_source(
    vnm::plot::Data_source* source,
    const std::string& data_type,
    double& t_min,
    double& t_max,
    double& t_available_min,
    float& v_min,
    float& v_max)
{
    if (!source) {
        return;
    }

    auto result = source->try_snapshot();
    if (result.status != vnm::plot::snapshot_result_t::Status::OK) {
        return;
    }

    const auto& snapshot = result.snapshot;
    if (snapshot.count == 0) {
        return;
    }

    // Get time range from first and last samples
    const auto* first_bytes = static_cast<const char*>(snapshot.data);
    const auto* last_bytes = first_bytes + (snapshot.count - 1) * snapshot.stride;

    double t_first = 0.0;
    double t_last = 0.0;

    if (data_type == "Trades") {
        t_first = reinterpret_cast<const Trade_sample*>(first_bytes)->timestamp;
        t_last = reinterpret_cast<const Trade_sample*>(last_bytes)->timestamp;
    }
    else {
        t_first = reinterpret_cast<const Bar_sample*>(first_bytes)->timestamp;
        t_last = reinterpret_cast<const Bar_sample*>(last_bytes)->timestamp;
    }

    // Track available time range for preview bar
    t_available_min = t_first;

    // Show last 10 seconds of data (sliding window)
    constexpr double window_size = 10.0;
    t_max = t_last;
    t_min = std::max(t_first, t_last - window_size);

    // Update value range
    if (source->has_value_range()) {
        auto [lo, hi] = source->value_range();
        // Add 10% padding
        float padding = (hi - lo) * 0.1f;
        if (padding < 0.01f) {
            padding = 1.0f;  // Minimum padding
        }
        v_min = lo - padding;
        v_max = hi + padding;
    }
}

void render_benchmark_frame(
    Benchmark_frame_params& params,
    Benchmark_frame_context& ctx)
{
    VNM_PLOT_PROFILE_SCOPE(&ctx.profiler, "renderer");
    VNM_PLOT_PROFILE_SCOPE(&ctx.profiler, "renderer.frame");

    // Get framebuffer size via callback (inside profiling scope)
    int fb_w = 0;
    int fb_h = 0;
    {
        VNM_PLOT_PROFILE_SCOPE(&ctx.profiler, "renderer.frame.fb_size");
        if (params.get_fb_size) {
            auto [w, h] = params.get_fb_size();
            fb_w = w;
            fb_h = h;
        }
    }

    // GL setup
    {
        VNM_PLOT_PROFILE_SCOPE(&ctx.profiler, "renderer.frame.gl_setup");
        if (!params.skip_gl_calls) {
            // Bind framebuffer if callback provided (headless FBO binding)
            if (params.bind_framebuffer) {
                params.bind_framebuffer();
            }

            glViewport(0, 0, fb_w, fb_h);
            glEnable(GL_MULTISAMPLE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            const auto palette = vnm::plot::Color_palette::for_theme(ctx.render_config.dark_mode);
            glClearColor(palette.background.r, palette.background.g,
                        palette.background.b, palette.background.a);

            GLbitfield clear_mask = GL_COLOR_BUFFER_BIT;
            if (params.clear_depth) {
                clear_mask |= GL_DEPTH_BUFFER_BIT;
            }
            glClear(clear_mask);
        }
    }

    // Update view range based on data
    {
        VNM_PLOT_PROFILE_SCOPE(&ctx.profiler, "renderer.frame.update_view_range");
        update_view_range_from_source(
            ctx.data_source,
            params.data_type,
            params.t_min,
            params.t_max,
            params.t_available_min,
            params.v_min,
            params.v_max);
    }

    // Calculate layout dimensions
    double adjusted_reserved_height;
    double usable_width;
    double usable_height;
    {
        VNM_PLOT_PROFILE_SCOPE(&ctx.profiler, "renderer.frame.dimension_calc");
        adjusted_reserved_height = params.base_label_height_px + params.adjusted_preview_height;
        usable_width = fb_w - params.vbar_width_pixels;
        usable_height = fb_h - adjusted_reserved_height;
    }

    // Use layout calculator for label positions
    vnm::plot::Layout_calculator::parameters_t layout_params;
    {
        VNM_PLOT_PROFILE_SCOPE(&ctx.profiler, "renderer.frame.layout_params_setup");
        layout_params.v_min = params.v_min;
        layout_params.v_max = params.v_max;
        layout_params.t_min = params.t_min;
        layout_params.t_max = params.t_max;
        layout_params.usable_width = usable_width;
        layout_params.usable_height = usable_height;
        layout_params.vbar_width = params.vbar_width_pixels;
        layout_params.label_visible_height = usable_height + params.adjusted_preview_height;
        layout_params.adjusted_font_size_in_pixels = params.adjusted_font_px;
#if defined(VNM_PLOT_ENABLE_TEXT)
        if (ctx.font_renderer) {
            layout_params.monospace_char_advance_px = ctx.font_renderer->monospace_advance_px();
            layout_params.monospace_advance_is_reliable = ctx.font_renderer->monospace_advance_is_reliable();
            layout_params.measure_text_cache_key = ctx.font_renderer->text_measure_cache_key();
            layout_params.measure_text_func = [font_renderer = ctx.font_renderer](const char* text) {
                return font_renderer->measure_text_px(text);
            };
        }
        else
#endif
        {
            layout_params.monospace_char_advance_px = 0.0f;
            layout_params.monospace_advance_is_reliable = false;
            layout_params.measure_text_cache_key = 0;
            layout_params.measure_text_func = [](const char* text) {
                return static_cast<float>(std::strlen(text));
            };
        }
        layout_params.h_label_vertical_nudge_factor = vnm::plot::detail::k_h_label_vertical_nudge_px;
        layout_params.format_timestamp_func = format_benchmark_timestamp;
        layout_params.get_required_fixed_digits_func = [](double) { return 2; };
        layout_params.profiler = &ctx.profiler;
    }

    vnm::plot::layout_cache_key_t cache_key;
    {
        VNM_PLOT_PROFILE_SCOPE(&ctx.profiler, "renderer.frame.cache_key_setup");
        cache_key.v0 = params.v_min;
        cache_key.v1 = params.v_max;
        cache_key.t0 = params.t_min;
        cache_key.t1 = params.t_max;
        cache_key.viewport_size = vnm::plot::Size2i{fb_w, fb_h};
        cache_key.adjusted_reserved_height = adjusted_reserved_height;
        cache_key.adjusted_preview_height = params.adjusted_preview_height;
        cache_key.adjusted_font_size_in_pixels = params.adjusted_font_px;
        cache_key.vbar_width_pixels = params.vbar_width_pixels;
#if defined(VNM_PLOT_ENABLE_TEXT)
        cache_key.font_metrics_key = ctx.font_renderer
            ? ctx.font_renderer->text_measure_cache_key() : 0;
#else
        cache_key.font_metrics_key = 0;
#endif
    }

    const vnm::plot::frame_layout_result_t* layout_ptr = nullptr;
    {
        VNM_PLOT_PROFILE_SCOPE(&ctx.profiler, "renderer.frame.layout_cache_lookup");
        layout_ptr = ctx.layout_cache.try_get(cache_key);
    }
    if (!layout_ptr) {
        VNM_PLOT_PROFILE_SCOPE(&ctx.profiler, "renderer.frame.layout_cache_miss");
        auto layout_result = ctx.layout_calc.calculate(layout_params);

        vnm::plot::frame_layout_result_t layout;
        {
            VNM_PLOT_PROFILE_SCOPE(&ctx.profiler, "renderer.frame.layout_cache_miss_store");
            layout.usable_width = usable_width;
            layout.usable_height = usable_height;
            layout.v_bar_width = params.vbar_width_pixels;
            layout.h_bar_height = params.base_label_height_px + 1.0;  // +1 for scissor padding
            layout.max_v_label_text_width = layout_result.max_v_label_text_width;
            layout.v_labels = std::move(layout_result.v_labels);
            layout.h_labels = std::move(layout_result.h_labels);
            layout.v_label_fixed_digits = layout_result.v_label_fixed_digits;
            layout.h_labels_subsecond = layout_result.h_labels_subsecond;
            layout_ptr = &ctx.layout_cache.store(cache_key, std::move(layout));
        }
    }

    // Build frame context
    vnm::plot::frame_context_t frame_ctx = [&]() {
        VNM_PLOT_PROFILE_SCOPE(&ctx.profiler, "renderer.frame.context_build");
        return vnm::plot::frame_context_t{
            *layout_ptr,
            params.v_min,
            params.v_max,
            params.v_min,  // preview_v0
            params.v_max,  // preview_v1
            params.t_min,
            params.t_max,
            params.t_available_min,  // t_available_min (full data range start)
            params.t_max,            // t_available_max (use current max for preview)
            fb_w,
            fb_h,
            glm::ortho(0.f, float(fb_w), float(fb_h), 0.f, -1.f, 1.f),
            params.adjusted_font_px,
            params.base_label_height_px,
            adjusted_reserved_height,
            params.adjusted_preview_height,
            false,  // show_info
            &ctx.render_config
        };
    }();

    // Render - vnm_plot internal scopes are captured by profiler automatically
    {
        VNM_PLOT_PROFILE_SCOPE(&ctx.profiler, "renderer.frame.render_passes");
        ctx.chrome_renderer.render_grid_and_backgrounds(frame_ctx, ctx.primitives);
        ctx.series_renderer.render(frame_ctx, ctx.series_map);
        ctx.chrome_renderer.render_preview_overlay(frame_ctx, ctx.primitives);
        if (!params.skip_gl_calls) {
            ctx.primitives.flush_rects(frame_ctx.pmv);
        }
    }

    // Render text labels
#if defined(VNM_PLOT_ENABLE_TEXT)
    if (ctx.text_renderer && ctx.render_config.show_text) {
        VNM_PLOT_PROFILE_SCOPE(&ctx.profiler, "renderer.frame.text_overlay");
        ctx.text_renderer->render(frame_ctx, false, false);
    }
#endif
}

}  // namespace vnm::benchmark
