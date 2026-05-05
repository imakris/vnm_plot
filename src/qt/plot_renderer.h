#pragma once

// VNM Plot Library - Plot Renderer
// QQuickRhiItem renderer for Plot_widget. Synchronizes widget state on the
// render thread and issues an RHI render pass that clears to the configured
// background color.

#include <QQuickRhiItem>

#include <memory>

namespace vnm::plot {

class Plot_widget;

class Plot_renderer : public QQuickRhiItemRenderer
{
public:
    explicit Plot_renderer(const Plot_widget* owner);
    ~Plot_renderer() override;

    Plot_renderer(const Plot_renderer&) = delete;
    Plot_renderer& operator=(const Plot_renderer&) = delete;

    void initialize(QRhiCommandBuffer* cb) override;
    void synchronize(QQuickRhiItem* item) override;
    void render(QRhiCommandBuffer* cb) override;

private:
    struct impl_t;
    std::unique_ptr<impl_t> m_impl;
};

} // namespace vnm::plot
