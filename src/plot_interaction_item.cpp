#include <vnm_plot/plot_interaction_item.h>

#include <QMouseEvent>
#include <QWheelEvent>
#include <QHoverEvent>
#include <QTimerEvent>

#include <algorithm>
#include <cmath>

namespace vnm::plot {

Plot_interaction_item::Plot_interaction_item(QQuickItem* parent)
    : QQuickItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
}

Plot_interaction_item::~Plot_interaction_item()
{
    m_zoom_timer.stop();
}

Plot_widget* Plot_interaction_item::plotWidget() const
{
    return m_plot_widget;
}

void Plot_interaction_item::setPlotWidget(Plot_widget* widget)
{
    if (m_plot_widget == widget) {
        return;
    }
    m_plot_widget = widget;
    emit plotWidgetChanged();
}

bool Plot_interaction_item::isInteractionEnabled() const
{
    return m_interaction_enabled;
}

void Plot_interaction_item::setInteractionEnabled(bool enabled)
{
    if (m_interaction_enabled == enabled) {
        return;
    }
    m_interaction_enabled = enabled;
    emit interactionEnabledChanged();
}

qreal Plot_interaction_item::usableWidth() const
{
    if (!m_plot_widget) {
        return width();
    }
    return width() - m_plot_widget->vbar_width_qml();
}

qreal Plot_interaction_item::usableHeight() const
{
    if (!m_plot_widget) {
        return height();
    }
    return height() - m_plot_widget->reserved_height();
}

qreal Plot_interaction_item::previewHeight() const
{
    if (!m_plot_widget) {
        return 0.0;
    }
    return m_plot_widget->preview_height();
}

qreal Plot_interaction_item::tStopMin() const
{
    if (!m_plot_widget) {
        return 0.0;
    }
    const qreal t_available_span = std::max(1e-9,
        m_plot_widget->t_available_max() - m_plot_widget->t_available_min());
    return (m_plot_widget->t_min() - m_plot_widget->t_available_min()) / t_available_span;
}

qreal Plot_interaction_item::tStopMax() const
{
    if (!m_plot_widget) {
        return 1.0;
    }
    const qreal t_available_span = std::max(1e-9,
        m_plot_widget->t_available_max() - m_plot_widget->t_available_min());
    return 1.0 - (m_plot_widget->t_available_max() - m_plot_widget->t_max()) / t_available_span;
}

qreal Plot_interaction_item::baseK() const
{
    return std::pow(k_zoom_per_notch, (1.0 - k_zoom_friction) / k_zoom_impulse_per_step);
}

void Plot_interaction_item::applyZoomStep()
{
    if (!m_plot_widget) {
        m_zoom_timer.stop();
        return;
    }

    constexpr qreal eps = 1e-3;
    bool active = false;

    if (std::abs(m_zoom_vel_t) > eps) {
        const qreal factor_t = std::pow(baseK(), m_zoom_vel_t);
        if (factor_t < 1.0 && !m_plot_widget->can_zoom_in()) {
            m_zoom_vel_t = 0.0;
        }
        else {
            m_plot_widget->adjust_t_from_pivot_and_scale(m_last_pivot_x, factor_t);
            m_zoom_vel_t *= k_zoom_friction;
            active = true;
        }
    }

    if (std::abs(m_zoom_vel_v) > eps) {
        const qreal factor_v = std::pow(baseK(), m_zoom_vel_v);
        m_plot_widget->adjust_v_from_pivot_and_scale(m_last_pivot_y, factor_v);
        m_zoom_vel_v *= k_zoom_friction;
        active = true;
    }

    if (!active) {
        m_zoom_timer.stop();
    }
}

void Plot_interaction_item::mousePressEvent(QMouseEvent* event)
{
    if (!m_interaction_enabled || !m_plot_widget) {
        event->ignore();
        return;
    }

    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }

    const qreal y = event->position().y();
    const qreal x = event->position().x();
    const qreal uh = usableHeight();
    const qreal ph = previewHeight();

    if (y < uh) {
        m_dragging = true;
        m_drag_start_x = x;
        m_drag_last_y = y;
        event->accept();
    }
    else if (ph > 0 && y > height() - ph) {
        const qreal stop_min = tStopMin();
        const qreal stop_max = tStopMax();
        if (x < width() * stop_min || x > width() * stop_max) {
            m_plot_widget->adjust_t_from_mouse_pos_on_preview(width(), x);
        }
        m_dragging_preview = true;
        m_drag_preview_start = x;
        event->accept();
    }
    else {
        event->ignore();
    }
}

void Plot_interaction_item::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_interaction_enabled) {
        return;
    }

    const qreal x = event->position().x();
    const qreal y = event->position().y();

    emit mousePositionChanged(x, y);

    if (!m_plot_widget) {
        return;
    }

    if (m_dragging) {
        const auto mods = event->modifiers();
        const bool ctrl_held = mods & Qt::ControlModifier;
        const bool alt_held = mods & Qt::AltModifier;

        if (ctrl_held || alt_held) {
            const qreal dy = y - m_drag_last_y;
            m_drag_last_y = y;
            m_plot_widget->adjust_v_from_mouse_diff(usableHeight(), dy);
        }

        if (!alt_held) {
            m_plot_widget->adjust_t_from_mouse_diff(usableWidth(), x - m_drag_start_x);
            m_drag_start_x = x;
        }
    }
    else if (m_dragging_preview) {
        m_plot_widget->adjust_t_from_mouse_diff_on_preview(width(), x - m_drag_preview_start);
        m_drag_preview_start = x;
    }
}

void Plot_interaction_item::mouseReleaseEvent(QMouseEvent* event)
{
    Q_UNUSED(event)
    m_dragging = false;
    m_dragging_preview = false;
    m_drag_start_x = 0;
    m_drag_preview_start = 0;
}

void Plot_interaction_item::wheelEvent(QWheelEvent* event)
{
    if (!m_interaction_enabled || !m_plot_widget) {
        event->ignore();
        return;
    }

    const qreal ph = previewHeight();
    const qreal y = event->position().y();

    if (ph > 0 && y > height() - (ph - 1)) {
        event->ignore();
        return;
    }

    qreal dy = event->angleDelta().y();
    if (dy == 0.0) {
        dy = event->pixelDelta().y();
    }
    if (dy == 0.0) {
        dy = event->angleDelta().x();
    }
    if (dy == 0.0) {
        dy = event->pixelDelta().x();
    }
    if (dy == 0.0) {
        event->ignore();
        return;
    }

    const auto mods = event->modifiers();
    const qreal steps = dy / 120.0;
    const qreal impulse = -steps * k_zoom_impulse_per_step;
    const bool zoom_time = (mods & Qt::ControlModifier) || !(mods & Qt::AltModifier);

    if (zoom_time && impulse < 0 && !m_plot_widget->can_zoom_in()) {
        event->ignore();
        return;
    }

    const qreal uw = usableWidth();
    const qreal uh = usableHeight();
    const qreal wx = std::min(event->position().x(), uw);
    const qreal px = uw > 0 ? (wx / uw) : 0.5;
    const qreal py = uh > 0 ? (y / uh) : 0.5;

    m_last_pivot_x = px;
    m_last_pivot_y = py;

    if (mods & Qt::ControlModifier) {
        m_zoom_vel_t += impulse;
        m_zoom_vel_v += impulse;
    }
    else if (mods & Qt::AltModifier) {
        m_zoom_vel_v += impulse;
    }
    else {
        m_zoom_vel_t += impulse;
    }

    m_zoom_vel_t = std::clamp(m_zoom_vel_t, -k_zoom_max_vel, k_zoom_max_vel);
    m_zoom_vel_v = std::clamp(m_zoom_vel_v, -k_zoom_max_vel, k_zoom_max_vel);

    applyZoomStep();

    if (!m_zoom_timer.isActive()) {
        m_zoom_timer.start(k_zoom_timer_interval_ms, this);
    }

    event->accept();
}

void Plot_interaction_item::hoverEnterEvent(QHoverEvent* event)
{
    // Always emit hover signals regardless of interactionEnabled.
    // The indicator (value display) should work even when pan/zoom is disabled.
    emit mousePositionChanged(event->position().x(), event->position().y());
}

void Plot_interaction_item::hoverLeaveEvent(QHoverEvent* event)
{
    Q_UNUSED(event)
    // Always emit mouseExited regardless of interactionEnabled.
    emit mouseExited();
}

void Plot_interaction_item::hoverMoveEvent(QHoverEvent* event)
{
    // Always emit hover signals regardless of interactionEnabled.
    emit mousePositionChanged(event->position().x(), event->position().y());
}

void Plot_interaction_item::timerEvent(QTimerEvent* event)
{
    if (event->timerId() == m_zoom_timer.timerId()) {
        applyZoomStep();
    }
    else {
        QQuickItem::timerEvent(event);
    }
}

} // namespace vnm::plot
