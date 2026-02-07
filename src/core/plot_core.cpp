#include <vnm_plot/core/plot_core.h>

#include <vnm_plot/core/font_renderer.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace vnm::plot {

Plot_core::Plot_core() = default;
Plot_core::~Plot_core() = default;

bool Plot_core::initialize()
{
    return initialize(init_params_t{});
}

bool Plot_core::initialize(const init_params_t& params)
{
    if (m_initialized) {
        return true;
    }

    if (params.init_embedded_assets) {
        init_embedded_assets(m_asset_loader);
    }

    if (!m_primitives.initialize(m_asset_loader)) {
        return false;
    }

    m_series.initialize(m_asset_loader);

#if defined(VNM_PLOT_ENABLE_TEXT)
    if (params.enable_text) {
        m_fonts = std::make_unique<Font_renderer>();
        m_text = std::make_unique<Text_renderer>(m_fonts.get());
    }
#else
    (void)params;
#endif

    m_initialized = true;
    return true;
}

void Plot_core::cleanup_gl_resources()
{
    m_series.cleanup_gl_resources();
    m_primitives.cleanup_gl_resources();
#if defined(VNM_PLOT_ENABLE_TEXT)
    if (m_fonts) {
        m_fonts->deinitialize();
        Font_renderer::cleanup_thread_resources();
    }
#endif
}

void Plot_core::render(
    const render_params_t& params,
    const std::map<int, std::shared_ptr<const series_data_t>>& series,
    const Plot_config* config)
{
    if (!m_initialized || params.width <= 0 || params.height <= 0) {
        return;
    }

    const bool skip_gl = config && config->skip_gl_calls;

    vnm::plot::Profiler* profiler = config ? config->profiler.get() : nullptr;
    m_primitives.set_profiler(profiler);

    frame_layout_result_t layout;
    layout.usable_width = std::max(0.0, double(params.width) - params.vbar_width_px);
    layout.usable_height = std::max(0.0, double(params.height) - params.adjusted_reserved_height);
    layout.v_bar_width = params.vbar_width_px;
    layout.h_bar_height = params.hbar_height_px;

    const glm::mat4 pmv = glm::ortho(
        0.f,
        static_cast<float>(params.width),
        static_cast<float>(params.height),
        0.f,
        -1.f,
        1.f);

    frame_context_t ctx = Frame_context_builder(layout)
        .v_range(params.v_min, params.v_max)
        .preview_v_range(params.preview_v_min, params.preview_v_max)
        .t_range(params.t_min, params.t_max)
        .available_t_range(params.t_available_min, params.t_available_max)
        .window_size(params.width, params.height)
        .pmv(pmv)
        .font_px(params.adjusted_font_px, params.base_label_height_px)
        .reserved_heights(params.adjusted_reserved_height, params.adjusted_preview_height)
        .show_info(params.show_info)
        .config(config)
        .build();

#if defined(VNM_PLOT_ENABLE_TEXT)
    if (!skip_gl && m_fonts && m_text && config && config->show_text) {
        const int font_px = static_cast<int>(std::lround(params.adjusted_font_px));
        const bool force_rebuild = (font_px != m_last_font_px);
        if (font_px > 0) {
            m_fonts->set_log_callbacks(config->log_error, config->log_debug);
            m_fonts->initialize(m_asset_loader, font_px, force_rebuild);
            m_last_font_px = font_px;
        }
    }
#endif

    m_chrome.render_grid_and_backgrounds(ctx, m_primitives);
    if (!series.empty()) {
        m_series.render(ctx, series);
    }
    m_chrome.render_zero_line(ctx, m_primitives);
    if (params.adjusted_preview_height > 0.0) {
        m_chrome.render_preview_overlay(ctx, m_primitives);
    }
    if (!skip_gl) {
        m_primitives.flush_rects(ctx.pmv);
    }

#if defined(VNM_PLOT_ENABLE_TEXT)
    if (!skip_gl && m_text && config && config->show_text) {
        m_text->render(ctx, false, false);
    }
#endif
}

} // namespace vnm::plot
