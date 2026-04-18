#pragma once

// VNM Plot Library - Plot Renderer
// Coordinates all sub-renderers to draw a complete plot.

#include <QQuickFramebufferObject>

#include <memory>

class QOpenGLFramebufferObject;

namespace vnm::plot {

// Forward declare the widget
class Plot_widget;

// -----------------------------------------------------------------------------
// Plot Renderer
// -----------------------------------------------------------------------------
// The main renderer that coordinates all sub-renderers and performs the
// complete plot rendering. This class lives on the render thread.
class Plot_renderer : public QQuickFramebufferObject::Renderer
{
public:
    explicit Plot_renderer(const Plot_widget* owner);
    ~Plot_renderer() override;

    // Non-copyable
    Plot_renderer(const Plot_renderer&) = delete;
    Plot_renderer& operator=(const Plot_renderer&) = delete;

    // Qt Quick FBO Renderer interface
    void synchronize(QQuickFramebufferObject* fbo_item) override;
    void render() override;
    QOpenGLFramebufferObject* createFramebufferObject(const QSize& size) override;

private:
    struct impl_t;
    std::unique_ptr<impl_t> m_impl;

    // Forwarders for posting updates to the widget's private render-thread
    // hooks. Plot_widget declares Plot_renderer (this class) as a friend, so
    // taking the pointer-to-member of a private setter is legal here; the
    // nested impl_t inherits that access through enclosing-class membership
    // and calls these helpers instead of naming the private setters directly.
    static void post_vbar_width_from_renderer(Plot_widget* widget, double px);
    static void post_auto_v_range_from_renderer(Plot_widget* widget, float v_min, float v_max);
};

} // namespace vnm::plot



