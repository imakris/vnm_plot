#include <vnm_plot/qt/plot_time_axis.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace vnm::plot {

namespace {
constexpr double k_axis_eps = 1e-12;
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

bool Plot_time_axis::sync_vbar_width() const
{
    return m_sync_vbar_width;
}

void Plot_time_axis::set_sync_vbar_width(bool v)
{
    if (m_sync_vbar_width == v) {
        return;
    }

    m_sync_vbar_width = v;
    if (!m_sync_vbar_width) {
        m_vbar_width_by_owner.clear();
        if (m_shared_vbar_width_px != 0.0) {
            m_shared_vbar_width_px = 0.0;
            emit shared_vbar_width_changed(m_shared_vbar_width_px);
        }
    }
    emit sync_vbar_width_changed();
}

double Plot_time_axis::shared_vbar_width_px() const
{
    return m_shared_vbar_width_px;
}

void Plot_time_axis::update_shared_vbar_width(const QObject* owner, double width_px)
{
    if (!m_sync_vbar_width || !owner) {
        return;
    }
    if (!std::isfinite(width_px) || width_px <= 0.0) {
        return;
    }

    m_vbar_width_by_owner[owner] = width_px;

    double max_width = 0.0;
    for (const auto& entry : m_vbar_width_by_owner) {
        max_width = std::max(max_width, entry.second);
    }

    if (std::abs(max_width - m_shared_vbar_width_px) > k_axis_eps) {
        m_shared_vbar_width_px = max_width;
        emit shared_vbar_width_changed(m_shared_vbar_width_px);
    }
}

void Plot_time_axis::clear_shared_vbar_width(const QObject* owner)
{
    if (!owner) {
        return;
    }

    const auto erased = m_vbar_width_by_owner.erase(owner);
    if (erased == 0) {
        return;
    }

    double max_width = 0.0;
    for (const auto& entry : m_vbar_width_by_owner) {
        max_width = std::max(max_width, entry.second);
    }

    if (std::abs(max_width - m_shared_vbar_width_px) > k_axis_eps) {
        m_shared_vbar_width_px = max_width;
        emit shared_vbar_width_changed(m_shared_vbar_width_px);
    }
}

void Plot_time_axis::set_t_min(double v)
{
    double new_min = v;
    double new_max = m_t_max;
    if (v >= m_t_max) {
        const double span = m_t_max - m_t_min;
        if (!(span > 0.0)) {
            return;
        }
        new_max = v + span;
    }
    set_limits_if_changed(new_min, new_max, m_t_available_min, m_t_available_max);
}

void Plot_time_axis::set_t_max(double v)
{
    double new_min = m_t_min;
    double new_max = v;
    if (v <= m_t_min) {
        const double span = m_t_max - m_t_min;
        if (!(span > 0.0)) {
            return;
        }
        new_min = v - span;
    }
    set_limits_if_changed(new_min, new_max, m_t_available_min, m_t_available_max);
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

void Plot_time_axis::set_indicator_state(QObject* owner, bool active, double t)
{
    set_indicator_state(owner, active, t, std::numeric_limits<double>::quiet_NaN());
}

void Plot_time_axis::set_indicator_state(QObject* owner, bool active, double t, double x_norm)
{
    if (!owner) {
        return;
    }

    bool changed = false;
    if (active) {
        bool owner_changed = false;
        if (m_indicator_owner != owner) {
            m_indicator_owner = owner;
            owner_changed = true;
            changed = true;
        }
        if (!m_indicator_active) {
            m_indicator_active = true;
            changed = true;
        }
        if (std::isfinite(t) && std::abs(m_indicator_t - t) > k_axis_eps) {
            m_indicator_t = t;
            changed = true;
        }
        if (std::isfinite(x_norm)) {
            const double clamped_x_norm = std::clamp(x_norm, 0.0, 1.0);
            if (!m_indicator_x_norm_valid || std::abs(m_indicator_x_norm - clamped_x_norm) > k_axis_eps) {
                m_indicator_x_norm = clamped_x_norm;
                changed = true;
            }
            if (!m_indicator_x_norm_valid) {
                m_indicator_x_norm_valid = true;
                changed = true;
            }
        }
        else
        if (owner_changed && m_indicator_x_norm_valid) {
            m_indicator_x_norm_valid = false;
            changed = true;
        }
    }
    else {
        if (m_indicator_owner == owner && (m_indicator_active || m_indicator_x_norm_valid)) {
            m_indicator_owner = nullptr;
            m_indicator_active = false;
            m_indicator_x_norm_valid = false;
            changed = true;
        }
    }

    if (changed) {
        emit indicator_state_changed();
    }
}

bool Plot_time_axis::indicator_active() const
{
    return m_indicator_active;
}

double Plot_time_axis::indicator_t() const
{
    return m_indicator_t;
}

bool Plot_time_axis::indicator_x_norm_valid() const
{
    return m_indicator_x_norm_valid;
}

double Plot_time_axis::indicator_x_norm() const
{
    return m_indicator_x_norm;
}

bool Plot_time_axis::indicator_owned_by(QObject* owner) const
{
    if (!owner) {
        return false;
    }
    return m_indicator_owner == owner;
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
