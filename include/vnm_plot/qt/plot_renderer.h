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
};

} // namespace vnm::plot



