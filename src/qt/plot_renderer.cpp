#include "plot_renderer.h"
#include <vnm_plot/qt/plot_widget.h>
#include <vnm_plot/core/asset_loader.h>
#include <vnm_plot/core/chrome_renderer.h>
#include <vnm_plot/core/color_palette.h>
#include <vnm_plot/core/constants.h>
#include <vnm_plot/core/primitive_renderer.h>
#include <vnm_plot/core/series_renderer.h>

#include <QColor>
#include <QMatrix4x4>
#include <QQuickWindow>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <rhi/qrhi.h>

#include <algorithm>
#include <map>
#include <memory>
#include <shared_mutex>

namespace vnm::plot {

namespace {

glm::mat4 to_glm_mat4(const QMatrix4x4& matrix)
{
    return glm::make_mat4(matrix.constData());
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
        bool           show_info = false;
        double         adjusted_font_px = 10.0;
        double         base_label_height_px = 14.0;
        double         adjusted_preview_height = 0.0;
        double         vbar_width_pixels = 0.0;
    };

    const Plot_widget* owner = nullptr;
    render_snapshot_t  snapshot;

    Asset_loader        asset_loader;
    Series_renderer     series;
    Primitive_renderer  primitives;
    Chrome_renderer     chrome;
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
    // Primitive_renderer needs no eager GL initialization on the RHI path:
    // its GL programs only feed the raw-GL flush used by the headless
    // benchmark and standalone tests, and the RHI path loads its own QSB
    // shaders on demand inside flush_rects / draw_grid_shader. Leaving the
    // GL pipe at zero is correct here because there is no live GL context
    // to compile against.
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

    // Frame layout for chrome + series. Axis labels and tick marks ride the
    // text path, which is not yet on RHI, so v_bar_width and the base
    // label height stay zero: chrome's degenerate-rect early-exits drop the
    // empty side panes that would otherwise paint as flat color blocks.
    // usable_height carves out the preview band at the bottom; usable_width
    // covers the full widget because no v_bar is reserved.
    // Reserve room at the bottom for both the preview band and the (still
    // empty) x-axis label strip. Plot_interaction_item's reserved_height()
    // also includes both, so the QML overlay's mouse hit-tests stay aligned
    // with the rendered layout while text remains on the GL path.
    const double reserved_h = snapshot.base_label_height_px + snapshot.adjusted_preview_height;
    frame_layout_result_t layout;
    layout.usable_width  = static_cast<double>(win_w);
    layout.usable_height = static_cast<double>(
        std::max(0.0, double(win_h) - reserved_h));
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
    // Pixel-space ortho with origin at top-left. QRhi's correction matrix
    // adapts the OpenGL-style projection to the active backend's clip-space
    // conventions.
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
    // Suppress the empty h-label pane on the RHI path: chrome would
    // otherwise paint a flat-color band at the bottom because text isn't on
    // RHI yet. Setting the height to 0 makes chrome's tick-mark draw
    // early-exit (the band has zero pixels to fill).
    // Keep base_label_height_px = 0 so chrome's tick-mark draw early-exits;
    // the label strip itself is left as background color until text moves
    // onto RHI.
    ctx.base_label_height_px = 0.0;
    ctx.adjusted_reserved_height = reserved_h;
    ctx.adjusted_preview_height = snapshot.adjusted_preview_height;
    ctx.show_info = snapshot.show_info;
    // Under any RHI backend the GL fallback is a no-op; series styles that
    // do not yet route through the RHI pipeline simply skip rendering. The
    // only render path that issues real gl* calls is when no QRhi is bound
    // (tests / headless paths drive the renderer with rhi_ptr == nullptr).
    ctx.skip_gl = config.skip_gl_calls || (rhi_ptr != nullptr);
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
    QRhiResourceUpdateBatch* rhi_updates =
        rhi_ptr ? rhi_ptr->nextResourceUpdateBatch() : nullptr;
    ctx.rhi_updates = rhi_updates;

    if (m_impl->series_initialized) {
        m_impl->series.prepare(ctx, snapshot.series);
    }
    // Chrome runs in two queueing phases so the depth order matches the
    // GL flow: backgrounds + main grid behind the series, zero line +
    // preview overlay in front. Both phases write to ctx.rhi_updates
    // before beginPass so all uploads are submitted atomically; the
    // checkpoint between them lets record_draws() replay the back-layer
    // slice, then the series renders, then record_draws() replays the
    // front-layer slice.
    m_impl->chrome.render_grid_and_backgrounds(ctx, m_impl->primitives);
    const std::size_t back_layer_end = m_impl->primitives.queued_op_count();
    m_impl->chrome.render_zero_line(ctx, m_impl->primitives);
    if (snapshot.adjusted_preview_height > 0.0) {
        m_impl->chrome.render_preview_overlay(ctx, m_impl->primitives);
    }
    const std::size_t front_layer_end = m_impl->primitives.queued_op_count();

    cb->beginPass(rt, clear_color, QRhiDepthStencilClearValue(1.0f, 0), rhi_updates);
    cb->setViewport(QRhiViewport(0, 0, win_w, win_h));
    m_impl->primitives.record_draws(ctx, back_layer_end);
    if (m_impl->series_initialized) {
        m_impl->series.render(ctx, snapshot.series);
    }
    m_impl->primitives.record_draws(ctx, front_layer_end);
    cb->endPass();
    m_impl->primitives.reset_frame();
}

} // namespace vnm::plot
