#include <vnm_plot/qt/plot_time_axis.h>

#include "t_axis_adjust.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace vnm::plot {

Plot_time_axis::Plot_time_axis(QObject* parent)
    : QObject(parent)
{}

qint64 Plot_time_axis::t_min() const
{
    return m_t_min;
}

qint64 Plot_time_axis::t_max() const
{
    return m_t_max;
}

qint64 Plot_time_axis::t_available_min() const
{
    return m_t_available_min;
}

qint64 Plot_time_axis::t_available_max() const
{
    return m_t_available_max;
}

bool Plot_time_axis::view_initialized() const
{
    return m_t_min_initialized && m_t_max_initialized;
}

bool Plot_time_axis::available_initialized() const
{
    return m_t_available_min_initialized && m_t_available_max_initialized;
}

bool Plot_time_axis::any_view_bound_initialized() const
{
    return m_t_min_initialized || m_t_max_initialized;
}

bool Plot_time_axis::any_available_bound_initialized() const
{
    return m_t_available_min_initialized || m_t_available_max_initialized;
}

// QML-facing readers surface 0 until both bounds of the relevant range have
// been initialized. QML eagerly evaluates property bindings before user code
// runs set_t_range, and default values would feed straight into spans and
// pixel math in PlotIndicator-style consumers.
qint64 Plot_time_axis::t_min_qml_ms() const
{
    return view_initialized() ? ns_to_ms_for_qml(m_t_min) : 0;
}

qint64 Plot_time_axis::t_max_qml_ms() const
{
    return view_initialized() ? ns_to_ms_for_qml(m_t_max) : 0;
}

qint64 Plot_time_axis::t_available_min_qml_ms() const
{
    return available_initialized() ? ns_to_ms_for_qml(m_t_available_min) : 0;
}

qint64 Plot_time_axis::t_available_max_qml_ms() const
{
    return available_initialized() ? ns_to_ms_for_qml(m_t_available_max) : 0;
}

void Plot_time_axis::set_t_min_qml_ms(qint64 v_ms)
{
    set_t_min(ms_for_qml_to_ns(v_ms));
}

void Plot_time_axis::set_t_max_qml_ms(qint64 v_ms)
{
    set_t_max(ms_for_qml_to_ns(v_ms));
}

void Plot_time_axis::set_t_available_min_qml_ms(qint64 v_ms)
{
    set_t_available_min(ms_for_qml_to_ns(v_ms));
}

void Plot_time_axis::set_t_available_max_qml_ms(qint64 v_ms)
{
    set_t_available_max(ms_for_qml_to_ns(v_ms));
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

    if (std::abs(max_width - m_shared_vbar_width_px) > 1e-12) {
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

    if (std::abs(max_width - m_shared_vbar_width_px) > 1e-12) {
        m_shared_vbar_width_px = max_width;
        emit shared_vbar_width_changed(m_shared_vbar_width_px);
    }
}

void Plot_time_axis::set_t_min(qint64 v)
{
    // While uninitialized, treat this as a seed: store the value but leave
    // the paired bound uninitialized. QML property bindings can only call
    // single-side setters, so without this they could never bring the axis
    // online; downstream consumers (sync_time_axis_state) gate on
    // view_initialized() and ignore half-seeded state. Once both sides are
    // real values the slide-on-overshoot logic below kicks in.
    if (!view_initialized()) {
        if (m_t_min_initialized && v == m_t_min) {
            return;
        }
        // If this seed would complete initialization (paired bound is real),
        // enforce ordering so the axis never goes online with an inverted
        // range. Matches set_t_range's silent-refusal contract.
        if (m_t_max_initialized && !(v < m_t_max)) {
            return;
        }
        set_limits_if_changed(
            v,
            m_t_max,
            m_t_available_min,
            m_t_available_max,
            true,
            m_t_max_initialized,
            m_t_available_min_initialized,
            m_t_available_max_initialized);
        return;
    }
    qint64 new_min = v;
    qint64 new_max = m_t_max;
    if (v >= m_t_max) {
        const auto span_ns = positive_span_ns(m_t_min, m_t_max);
        if (!span_ns) {
            return;
        }
        new_max = saturating_add_duration_ns(v, *span_ns);
        if (positive_span_ns(new_min, new_max) != span_ns) {
            return;
        }
    }
    set_limits_if_changed(
        new_min,
        new_max,
        m_t_available_min,
        m_t_available_max,
        true,
        true,
        m_t_available_min_initialized,
        m_t_available_max_initialized);
}

void Plot_time_axis::set_t_max(qint64 v)
{
    if (!view_initialized()) {
        if (m_t_max_initialized && v == m_t_max) {
            return;
        }
        if (m_t_min_initialized && !(v > m_t_min)) {
            return;
        }
        set_limits_if_changed(
            m_t_min,
            v,
            m_t_available_min,
            m_t_available_max,
            m_t_min_initialized,
            true,
            m_t_available_min_initialized,
            m_t_available_max_initialized);
        return;
    }
    qint64 new_min = m_t_min;
    qint64 new_max = v;
    if (v <= m_t_min) {
        const auto span_ns = positive_span_ns(m_t_min, m_t_max);
        if (!span_ns) {
            return;
        }
        new_min = saturating_sub_duration_ns(v, *span_ns);
        if (positive_span_ns(new_min, new_max) != span_ns) {
            return;
        }
    }
    set_limits_if_changed(
        new_min,
        new_max,
        m_t_available_min,
        m_t_available_max,
        true,
        true,
        m_t_available_min_initialized,
        m_t_available_max_initialized);
}

void Plot_time_axis::set_t_available_min(qint64 v)
{
    // First-bound seed: the paired bound is still uninitialized, so there is
    // no range to validate or clamp against. Once the paired bound arrives,
    // delegate to the atomic setter so ordering, view-clamp, and view auto-init
    // from available all live in one place.
    if (!m_t_available_max_initialized) {
        if (m_t_available_min_initialized && v == m_t_available_min) {
            return;
        }
        set_limits_if_changed(
            m_t_min,
            m_t_max,
            v,
            m_t_available_max,
            m_t_min_initialized,
            m_t_max_initialized,
            true,
            false);
        return;
    }
    set_available_t_range(v, m_t_available_max);
}

void Plot_time_axis::set_t_available_max(qint64 v)
{
    if (!m_t_available_min_initialized) {
        if (m_t_available_max_initialized && v == m_t_available_max) {
            return;
        }
        set_limits_if_changed(
            m_t_min,
            m_t_max,
            m_t_available_min,
            v,
            m_t_min_initialized,
            m_t_max_initialized,
            false,
            true);
        return;
    }
    set_available_t_range(m_t_available_min, v);
}

void Plot_time_axis::set_t_range(qint64 t_min_ns, qint64 t_max_ns)
{
    if (!(t_max_ns > t_min_ns)) {
        return;
    }
    set_limits_if_changed(
        t_min_ns,
        t_max_ns,
        m_t_available_min,
        m_t_available_max,
        true,
        true,
        m_t_available_min_initialized,
        m_t_available_max_initialized);
}

void Plot_time_axis::set_t_range_qml_ms(qint64 t_min_ms, qint64 t_max_ms)
{
    set_t_range(ms_for_qml_to_ns(t_min_ms), ms_for_qml_to_ns(t_max_ms));
}

void Plot_time_axis::set_available_t_range_qml_ms(qint64 t_available_min_ms, qint64 t_available_max_ms)
{
    set_available_t_range(
        ms_for_qml_to_ns(t_available_min_ms),
        ms_for_qml_to_ns(t_available_max_ms));
}

void Plot_time_axis::set_available_t_range(qint64 t_available_min_ns, qint64 t_available_max_ns)
{
    if (!(t_available_max_ns > t_available_min_ns)) {
        return;
    }

    qint64 new_t_min;
    qint64 new_t_max;
    bool new_t_min_initialized = m_t_min_initialized;
    bool new_t_max_initialized = m_t_max_initialized;
    const bool view_unset = !m_t_min_initialized && !m_t_max_initialized;
    if (view_unset) {
        // No view at all; adopt the entire available range as the initial
        // view so a QML caller that configures available bounds first still
        // gives subsequent interactions a real range to operate on.
        new_t_min = t_available_min_ns;
        new_t_max = t_available_max_ns;
        new_t_min_initialized = true;
        new_t_max_initialized = true;
    }
    else if (!view_initialized()) {
        // Half-seeded view (one bound real, one unset). Preserve as-is:
        // adopting available would clobber the user's half-seeded value, and
        // running the clamp logic before both view bounds exist would produce
        // a range the caller did not ask for.
        // The next set_t_min / set_t_max seed completes the view, at which
        // point any subsequent set_available_t_range hits the clamp branch.
        new_t_min = m_t_min;
        new_t_max = m_t_max;
    }
    else {
        new_t_min = m_t_min;
        new_t_max = m_t_max;

        const auto clamped = clamp_time_range_to_available_ns(
            time_range_t{new_t_min, new_t_max},
            time_range_t{t_available_min_ns, t_available_max_ns});
        if (clamped) {
            new_t_min = clamped->min_ns;
            new_t_max = clamped->max_ns;
        }
        else {
            new_t_min = t_available_min_ns;
            new_t_max = t_available_max_ns;
        }
    }

    set_limits_if_changed(
        new_t_min,
        new_t_max,
        t_available_min_ns,
        t_available_max_ns,
        new_t_min_initialized,
        new_t_max_initialized,
        true,
        true);
}

namespace {

detail::t_view_snapshot_t time_axis_view_snapshot(const Plot_time_axis& a)
{
    return {a.t_min(), a.t_max(), a.t_available_min(), a.t_available_max()};
}

} // anonymous namespace

void Plot_time_axis::adjust_t_from_mouse_diff(double ref_width, double diff)
{
    if (!view_initialized()) {
        return;
    }
    detail::adjust_t_from_mouse_diff_impl(
        time_axis_view_snapshot(*this), ref_width, diff,
        [this](qint64 mn, qint64 mx) { adjust_t_to_target(mn, mx); });
}

void Plot_time_axis::adjust_t_from_mouse_diff_on_preview(double ref_width, double diff)
{
    if (!view_initialized() || !available_initialized()) {
        return;
    }
    detail::adjust_t_from_mouse_diff_on_preview_impl(
        time_axis_view_snapshot(*this), ref_width, diff,
        [this](qint64 mn, qint64 mx) { adjust_t_to_target(mn, mx); });
}

void Plot_time_axis::adjust_t_from_mouse_pos_on_preview(double ref_width, double x_pos)
{
    if (!view_initialized() || !available_initialized()) {
        return;
    }
    detail::adjust_t_from_mouse_pos_on_preview_impl(
        time_axis_view_snapshot(*this), ref_width, x_pos,
        [this](qint64 mn, qint64 mx) { adjust_t_to_target(mn, mx); });
}

void Plot_time_axis::adjust_t_from_pivot_and_scale(double pivot, double scale)
{
    if (!view_initialized()) {
        return;
    }
    detail::adjust_t_from_pivot_and_scale_impl(
        time_axis_view_snapshot(*this), pivot, scale,
        [this](qint64 mn, qint64 mx) { adjust_t_to_target(mn, mx); });
}

void Plot_time_axis::adjust_t_to_target(qint64 target_min_ns, qint64 target_max_ns)
{
    if (!(target_max_ns > target_min_ns)) {
        return;
    }

    time_range_t target{target_min_ns, target_max_ns};
    if (available_initialized()) {
        const auto clamped = clamp_time_range_to_available_ns(
            target,
            time_range_t{m_t_available_min, m_t_available_max});
        if (!clamped) {
            return;
        }
        target = *clamped;
    }

    set_limits_if_changed(
        target.min_ns,
        target.max_ns,
        m_t_available_min,
        m_t_available_max,
        true,
        true,
        m_t_available_min_initialized,
        m_t_available_max_initialized);
}

void Plot_time_axis::set_indicator_state(QObject* owner, bool active, qint64 t_ms)
{
    set_indicator_state(owner, active, t_ms, std::numeric_limits<double>::quiet_NaN());
}

void Plot_time_axis::set_indicator_state(QObject* owner, bool active, qint64 t_ms, double x_norm)
{
    if (!owner) {
        return;
    }

    // QML callers pass t_ms in milliseconds-since-epoch; internal storage
    // is nanoseconds.
    const qint64 t_ns = ms_for_qml_to_ns(t_ms);

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
        if (m_indicator_t != t_ns) {
            m_indicator_t = t_ns;
            changed = true;
        }
        if (std::isfinite(x_norm)) {
            const double clamped_x_norm = std::clamp(x_norm, 0.0, 1.0);
            if (!m_indicator_x_norm_valid || std::abs(m_indicator_x_norm - clamped_x_norm) > 1e-12) {
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

qint64 Plot_time_axis::indicator_t() const
{
    return m_indicator_t;
}

qint64 Plot_time_axis::indicator_t_qml_ms() const
{
    return ns_to_ms_for_qml(m_indicator_t);
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
    qint64 t_min_ns,
    qint64 t_max_ns,
    qint64 t_available_min_ns,
    qint64 t_available_max_ns,
    bool t_min_initialized,
    bool t_max_initialized,
    bool t_available_min_initialized,
    bool t_available_max_initialized)
{
    const bool changed =
        m_t_min != t_min_ns ||
        m_t_max != t_max_ns ||
        m_t_available_min != t_available_min_ns ||
        m_t_available_max != t_available_max_ns ||
        m_t_min_initialized != t_min_initialized ||
        m_t_max_initialized != t_max_initialized ||
        m_t_available_min_initialized != t_available_min_initialized ||
        m_t_available_max_initialized != t_available_max_initialized;

    if (!changed) {
        return false;
    }

    m_t_min = t_min_ns;
    m_t_max = t_max_ns;
    m_t_available_min = t_available_min_ns;
    m_t_available_max = t_available_max_ns;
    m_t_min_initialized = t_min_initialized;
    m_t_max_initialized = t_max_initialized;
    m_t_available_min_initialized = t_available_min_initialized;
    m_t_available_max_initialized = t_available_max_initialized;
    emit t_limits_changed();
    return true;
}

} // namespace vnm::plot
