#include <vnm_plot/core/plot_core.h>

#include <vnm_plot/core/algo.h>
#include <vnm_plot/core/asset_loader.h>
#include <vnm_plot/core/chrome_renderer.h>
#include <vnm_plot/core/constants.h>
#include <vnm_plot/core/font_renderer.h>
#include <vnm_plot/core/layout_calculator.h>
#include <vnm_plot/core/plot_config.h>
#include <vnm_plot/core/primitive_renderer.h>
#include <vnm_plot/core/series_renderer.h>
#include <vnm_plot/core/text_renderer.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace vnm::plot {
namespace {

double sanitize_positive(double value, double fallback)
{
    if (!std::isfinite(value) || value <= 0.0) {
        return fallback;
    }
    return value;
}

double sanitize_non_negative(double value, double fallback)
{
    if (!std::isfinite(value) || value < 0.0) {
        return fallback;
    }
    return value;
}

Layout_calculator::parameters_t build_layout_params(
    float v_min,
    float v_max,
    double t_min,
    double t_max,
    int win_w,
    double usable_height,
    double vbar_width,
    double preview_height,
    double font_px,
    const Plot_config* config,
    const Font_renderer* fonts)
{
    Layout_calculator::parameters_t params;
    params.v_min = v_min;
    params.v_max = v_max;
    params.t_min = t_min;
    params.t_max = t_max;
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

    if (config) {
        params.get_required_fixed_digits_func = [](double) { return 2; };
        params.format_timestamp_func = [config](double ts, double step) -> std::string {
            if (config->format_timestamp) {
                return config->format_timestamp(ts, step);
            }
            return format_axis_fixed_or_int(ts, 3);
        };
        params.profiler = config->profiler.get();
    }

    return params;
}

} // namespace

struct Plot_core::impl_t
{
    Asset_loader asset_loader;
    Primitive_renderer primitives;
    Series_renderer series;
    Chrome_renderer chrome;
    std::unique_ptr<Font_renderer> fonts;
    std::unique_ptr<Text_renderer> text;
    int last_font_px = 0;
    bool initialized = false;
};

Plot_core::Plot_core()
:
    m_impl(std::make_unique<impl_t>())
{}

Plot_core::~Plot_core() = default;

bool Plot_core::initialize()
{
    return initialize(init_params_t{});
}

bool Plot_core::initialize(const init_params_t& params)
{
    if (m_impl->initialized) {
        return true;
    }

    if (params.init_embedded_assets) {
        init_embedded_assets(m_impl->asset_loader);
    }

    if (!m_impl->primitives.initialize(m_impl->asset_loader)) {
        return false;
    }

    m_impl->series.initialize(m_impl->asset_loader);

#if defined(VNM_PLOT_ENABLE_TEXT)
    if (params.enable_text) {
        m_impl->fonts = std::make_unique<Font_renderer>();
        m_impl->text = std::make_unique<Text_renderer>(m_impl->fonts.get());
    }
#else
    (void)params;
#endif

    m_impl->initialized = true;
    return true;
}

void Plot_core::cleanup_gl_resources()
{
    m_impl->series.cleanup_gl_resources();
    m_impl->primitives.cleanup_gl_resources();
#if defined(VNM_PLOT_ENABLE_TEXT)
    if (m_impl->fonts) {
        m_impl->fonts->deinitialize();
        Font_renderer::cleanup_thread_resources();
    }
#endif
}

Asset_loader& Plot_core::asset_loader() { return m_impl->asset_loader; }
Primitive_renderer& Plot_core::primitives() { return m_impl->primitives; }
Series_renderer& Plot_core::series_renderer() { return m_impl->series; }
Chrome_renderer& Plot_core::chrome_renderer() { return m_impl->chrome; }
Text_renderer* Plot_core::text_renderer() { return m_impl->text.get(); }

void Plot_core::render(
    const render_params_t& params,
    const std::map<int, std::shared_ptr<const series_data_t>>& series,
    const Plot_config* config)
{
    if (!m_impl->initialized || params.width <= 0 || params.height <= 0) {
        return;
    }

    const bool skip_gl = config && config->skip_gl_calls;

    vnm::plot::Profiler* profiler = config ? config->profiler.get() : nullptr;
    m_impl->primitives.set_profiler(profiler);

    const float preview_v_min = params.preview_v_min.value_or(params.v_min);
    const float preview_v_max = params.preview_v_max.value_or(params.v_max);
    const double t_available_min = params.t_available_min.value_or(params.t_min);
    const double t_available_max = params.t_available_max.value_or(params.t_max);

    const double font_px = sanitize_positive(
        config ? config->font_size_px : detail::k_default_font_px,
        detail::k_default_font_px);
    const double base_label_height_px = sanitize_non_negative(
        config ? config->base_label_height_px : detail::k_default_base_label_height_px,
        detail::k_default_base_label_height_px);

    double preview_height = 0.0;
    if (config) {
        preview_height = sanitize_non_negative(config->preview_height_px, 0.0);
    }

    const double reserved_height = base_label_height_px + preview_height;
    const double usable_height = std::max(0.0, double(params.height) - reserved_height);

#if defined(VNM_PLOT_ENABLE_TEXT)
    if (!skip_gl && m_impl->fonts && m_impl->text && config && config->show_text) {
        const int font_px_int = static_cast<int>(std::lround(font_px));
        const bool force_rebuild = (font_px_int != m_impl->last_font_px);
        if (font_px_int > 0) {
            m_impl->fonts->set_log_callbacks(config->log_error, config->log_debug);
            m_impl->fonts->initialize(m_impl->asset_loader, font_px_int, force_rebuild);
            m_impl->last_font_px = font_px_int;
        }
    }
#endif

    const Font_renderer* fonts = (config && config->show_text) ? m_impl->fonts.get() : nullptr;

    Layout_calculator layout_calc;
    frame_layout_result_t layout;
    double vbar_width = detail::k_vbar_min_width_px_d;

    auto layout_params = build_layout_params(
        params.v_min, params.v_max, params.t_min, params.t_max, params.width,
        usable_height, vbar_width, preview_height, font_px, config, fonts);
    auto layout_result = layout_calc.calculate(layout_params);

    double measured_vbar_width = std::max(
        detail::k_vbar_min_width_px_d,
        double(layout_result.max_v_label_text_width) + detail::k_v_label_horizontal_padding_px);
    if (!std::isfinite(measured_vbar_width) || measured_vbar_width <= 0.0) {
        measured_vbar_width = detail::k_vbar_min_width_px_d;
    }

    if (std::abs(measured_vbar_width - vbar_width) > detail::k_vbar_width_change_threshold_d) {
        vbar_width = measured_vbar_width;
        layout_params = build_layout_params(
            params.v_min, params.v_max, params.t_min, params.t_max, params.width,
            usable_height, vbar_width, preview_height, font_px, config, fonts);
        layout_result = layout_calc.calculate(layout_params);
    }
    else {
        vbar_width = measured_vbar_width;
    }

    layout.usable_width = std::max(0.0, double(params.width) - vbar_width);
    layout.usable_height = usable_height;
    layout.v_bar_width = vbar_width;
    layout.h_bar_height = base_label_height_px + detail::k_scissor_pad_px;
    layout.max_v_label_text_width = layout_result.max_v_label_text_width;
    layout.v_labels = std::move(layout_result.v_labels);
    layout.h_labels = std::move(layout_result.h_labels);
    layout.v_label_fixed_digits = layout_result.v_label_fixed_digits;
    layout.h_labels_subsecond = layout_result.h_labels_subsecond;
    layout.vertical_seed_index = layout_result.vertical_seed_index;
    layout.vertical_seed_step = layout_result.vertical_seed_step;
    layout.vertical_finest_step = layout_result.vertical_finest_step;
    layout.horizontal_seed_index = layout_result.horizontal_seed_index;
    layout.horizontal_seed_step = layout_result.horizontal_seed_step;

    const glm::mat4 pmv = glm::ortho(
        0.f,
        static_cast<float>(params.width),
        static_cast<float>(params.height),
        0.f,
        -1.f,
        1.f);

    frame_context_t ctx{layout};
    ctx.v0 = params.v_min;
    ctx.v1 = params.v_max;
    ctx.preview_v0 = preview_v_min;
    ctx.preview_v1 = preview_v_max;
    ctx.t0 = params.t_min;
    ctx.t1 = params.t_max;
    ctx.t_available_min = t_available_min;
    ctx.t_available_max = t_available_max;
    ctx.win_w = params.width;
    ctx.win_h = params.height;
    ctx.pmv = pmv;
    ctx.adjusted_font_px = font_px;
    ctx.base_label_height_px = base_label_height_px;
    ctx.adjusted_reserved_height = reserved_height;
    ctx.adjusted_preview_height = preview_height;
    ctx.show_info = params.show_info;
    ctx.skip_gl = config && config->skip_gl_calls;
    ctx.dark_mode = config ? config->dark_mode : false;
    ctx.config = config;

    m_impl->chrome.render_grid_and_backgrounds(ctx, m_impl->primitives);
    if (!series.empty()) {
        m_impl->series.render(ctx, series);
    }
    m_impl->chrome.render_zero_line(ctx, m_impl->primitives);
    if (preview_height > 0.0) {
        m_impl->chrome.render_preview_overlay(ctx, m_impl->primitives);
    }
    if (!skip_gl) {
        m_impl->primitives.flush_rects(ctx.pmv);
    }

#if defined(VNM_PLOT_ENABLE_TEXT)
    if (!skip_gl && m_impl->text && config && config->show_text) {
        m_impl->text->render(ctx, false, false);
    }
#endif
}

} // namespace vnm::plot
