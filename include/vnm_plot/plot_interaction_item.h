#pragma once

#include <vnm_plot/plot_widget.h>

#include <QBasicTimer>
#include <QQuickItem>

namespace vnm::plot {

class Plot_interaction_item : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(Plot_widget* plotWidget READ plotWidget WRITE setPlotWidget NOTIFY plotWidgetChanged REQUIRED)
    Q_PROPERTY(bool interactionEnabled READ isInteractionEnabled WRITE setInteractionEnabled NOTIFY interactionEnabledChanged)

public:
    explicit Plot_interaction_item(QQuickItem* parent = nullptr);
    ~Plot_interaction_item() override;

    Plot_widget* plotWidget() const;
    void setPlotWidget(Plot_widget* widget);

    bool isInteractionEnabled() const;
    void setInteractionEnabled(bool enabled);

signals:
    void plotWidgetChanged();
    void interactionEnabledChanged();
    void mousePositionChanged(qreal x, qreal y);
    void mouseExited();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void hoverEnterEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;
    void hoverMoveEvent(QHoverEvent* event) override;
    void timerEvent(QTimerEvent* event) override;

private:
    qreal usableWidth() const;
    qreal usableHeight() const;
    qreal previewHeight() const;
    qreal tStopMin() const;
    qreal tStopMax() const;
    void applyZoomStep();
    qreal baseK() const;

    Plot_widget* m_plot_widget = nullptr;
    bool m_interaction_enabled = true;

    bool m_dragging = false;
    bool m_dragging_preview = false;
    qreal m_drag_start_x = 0;
    qreal m_drag_last_y = 0;
    qreal m_drag_preview_start = 0;

    qreal m_zoom_vel_t = 0.0;
    qreal m_zoom_vel_v = 0.0;
    qreal m_last_pivot_x = 0.5;
    qreal m_last_pivot_y = 0.5;
    QBasicTimer m_zoom_timer;

    static constexpr qreal k_zoom_friction = 0.75;
    static constexpr qreal k_zoom_impulse_per_step = 1.0;
    static constexpr qreal k_zoom_max_vel = 5.0;
    static constexpr qreal k_zoom_per_notch = 1.05;
    static constexpr int k_zoom_timer_interval_ms = 16;
};

} // namespace vnm::plot
