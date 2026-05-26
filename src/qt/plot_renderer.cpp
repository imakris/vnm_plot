#include "plot_renderer.h"
#include <vnm_plot/qt/plot_widget.h>
#include <vnm_plot/core/asset_loader.h>
#include <vnm_plot/core/chrome_renderer.h>
#include <vnm_plot/core/color_palette.h>
#include <vnm_plot/core/constants.h>
#include <vnm_plot/core/font_renderer.h>
#include <vnm_plot/core/layout_calculator.h>
#include <vnm_plot/core/primitive_renderer.h>
#include <vnm_plot/core/series_renderer.h>
#include <vnm_plot/core/text_renderer.h>

#include <QColor>
#include <QMatrix4x4>
#include <QQuickWindow>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <rhi/qrhi.h>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <limits>
#include <map>
#include <memory>
#include <shared_mutex>
#include <tuple>
#include <utility>

namespace vnm::plot {

namespace {

glm::mat4 to_glm_mat4(const QMatrix4x4& matrix)
{
    return glm::make_mat4(matrix.constData());
}

bool include_sample_range(
    const Data_access_policy& access,
    const void* sample,
    float& out_min,
    float& out_max)
{
    if (!sample) {
        return false;
    }

    float low = 0.0f;
    float high = 0.0f;
    if (access.get_range) {
        std::tie(low, high) = access.get_range(sample);
    }
    else
    if (access.get_value) {
        low = access.get_value(sample);
        high = low;
    }
    else {
        return false;
    }

    if (!std::isfinite(low) || !std::isfinite(high)) {
        return false;
    }

    out_min = std::min(out_min, std::min(low, high));
    out_max = std::max(out_max, std::max(low, high));
    return true;
}

bool scan_series_range(
    Data_source& source,
    const Data_access_policy& access,
    std::size_t level,
    bool visible_only,
    std::int64_t t_min,
    std::int64_t t_max,
    float& out_min,
    float& out_max)
{
    data_snapshot_t snapshot = source.snapshot(level);
    if (!snapshot.is_valid()) {
        return false;
    }

    bool have_any = false;
    for (std::size_t i = 0; i < snapshot.count; ++i) {
        const void* sample = snapshot.at(i);
        if (visible_only) {
            if (!sample || !access.get_timestamp) {
                continue;
            }
            const std::int64_t t = access.get_timestamp(sample);
            if (t < t_min || t > t_max) {
                continue;
            }
        }
        have_any = include_sample_range(access, sample, out_min, out_max) || have_any;
    }
    return have_any;
}

void apply_auto_v_range_padding(
    const Plot_config& config,
    bool data_range_nonnegative,
    float& v_min,
    float& v_max)
{
    if (v_max == v_min) {
        const float pad = std::max(std::abs(v_min) * 0.01f, 0.5f);
        v_min -= pad;
        v_max += pad;
    }

    if (config.auto_v_range_extra_scale > 0.0) {
        const double span = double(v_max) - double(v_min);
        if (span > 0.0) {
            const double center = 0.5 * (double(v_min) + double(v_max));
            const double padded_span = span * (1.0 + config.auto_v_range_extra_scale);
            v_min = static_cast<float>(center - padded_span * 0.5);
            v_max = static_cast<float>(center + padded_span * 0.5);
        }
    }

    if (config.floor_nonnegative_auto_v_range_at_zero && data_range_nonnegative && v_min < 0.0f) {
        v_min = 0.0f;
    }
}

std::pair<float, float> resolve_v_range(
    const std::map<int, std::shared_ptr<const series_data_t>>& series,
    const data_config_t& data_cfg,
    const Plot_config& config,
    bool v_auto)
{
    if (!v_auto) {
        return {data_cfg.v_manual_min, data_cfg.v_manual_max};
    }

    float v_min = std::numeric_limits<float>::max();
    float v_max = std::numeric_limits<float>::lowest();
    bool have_any = false;
    const bool visible_only = config.auto_v_range_mode == Auto_v_range_mode::VISIBLE;

    for (const auto& [_, item] : series) {
        if (!item || !item->enabled) {
            continue;
        }

        Data_source* source = item->main_source();
        if (!source) {
            continue;
        }

        float series_min = 0.0f;
        float series_max = 0.0f;
        bool got_range = false;
        if (!visible_only &&
            source->has_value_range() &&
            !source->value_range_needs_rescan())
        {
            auto [ds_min, ds_max] = source->value_range();
            if (std::isfinite(ds_min) && std::isfinite(ds_max) && ds_min <= ds_max) {
                series_min = ds_min;
                series_max = ds_max;
                got_range = true;
            }
        }

        if (!got_range) {
            const std::size_t levels = source->lod_levels();
            if (levels == 0) {
                continue;
            }
            const std::size_t level =
                config.auto_v_range_mode == Auto_v_range_mode::GLOBAL_LOD
                    ? levels - 1
                    : 0;
            got_range = scan_series_range(
                *source,
                item->main_access(),
                level,
                visible_only,
                data_cfg.t_min,
                data_cfg.t_max,
                series_min,
                series_max);
        }

        if (!got_range) {
            continue;
        }

        v_min = std::min(v_min, series_min);
        v_max = std::max(v_max, series_max);
        have_any = true;
    }

    if (!have_any || !std::isfinite(v_min) || !std::isfinite(v_max) || v_max < v_min) {
        return {data_cfg.v_min, data_cfg.v_max};
    }

    const bool data_range_nonnegative = v_min >= 0.0f;
    apply_auto_v_range_padding(config, data_range_nonnegative, v_min, v_max);
    return {v_min, v_max};
}

std::pair<float, float> resolve_preview_v_range(
    const std::map<int, std::shared_ptr<const series_data_t>>& series,
    const data_config_t& data_cfg,
    const Plot_config& config)
{
    float v_min = std::numeric_limits<float>::max();
    float v_max = std::numeric_limits<float>::lowest();
    bool have_any = false;

    for (const auto& [_, item] : series) {
        if (!item || !item->enabled) {
            continue;
        }

        Data_source* source = item->preview_source();
        if (!source) {
            continue;
        }
        const Data_access_policy& access = item->preview_access();
        if (!access.get_value && !access.get_range) {
            continue;
        }

        float series_min = 0.0f;
        float series_max = 0.0f;
        bool got_range = false;
        if (source->has_value_range() && !source->value_range_needs_rescan()) {
            auto [ds_min, ds_max] = source->value_range();
            if (std::isfinite(ds_min) && std::isfinite(ds_max) && ds_min <= ds_max) {
                series_min = ds_min;
                series_max = ds_max;
                got_range = true;
            }
        }

        if (!got_range) {
            const std::size_t levels = source->lod_levels();
            if (levels == 0) {
                continue;
            }
            const std::size_t level =
                config.auto_v_range_mode == Auto_v_range_mode::GLOBAL_LOD
                    ? levels - 1
                    : 0;
            got_range = scan_series_range(
                *source,
                access,
                level,
                false,
                data_cfg.t_available_min,
                data_cfg.t_available_max,
                series_min,
                series_max);
        }

        if (!got_range) {
            continue;
        }

        v_min = std::min(v_min, series_min);
        v_max = std::max(v_max, series_max);
        have_any = true;
    }

    if (!have_any || !std::isfinite(v_min) || !std::isfinite(v_max) || v_max < v_min) {
        return {data_cfg.v_min, data_cfg.v_max};
    }

    const bool data_range_nonnegative = v_min >= 0.0f;
    apply_auto_v_range_padding(config, data_range_nonnegative, v_min, v_max);
    return {v_min, v_max};
}

Layout_calculator::parameters_t build_layout_params(
    float v_min,
    float v_max,
    std::int64_t t_min_ns,
    std::int64_t t_max_ns,
    int win_w,
    double usable_height,
    double vbar_width,
    double preview_height,
    double font_px,
    const Plot_config& config,
    const Font_renderer* fonts)
{
    Layout_calculator::parameters_t params;
    params.v_min = v_min;
    params.v_max = v_max;
    params.t_min = t_min_ns;
    params.t_max = t_max_ns;
    params.usable_width = std::max(0.0, double(win_w) - vbar_width);
    params.usable_height = usable_height;
    params.vbar_width = vbar_width;
    params.label_visible_height = usable_height + preview_height;
    params.adjusted_font_size_in_pixels = font_px;
    params.h_label_vertical_nudge_factor = detail::k_h_label_vertical_nudge_px;

    if (fonts) {
        params.monospace_char_advance_px = fonts->monospace_advance_px();
        params.monospace_advance_is_reliable = fonts->monospace_advance_is_reliable();
        params.measure_text_cache_key = fonts->text_measure_cache_key();
        params.measure_text_func = [fonts](const char* text) {
            return fonts->measure_text_px(text);
        };
    }

    params.get_required_fixed_digits_func = [](double) { return 2; };
    const Plot_config* config_ptr = &config;
    params.format_timestamp_func = [config_ptr](std::int64_t ts_ns, std::int64_t step_ns) -> std::string {
        if (config_ptr->format_timestamp) {
            return config_ptr->format_timestamp(ts_ns, step_ns);
        }
        return default_format_timestamp(ts_ns, step_ns);
    };
    params.format_timestamp_revision = config.format_timestamp_revision;
    params.format_value_func = [config_ptr](double value, const value_format_context_t& context) -> std::string {
        if (config_ptr->format_value) {
            return config_ptr->format_value(value, context);
        }
        return {};
    };
    params.format_value_revision = config.format_value_revision;
    params.profiler = config.profiler.get();
    return params;
}

} // anonymous namespace

struct Plot_renderer::impl_t
{
    // Snapshot of the widget state the render path reads. Held by value so
    // render() runs on the renderer thread without re-acquiring Plot_widget
    // locks.
    struct render_snapshot_t
    {
        Plot_config    config;
        data_config_t  data_cfg;
        std::map<int, std::shared_ptr<const series_data_t>> series;
        bool           v_auto = true;
        int            visible_info_flags = k_visible_info_none;
        double         adjusted_font_px = 10.0;
        double         base_label_height_px = 14.0;
        double         adjusted_preview_height = 0.0;
        double         vbar_width_pixels = 0.0;
        std::uint64_t  config_revision = 0;
    };

    const Plot_widget* owner = nullptr;
    render_snapshot_t  snapshot;

    Asset_loader        asset_loader;
    Series_renderer     series;
    Primitive_renderer  primitives;
    Chrome_renderer     chrome;
    Layout_calculator   layout_calc;
    Layout_cache        layout_cache;
    double              last_vbar_width_pixels = detail::k_vbar_min_width_px_d;
#if defined(VNM_PLOT_ENABLE_TEXT)
    std::unique_ptr<Font_renderer> fonts;
    std::unique_ptr<Text_renderer> text;
#endif
    std::chrono::steady_clock::time_point last_render_callback;
    bool                series_initialized   = false;
};

Plot_renderer::Plot_renderer(const Plot_widget* owner)
    : m_impl(std::make_unique<impl_t>())
{
    m_impl->owner = owner;
    init_embedded_assets(m_impl->asset_loader);
}

Plot_renderer::~Plot_renderer() = default;

void Plot_renderer::initialize(QRhiCommandBuffer* /*cb*/)
{
    if (!m_impl->series_initialized) {
        m_impl->series.initialize(m_impl->asset_loader);
        m_impl->series_initialized = true;
    }
#if defined(VNM_PLOT_ENABLE_TEXT)
    if (!m_impl->fonts) {
        m_impl->fonts = std::make_unique<Font_renderer>();
        m_impl->text = std::make_unique<Text_renderer>(m_impl->fonts.get());
    }
#endif
}

void Plot_renderer::synchronize(QQuickRhiItem* item)
{
    auto* widget = static_cast<Plot_widget*>(item);
    if (!widget) {
        return;
    }

    {
        std::shared_lock lock(widget->m_config_mutex);
        m_impl->snapshot.config = widget->m_config;
    }
    {
        std::shared_lock lock(widget->m_data_cfg_mutex);
        m_impl->snapshot.data_cfg = widget->m_data_cfg;
    }
    {
        std::shared_lock lock(widget->m_series_mutex);
        m_impl->snapshot.series = widget->m_series;
    }
    m_impl->snapshot.v_auto = widget->m_v_auto.load(std::memory_order_acquire);
    m_impl->snapshot.visible_info_flags =
        widget->m_visible_info_flags.load(std::memory_order_acquire);
    m_impl->snapshot.adjusted_font_px = widget->m_adjusted_font_size;
    m_impl->snapshot.base_label_height_px = widget->m_base_label_height;
    m_impl->snapshot.adjusted_preview_height = widget->m_adjusted_preview_height;
    m_impl->snapshot.vbar_width_pixels = widget->vbar_width_pixels();
    m_impl->snapshot.config_revision = widget->m_config_revision.load(std::memory_order_acquire);
}

void Plot_renderer::render(QRhiCommandBuffer* cb)
{
    QRhiRenderTarget* const rt = renderTarget();
    if (!cb || !rt) {
        return;
    }
    QRhi* const rhi_ptr = rhi();

    const auto& snapshot = m_impl->snapshot;
    const Plot_config& config = snapshot.config;
    vnm::plot::Profiler* profiler = config.profiler.get();
    const auto callback_now = std::chrono::steady_clock::now();
    if (profiler && m_impl->last_render_callback.time_since_epoch().count() != 0) {
        const double elapsed_ms =
            std::chrono::duration<double, std::milli>(
                callback_now - m_impl->last_render_callback).count();
        profiler->record_observation("qrhi.renderer.callback_interval", elapsed_ms);
    }
    m_impl->last_render_callback = callback_now;

    VNM_PLOT_PROFILE_SCOPE(profiler, "renderer");
    VNM_PLOT_PROFILE_SCOPE(profiler, "renderer.frame");

    const Color_palette palette =
        config.dark_mode ? Color_palette::dark() : Color_palette::light();

    QColor clear_color;
    if (config.clear_to_transparent) {
        clear_color = QColor(0, 0, 0, 0);
    }
    else {
        clear_color = QColor::fromRgbF(
            palette.background.r,
            palette.background.g,
            palette.background.b,
            palette.background.a);
    }

    QSize pixel_size;
    {
        VNM_PLOT_PROFILE_SCOPE(profiler, "renderer.frame.fb_size");
        pixel_size = rt->pixelSize();
    }
    const int win_w = pixel_size.width();
    const int win_h = pixel_size.height();

    m_impl->primitives.set_profiler(profiler);
    const auto log_error = config.log_error;
    const auto log_debug = config.log_debug;
    m_impl->asset_loader.set_log_callback(log_error);
    m_impl->primitives.set_log_callback(log_error);

    const double reserved_h = snapshot.base_label_height_px + snapshot.adjusted_preview_height;
    const auto [v_min, v_max] = resolve_v_range(
        snapshot.series,
        snapshot.data_cfg,
        config,
        snapshot.v_auto);
    const bool preview_enabled =
        snapshot.adjusted_preview_height > 0.0 && config.preview_visibility > 0.0;
    const auto [preview_v_min, preview_v_max] = preview_enabled
        ? resolve_preview_v_range(snapshot.series, snapshot.data_cfg, config)
        : std::make_pair(v_min, v_max);

#if defined(VNM_PLOT_ENABLE_TEXT)
    if (m_impl->fonts) {
        m_impl->fonts->set_log_callbacks(log_error, log_debug);
    }
    if (m_impl->fonts && config.show_text) {
        const int font_px_int = static_cast<int>(std::lround(snapshot.adjusted_font_px));
        if (font_px_int > 0) {
            m_impl->fonts->initialize_metrics(m_impl->asset_loader, font_px_int);
        }
    }
    const Font_renderer* layout_fonts =
        (config.show_text && m_impl->fonts && m_impl->fonts->text_measure_cache_key() != 0)
            ? m_impl->fonts.get()
            : nullptr;
#else
    const Font_renderer* layout_fonts = nullptr;
#endif

    const double usable_height = std::max(0.0, double(win_h) - reserved_h);
    const double max_vbar_width = std::max(
        detail::k_vbar_min_width_px_d,
        std::max(0.0, double(win_w) * 0.5));
    double vbar_width = snapshot.vbar_width_pixels;
    if (!std::isfinite(vbar_width) || vbar_width <= detail::k_vbar_min_width_px_d) {
        vbar_width = m_impl->last_vbar_width_pixels;
    }
    if (!std::isfinite(vbar_width) || vbar_width <= detail::k_vbar_min_width_px_d) {
        vbar_width = detail::k_vbar_min_width_px_d;
    }
    vbar_width = std::clamp(vbar_width, detail::k_vbar_min_width_px_d, max_vbar_width);

    const auto make_cache_key = [&](double width_px) {
        layout_cache_key_t key;
        key.v0 = v_min;
        key.v1 = v_max;
        key.t0 = snapshot.data_cfg.t_min;
        key.t1 = snapshot.data_cfg.t_max;
        key.viewport_size = Size_2i{win_w, win_h};
        key.adjusted_reserved_height = reserved_h;
        key.adjusted_preview_height = snapshot.adjusted_preview_height;
        key.adjusted_font_size_in_pixels = snapshot.adjusted_font_px;
        key.vbar_width_pixels = width_px;
        key.font_metrics_key = layout_fonts ? layout_fonts->text_measure_cache_key() : 0;
        key.config_revision = snapshot.config_revision;
        return key;
    };

    const auto calculate_layout = [&](double width_px) {
        const auto params = build_layout_params(
            v_min,
            v_max,
            snapshot.data_cfg.t_min,
            snapshot.data_cfg.t_max,
            win_w,
            usable_height,
            width_px,
            snapshot.adjusted_preview_height,
            snapshot.adjusted_font_px,
            config,
            layout_fonts);
        return m_impl->layout_calc.calculate(params);
    };

    const frame_layout_result_t* layout_ptr =
        m_impl->layout_cache.try_get(make_cache_key(vbar_width));

    if (!layout_ptr) {
        auto layout_result = calculate_layout(vbar_width);

        double measured_vbar_width = std::max(
            detail::k_vbar_min_width_px_d,
            double(layout_result.max_v_label_text_width) + detail::k_v_label_horizontal_padding_px);
        if (!std::isfinite(measured_vbar_width) || measured_vbar_width <= 0.0) {
            measured_vbar_width = detail::k_vbar_min_width_px_d;
        }
        measured_vbar_width = std::clamp(
            measured_vbar_width,
            detail::k_vbar_min_width_px_d,
            max_vbar_width);

        if (std::abs(measured_vbar_width - vbar_width) > detail::k_vbar_width_change_threshold_d) {
            vbar_width = measured_vbar_width;
            layout_ptr = m_impl->layout_cache.try_get(make_cache_key(vbar_width));
            if (!layout_ptr) {
                layout_result = calculate_layout(vbar_width);
            }
        }
        else {
            vbar_width = measured_vbar_width;
        }

        if (!layout_ptr) {
            frame_layout_result_t cached_layout;
            cached_layout.usable_width = std::max(0.0, double(win_w) - vbar_width);
            cached_layout.usable_height = usable_height;
            cached_layout.v_bar_width = vbar_width;
            cached_layout.h_bar_height = snapshot.base_label_height_px + detail::k_scissor_pad_px;
            cached_layout.max_v_label_text_width = layout_result.max_v_label_text_width;
            cached_layout.v_labels = std::move(layout_result.v_labels);
            cached_layout.h_labels = std::move(layout_result.h_labels);
            cached_layout.v_label_fixed_digits = layout_result.v_label_fixed_digits;
            cached_layout.h_labels_subsecond = layout_result.h_labels_subsecond;
            cached_layout.vertical_seed_index = layout_result.vertical_seed_index;
            cached_layout.vertical_seed_step = layout_result.vertical_seed_step;
            cached_layout.vertical_finest_step = layout_result.vertical_finest_step;
            cached_layout.horizontal_seed_index = layout_result.horizontal_seed_index;
            cached_layout.horizontal_seed_step = layout_result.horizontal_seed_step;
            layout_ptr = &m_impl->layout_cache.store(make_cache_key(vbar_width), std::move(cached_layout));
        }
    }

    vbar_width = layout_ptr->v_bar_width;
    m_impl->last_vbar_width_pixels = vbar_width;
    if (m_impl->owner) {
        m_impl->owner->publish_measured_vbar_width(vbar_width);
    }

    frame_context_t ctx{*layout_ptr};
    ctx.v0 = v_min;
    ctx.v1 = v_max;
    ctx.preview_v0 = preview_v_min;
    ctx.preview_v1 = preview_v_max;
    if (m_impl->owner) {
        m_impl->owner->set_rendered_v_range(ctx.v0, ctx.v1);
    }
    ctx.t0 = snapshot.data_cfg.t_min;
    ctx.t1 = snapshot.data_cfg.t_max;
    if (m_impl->owner) {
        m_impl->owner->set_rendered_t_range(ctx.t0, ctx.t1);
    }
    ctx.t_available_min = snapshot.data_cfg.t_available_min;
    ctx.t_available_max = snapshot.data_cfg.t_available_max;
    ctx.win_w = win_w;
    ctx.win_h = win_h;
    // Pixel-space ortho with origin at top-left. QRhi's correction matrix
    // adapts it to the active backend's clip-space conventions.
    const glm::mat4 pixel_ortho = glm::ortho(
        0.0f,
        static_cast<float>(win_w),
        static_cast<float>(win_h),
        0.0f,
        -1.0f,
        1.0f);
    ctx.pmv = rhi_ptr
        ? to_glm_mat4(rhi_ptr->clipSpaceCorrMatrix()) * pixel_ortho
        : pixel_ortho;
    ctx.adjusted_font_px = snapshot.adjusted_font_px;
    ctx.base_label_height_px = snapshot.base_label_height_px;
    ctx.adjusted_reserved_height = reserved_h;
    ctx.adjusted_preview_height = snapshot.adjusted_preview_height;
    ctx.visible_info_flags = snapshot.visible_info_flags;
    ctx.dark_mode = config.dark_mode;
    ctx.config = &config;
    ctx.rhi = rhi_ptr;
    ctx.cb = cb;
    ctx.render_target = rt;

    // Open the resource-update batch BEFORE the render pass. Both series and
    // primitives fill it (series via prepare(), primitives via flush_rects /
    // draw_grid_shader called from chrome); beginPass then submits the
    // now-full batch atomically before any draw in the pass.
    // cb->resourceUpdate from inside an open pass is a hard error on D3D11
    // (recordingPass != NoPass asserts), so the upload work has to be over
    // by the time beginPass runs. record_draws() and series.render()
    // afterwards record draw commands only.
    {
        VNM_PLOT_PROFILE_SCOPE(profiler, "renderer.frame.render_passes");

        QRhiResourceUpdateBatch* rhi_updates =
            rhi_ptr ? rhi_ptr->nextResourceUpdateBatch() : nullptr;
        ctx.rhi_updates = rhi_updates;

        if (m_impl->series_initialized) {
            m_impl->series.prepare(ctx, snapshot.series);
        }
        // Chrome runs in two queueing phases so backgrounds and the main grid
        // stay behind the series while the zero line and preview overlay stay
        // in front. Both phases write to ctx.rhi_updates before beginPass so
        // all uploads are submitted atomically; the checkpoint between them
        // lets record_draws() replay the back-layer slice, then the series
        // renders, then record_draws() replays the front-layer slice.
        m_impl->chrome.render_grid_and_backgrounds(ctx, m_impl->primitives);
        const std::size_t back_layer_end = m_impl->primitives.queued_op_count();
        m_impl->chrome.render_zero_line(ctx, m_impl->primitives);
        if (snapshot.adjusted_preview_height > 0.0) {
            m_impl->chrome.render_preview_overlay(ctx, m_impl->primitives);
        }
        const std::size_t front_layer_end = m_impl->primitives.queued_op_count();

#if defined(VNM_PLOT_ENABLE_TEXT)
        if (m_impl->text && config.show_text) {
            m_impl->text->prepare(ctx, false, false);
        }
#endif

        cb->beginPass(rt, clear_color, QRhiDepthStencilClearValue(1.0f, 0), rhi_updates);
        cb->setViewport(QRhiViewport(0, 0, win_w, win_h));
        m_impl->primitives.record_draws(ctx, back_layer_end);
        if (m_impl->series_initialized) {
            m_impl->series.render(ctx, snapshot.series);
        }
        m_impl->primitives.record_draws(ctx, front_layer_end);
#if defined(VNM_PLOT_ENABLE_TEXT)
        if (m_impl->text && config.show_text) {
            m_impl->text->record(ctx);
        }
#endif
        cb->endPass();
        m_impl->primitives.reset_frame();
    }
}

} // namespace vnm::plot
