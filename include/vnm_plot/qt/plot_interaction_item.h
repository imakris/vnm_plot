#pragma once

#include <vnm_plot/qt/plot_widget.h>

#include <QBasicTimer>
#include <QQuickItem>

namespace vnm::plot {

class Plot_interaction_item : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(Plot_widget* plot_widget READ plot_widget WRITE set_plot_widget NOTIFY plot_widget_changed REQUIRED)
    Q_PROPERTY(bool interaction_enabled READ is_interaction_enabled WRITE set_interaction_enabled NOTIFY interaction_enabled_changed)

public:
    explicit Plot_interaction_item(QQuickItem* parent = nullptr);
    ~Plot_interaction_item() override;

    Plot_widget* plot_widget() const;
    void set_plot_widget(Plot_widget* widget);

    bool is_interaction_enabled() const;
    void set_interaction_enabled(bool enabled);

signals:
    void plot_widget_changed();
    void interaction_enabled_changed();
    void mouse_position_changed(qreal x, qreal y);
    void mouse_exited();

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
    qreal usable_width() const;
    qreal usable_height() const;
    qreal preview_height() const;
    qreal t_stop_min() const;
    qreal t_stop_max() const;
    void apply_zoom_step();
    qreal base_k() const;

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
