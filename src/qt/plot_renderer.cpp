#include "plot_renderer.h"
#include <vnm_plot/qt/plot_widget.h>
#include <vnm_plot/core/color_palette.h>

#include <QColor>
#include <rhi/qrhi.h>

#include <shared_mutex>

namespace vnm::plot {

struct Plot_renderer::impl_t
{
    // Snapshot of the configuration fields the render pass reads. Held by
    // value so render() runs on the renderer thread without re-acquiring
    // Plot_widget locks.
    struct render_snapshot_t
    {
        Plot_config config;
    };

    const Plot_widget* owner = nullptr;
    render_snapshot_t snapshot;
};

Plot_renderer::Plot_renderer(const Plot_widget* owner)
    : m_impl(std::make_unique<impl_t>())
{
    m_impl->owner = owner;
}

Plot_renderer::~Plot_renderer() = default;

void Plot_renderer::initialize(QRhiCommandBuffer* /*cb*/)
{
    // QQuickRhiItemRenderer requires the override; the clear-only render path
    // holds no persistent GPU resources so there is nothing to set up here.
}

void Plot_renderer::synchronize(QQuickRhiItem* item)
{
    auto* widget = static_cast<Plot_widget*>(item);
    if (!widget) {
        return;
    }

    std::shared_lock lock(widget->m_config_mutex);
    m_impl->snapshot.config = widget->m_config;
}

void Plot_renderer::render(QRhiCommandBuffer* cb)
{
    QRhiRenderTarget* const rt = renderTarget();
    if (!cb || !rt) {
        return;
    }

    const Plot_config& config = m_impl->snapshot.config;
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

    cb->beginPass(rt, clear_color, QRhiDepthStencilClearValue(1.0f, 0));
    cb->endPass();
}

} // namespace vnm::plot
