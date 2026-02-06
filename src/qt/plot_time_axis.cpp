#include <vnm_plot/qt/plot_time_axis.h>

#include <cmath>

namespace vnm::plot {

namespace {
constexpr double k_axis_eps = 1e-12;
constexpr double k_axis_min_span_abs = 1e-9;
constexpr double k_axis_min_span_rel = 1e-9;

double min_span_for(double value, double avail_span)
{
    double span = std::max(k_axis_min_span_abs, std::abs(value) * k_axis_min_span_rel);
    if (avail_span > 0.0) {
        span = std::min(span, avail_span);
        if (!(span > 0.0)) {
            span = std::max(k_axis_min_span_abs, std::abs(value) * k_axis_min_span_rel);
        }
    }
    return span;
}
}

Plot_time_axis::Plot_time_axis(QObject* parent)
    : QObject(parent)
{}

double Plot_time_axis::t_min() const
{
    return m_t_min;
}

double Plot_time_axis::t_max() const
{
    return m_t_max;
}

double Plot_time_axis::t_available_min() const
{
    return m_t_available_min;
}

double Plot_time_axis::t_available_max() const
{
    return m_t_available_max;
}

void Plot_time_axis::set_t_min(double v)
{
    double new_min = v;
    double new_max = m_t_max;
    if (v >= m_t_max) {
        double span = m_t_max - m_t_min;
        if (!(span > 0.0)) {
            const double avail_span = m_t_available_max - m_t_available_min;
            span = min_span_for(v, avail_span);
        }
        new_max = v + span;
    }
    adjust_t_to_target(new_min, new_max);
}

void Plot_time_axis::set_t_max(double v)
{
    double new_min = m_t_min;
    double new_max = v;
    if (v <= m_t_min) {
        double span = m_t_max - m_t_min;
        if (!(span > 0.0)) {
            const double avail_span = m_t_available_max - m_t_available_min;
            span = min_span_for(v, avail_span);
        }
        new_min = v - span;
    }
    adjust_t_to_target(new_min, new_max);
}

void Plot_time_axis::set_t_available_min(double v)
{
    set_limits_if_changed(m_t_min, m_t_max, v, m_t_available_max);
}

void Plot_time_axis::set_t_available_max(double v)
{
    set_limits_if_changed(m_t_min, m_t_max, m_t_available_min, v);
}

void Plot_time_axis::set_t_range(double t_min, double t_max)
{
    set_limits_if_changed(t_min, t_max, m_t_available_min, m_t_available_max);
}

void Plot_time_axis::set_available_t_range(double t_available_min, double t_available_max)
{
    double new_t_min = m_t_min;
    double new_t_max = m_t_max;

    if (t_available_max > t_available_min) {
        const double span = t_available_max - t_available_min;
        const double cur_span = new_t_max - new_t_min;
        if (cur_span > span) {
            new_t_min = t_available_min;
            new_t_max = t_available_max;
        }
        else {
            if (new_t_min < t_available_min) {
                new_t_min = t_available_min;
                new_t_max = t_available_min + cur_span;
            }
            if (new_t_max > t_available_max) {
                new_t_max = t_available_max;
                new_t_min = t_available_max - cur_span;
            }
        }
    }

    set_limits_if_changed(new_t_min, new_t_max, t_available_min, t_available_max);
}

void Plot_time_axis::adjust_t_from_mouse_diff(double ref_width, double diff)
{
    if (ref_width <= 0.0) {
        return;
    }

    const double span = m_t_max - m_t_min;
    const double delta = diff * span / ref_width;
    adjust_t_to_target(m_t_min - delta, m_t_max - delta);
}

void Plot_time_axis::adjust_t_from_mouse_diff_on_preview(double ref_width, double diff)
{
    if (ref_width <= 0.0) {
        return;
    }

    const double avail_span = m_t_available_max - m_t_available_min;
    const double delta = diff * avail_span / ref_width;
    adjust_t_to_target(m_t_min + delta, m_t_max + delta);
}

void Plot_time_axis::adjust_t_from_mouse_pos_on_preview(double ref_width, double x_pos)
{
    if (ref_width <= 0.0) {
        return;
    }

    const double span = m_t_max - m_t_min;
    const double avail_span = m_t_available_max - m_t_available_min;
    const double rel = x_pos / ref_width;
    const double new_center = m_t_available_min + rel * avail_span;
    adjust_t_to_target(new_center - span * 0.5, new_center + span * 0.5);
}

void Plot_time_axis::adjust_t_from_pivot_and_scale(double pivot, double scale)
{
    if (scale <= 0.0) {
        return;
    }

    const double t_pivot = m_t_min + (m_t_max - m_t_min) * pivot;
    const double new_min = t_pivot - (t_pivot - m_t_min) * scale;
    const double new_max = t_pivot + (m_t_max - t_pivot) * scale;
    adjust_t_to_target(new_min, new_max);
}

void Plot_time_axis::adjust_t_to_target(double target_min, double target_max)
{
    if (!(target_max > target_min)) {
        return;
    }

    const double avail_span = m_t_available_max - m_t_available_min;
    double span = target_max - target_min;
    if (avail_span > 0.0 && span > avail_span) {
        span = avail_span;
    }

    const double center = 0.5 * (target_min + target_max);
    double new_min = center - span * 0.5;
    double new_max = center + span * 0.5;

    if (avail_span > 0.0) {
        if (new_max > m_t_available_max) {
            new_max = m_t_available_max;
            new_min = new_max - span;
        }
        if (new_min < m_t_available_min) {
            new_min = m_t_available_min;
            new_max = new_min + span;
        }
    }

    set_limits_if_changed(new_min, new_max, m_t_available_min, m_t_available_max);
}

bool Plot_time_axis::set_limits_if_changed(
    double t_min,
    double t_max,
    double t_available_min,
    double t_available_max)
{
    const bool changed =
        (std::abs(m_t_min - t_min) > k_axis_eps) ||
        (std::abs(m_t_max - t_max) > k_axis_eps) ||
        (std::abs(m_t_available_min - t_available_min) > k_axis_eps) ||
        (std::abs(m_t_available_max - t_available_max) > k_axis_eps);

    if (!changed) {
        return false;
    }

    m_t_min = t_min;
    m_t_max = t_max;
    m_t_available_min = t_available_min;
    m_t_available_max = t_available_max;
    emit t_limits_changed();
    return true;
}

} // namespace vnm::plot
