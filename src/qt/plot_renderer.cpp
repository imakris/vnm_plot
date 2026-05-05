#include "plot_renderer.h"
#include <vnm_plot/qt/plot_widget.h>
#include <vnm_plot/core/asset_loader.h>
#include <vnm_plot/core/color_palette.h>
#include <vnm_plot/core/constants.h>
#include <vnm_plot/core/series_renderer.h>

#include <QColor>
#include <QQuickWindow>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <rhi/qrhi.h>

#include <algorithm>
#include <map>
#include <memory>
#include <shared_mutex>

namespace vnm::plot {

struct Plot_renderer::impl_t
{
    // Snapshot of the widget state the render path reads. Held by value so
    // render() runs on the renderer thread without re-acquiring Plot_widget
    // locks. C1 reinstates the data_cfg, series map, and adjusted preview
    // height that Batch B dropped together with the GL renderer.
    struct render_snapshot_t
    {
        Plot_config    config;
        data_config_t  data_cfg;
        std::map<int, std::shared_ptr<const series_data_t>> series;
        bool           v_auto = true;
        bool           show_info = false;
        double         adjusted_font_px = 10.0;
        double         base_label_height_px = 14.0;
        double         adjusted_preview_height = 0.0;
        double         vbar_width_pixels = 0.0;
    };

    const Plot_widget* owner = nullptr;
    render_snapshot_t  snapshot;

    Asset_loader      asset_loader;
    Series_renderer   series;
    bool              series_initialized = false;
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
    m_impl->snapshot.show_info = widget->m_show_info.load(std::memory_order_acquire);
    m_impl->snapshot.adjusted_font_px = widget->m_adjusted_font_size;
    m_impl->snapshot.base_label_height_px = widget->m_base_label_height;
    m_impl->snapshot.adjusted_preview_height = widget->m_adjusted_preview_height;
    m_impl->snapshot.vbar_width_pixels = widget->vbar_width_pixels();
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

    const QSize pixel_size = rt->pixelSize();
    const int win_w = pixel_size.width();
    const int win_h = pixel_size.height();

    // Minimal frame_layout_result_t. The series renderer reads usable_width,
    // usable_height, and h_bar_height (used as scissor padding under the
    // preview pane). The chrome and label fields stay zero because chrome and
    // labels do not go through this renderer path.
    frame_layout_result_t layout;
    layout.usable_width  = static_cast<double>(win_w);
    layout.usable_height = static_cast<double>(
        std::max(0.0, double(win_h) - snapshot.adjusted_preview_height));
    layout.v_bar_width   = 0.0;
    layout.h_bar_height  = 0.0;
    layout.max_v_label_text_width = 0.0f;

    frame_context_t ctx{layout};
    ctx.v0 = snapshot.v_auto ? snapshot.data_cfg.v_min : snapshot.data_cfg.v_manual_min;
    ctx.v1 = snapshot.v_auto ? snapshot.data_cfg.v_max : snapshot.data_cfg.v_manual_max;
    ctx.preview_v0 = ctx.v0;
    ctx.preview_v1 = ctx.v1;
    ctx.t0 = snapshot.data_cfg.t_min;
    ctx.t1 = snapshot.data_cfg.t_max;
    ctx.t_available_min = snapshot.data_cfg.t_available_min;
    ctx.t_available_max = snapshot.data_cfg.t_available_max;
    ctx.win_w = win_w;
    ctx.win_h = win_h;
    // Pixel-space ortho with origin at top-left. Plot_widget keeps
    // setMirrorVertically(true) on QQuickRhiItem so the offscreen FBO is
    // sampled flipped, giving the same visual orientation the GL path
    // produced.
    ctx.pmv = glm::ortho(
        0.0f,
        static_cast<float>(win_w),
        static_cast<float>(win_h),
        0.0f,
        -1.0f,
        1.0f);
    ctx.adjusted_font_px = snapshot.adjusted_font_px;
    ctx.base_label_height_px = snapshot.base_label_height_px;
    ctx.adjusted_reserved_height =
        snapshot.base_label_height_px + snapshot.adjusted_preview_height;
    ctx.adjusted_preview_height = snapshot.adjusted_preview_height;
    ctx.show_info = snapshot.show_info;
    // Under any RHI backend the legacy GL fallback is a no-op; series styles
    // that have not yet moved to the RHI pipeline simply skip rendering. The
    // only render path that issues real gl* calls is when no QRhi is bound
    // (tests / headless paths drive the renderer with rhi_ptr == nullptr).
    ctx.skip_gl = config.skip_gl_calls || (rhi_ptr != nullptr);
    ctx.dark_mode = config.dark_mode;
    ctx.config = &config;
    ctx.rhi = rhi_ptr;
    ctx.cb = cb;
    ctx.render_target = rt;

    // Open the resource-update batch BEFORE the render pass. The series
    // renderer fills it via prepare(); beginPass then submits the now-full
    // batch atomically before any draw in the pass. cb->resourceUpdate from
    // inside an open pass is a hard error on D3D11 (recordingPass != NoPass
    // asserts), so the upload work has to be over by the time beginPass
    // runs. render() afterwards records draw commands only.
    QRhiResourceUpdateBatch* rhi_updates =
        rhi_ptr ? rhi_ptr->nextResourceUpdateBatch() : nullptr;
    ctx.rhi_updates = rhi_updates;

    if (m_impl->series_initialized) {
        m_impl->series.prepare(ctx, snapshot.series);
    }
    cb->beginPass(rt, clear_color, QRhiDepthStencilClearValue(1.0f, 0), rhi_updates);
    if (m_impl->series_initialized) {
        m_impl->series.render(ctx, snapshot.series);
    }
    cb->endPass();
}

} // namespace vnm::plot
