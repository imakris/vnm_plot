#pragma once

// VNM Plot Library - Plot Renderer
// Coordinates all sub-renderers to draw a complete plot.

#include <QQuickFramebufferObject>

#include <memory>

class QOpenGLFramebufferObject;

namespace vnm::plot {

class Plot_widget;

class Plot_renderer : public QQuickFramebufferObject::Renderer
{
public:
    explicit Plot_renderer(const Plot_widget* owner);
    ~Plot_renderer() override;

    Plot_renderer(const Plot_renderer&) = delete;
    Plot_renderer& operator=(const Plot_renderer&) = delete;

    void synchronize(QQuickFramebufferObject* fbo_item) override;
    void render() override;
    QOpenGLFramebufferObject* createFramebufferObject(const QSize& size) override;

private:
    struct impl_t;
    std::unique_ptr<impl_t> m_impl;

    static void post_vbar_width_from_renderer(Plot_widget* widget, double px);
    static void post_auto_v_range_from_renderer(Plot_widget* widget, float v_min, float v_max);
};

} // namespace vnm::plot
