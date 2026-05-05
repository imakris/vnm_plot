#include <vnm_plot/qt/plot_time_axis.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace vnm::plot {

namespace {

// Round-to-nearest helper for fp64 -> qint64 conversions on the time axis.
// Saturates instead of overflowing on extreme inputs so a stray Infinity from
// a misuse cannot tear the integer field.
qint64 to_qint64_rounded(double value)
{
    if (!std::isfinite(value)) {
        return 0;
    }
    const double clamped = std::clamp(
        value,
        static_cast<double>(std::numeric_limits<qint64>::min()),
        static_cast<double>(std::numeric_limits<qint64>::max()));
    return static_cast<qint64>(std::llround(clamped));
}

} // anonymous namespace

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
    return m_t_min != k_t_unset && m_t_max != k_t_unset;
}

bool Plot_time_axis::available_initialized() const
{
    return m_t_available_min != k_t_unset && m_t_available_max != k_t_unset;
}

// QML-facing readers. While the axis is still uninitialized, surface 0
// instead of the sentinel mapped through ns_to_ms_for_qml(): QML eagerly
// evaluates property bindings before any user code runs set_t_range, and
// a giant negative integer would feed straight into spans and pixel math
// in PlotIndicator-style consumers.
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
    // the paired bound at sentinel. QML property bindings can only call
    // single-side setters, so without this they could never bring the axis
    // online; downstream consumers (sync_time_axis_state) gate on
    // view_initialized() and ignore half-seeded state. Once both sides are
    // real values the slide-on-overshoot logic below kicks in.
    if (!view_initialized()) {
        if (v == m_t_min) {
            return;
        }
        // If this seed would complete initialization (paired bound is real),
        // enforce ordering so the axis never goes online with an inverted
        // range. Matches set_t_range's silent-refusal contract.
        if (m_t_max != k_t_unset && !(v < m_t_max)) {
            return;
        }
        set_limits_if_changed(v, m_t_max, m_t_available_min, m_t_available_max);
        return;
    }
    qint64 new_min = v;
    qint64 new_max = m_t_max;
    if (v >= m_t_max) {
        const qint64 span = m_t_max - m_t_min;
        if (span <= 0) {
            return;
        }
        new_max = v + span;
    }
    set_limits_if_changed(new_min, new_max, m_t_available_min, m_t_available_max);
}

void Plot_time_axis::set_t_max(qint64 v)
{
    if (!view_initialized()) {
        if (v == m_t_max) {
            return;
        }
        if (m_t_min != k_t_unset && !(v > m_t_min)) {
            return;
        }
        set_limits_if_changed(m_t_min, v, m_t_available_min, m_t_available_max);
        return;
    }
    qint64 new_min = m_t_min;
    qint64 new_max = v;
    if (v <= m_t_min) {
        const qint64 span = m_t_max - m_t_min;
        if (span <= 0) {
            return;
        }
        new_min = v - span;
    }
    set_limits_if_changed(new_min, new_max, m_t_available_min, m_t_available_max);
}

void Plot_time_axis::set_t_available_min(qint64 v)
{
    // First-bound seed: paired bound still sentinel, no range to validate or
    // clamp against. Once the paired bound arrives, delegate to the atomic
    // setter so ordering, view-clamp, and view auto-init from available all
    // live in one place — applying both to a "completing seed" update and to
    // any post-init change. Without delegation a QML write that shrinks the
    // available range below the current view leaves the view dangling
    // outside available, which sync_time_axis_state would propagate.
    if (m_t_available_max == k_t_unset) {
        if (v == m_t_available_min) {
            return;
        }
        set_limits_if_changed(m_t_min, m_t_max, v, m_t_available_max);
        return;
    }
    set_available_t_range(v, m_t_available_max);
}

void Plot_time_axis::set_t_available_max(qint64 v)
{
    if (m_t_available_min == k_t_unset) {
        if (v == m_t_available_max) {
            return;
        }
        set_limits_if_changed(m_t_min, m_t_max, m_t_available_min, v);
        return;
    }
    set_available_t_range(m_t_available_min, v);
}

void Plot_time_axis::set_t_range(qint64 t_min_ns, qint64 t_max_ns)
{
    if (!(t_max_ns > t_min_ns)) {
        return;
    }
    set_limits_if_changed(t_min_ns, t_max_ns, m_t_available_min, m_t_available_max);
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
    const bool view_unset = (m_t_min == k_t_unset && m_t_max == k_t_unset);
    if (view_unset) {
        // No view at all; adopt the entire available range as the initial
        // view. Without this, the clamp logic below would read the sentinel
        // as a real bound and collapse new_t_min/new_t_max onto t_available_min.
        new_t_min = t_available_min_ns;
        new_t_max = t_available_max_ns;
    }
    else if (!view_initialized()) {
        // Half-seeded view (one bound real, one sentinel). Preserve as-is:
        // adopting available would clobber the user's half-seeded value, and
        // running the clamp logic on a sentinel bound would produce garbage.
        // The next set_t_min / set_t_max seed completes the view, at which
        // point any subsequent set_available_t_range hits the clamp branch.
        new_t_min = m_t_min;
        new_t_max = m_t_max;
    }
    else {
        new_t_min = m_t_min;
        new_t_max = m_t_max;

        const qint64 span = t_available_max_ns - t_available_min_ns;
        const qint64 cur_span = new_t_max - new_t_min;
        if (cur_span > span) {
            new_t_min = t_available_min_ns;
            new_t_max = t_available_max_ns;
        }
        else {
            if (new_t_min < t_available_min_ns) {
                new_t_min = t_available_min_ns;
                new_t_max = t_available_min_ns + cur_span;
            }
            if (new_t_max > t_available_max_ns) {
                new_t_max = t_available_max_ns;
                new_t_min = t_available_max_ns - cur_span;
            }
        }
    }

    set_limits_if_changed(new_t_min, new_t_max, t_available_min_ns, t_available_max_ns);
}

void Plot_time_axis::adjust_t_from_mouse_diff(double ref_width, double diff)
{
    if (ref_width <= 0.0 || !view_initialized()) {
        return;
    }

    // Pixel deltas are double, but the visible span is in int64 nanoseconds;
    // convert through fp64 once, round when re-attaching to the qint64 axis.
    const qint64 span_ns = m_t_max - m_t_min;
    const qint64 delta_ns = to_qint64_rounded(
        diff * static_cast<double>(span_ns) / ref_width);
    adjust_t_to_target(m_t_min - delta_ns, m_t_max - delta_ns);
}

void Plot_time_axis::adjust_t_from_mouse_diff_on_preview(double ref_width, double diff)
{
    if (ref_width <= 0.0 || !view_initialized() || !available_initialized()) {
        return;
    }

    const qint64 avail_span_ns = m_t_available_max - m_t_available_min;
    const qint64 delta_ns = to_qint64_rounded(
        diff * static_cast<double>(avail_span_ns) / ref_width);
    adjust_t_to_target(m_t_min + delta_ns, m_t_max + delta_ns);
}

void Plot_time_axis::adjust_t_from_mouse_pos_on_preview(double ref_width, double x_pos)
{
    if (ref_width <= 0.0 || !view_initialized() || !available_initialized()) {
        return;
    }

    const qint64 span_ns = m_t_max - m_t_min;
    const qint64 avail_span_ns = m_t_available_max - m_t_available_min;
    const double rel = x_pos / ref_width;
    const qint64 new_center_ns = m_t_available_min
        + to_qint64_rounded(rel * static_cast<double>(avail_span_ns));
    const qint64 half_ns = span_ns / 2;
    adjust_t_to_target(new_center_ns - half_ns, new_center_ns + (span_ns - half_ns));
}

void Plot_time_axis::adjust_t_from_pivot_and_scale(double pivot, double scale)
{
    if (scale <= 0.0 || !view_initialized()) {
        return;
    }

    const qint64 span_ns = m_t_max - m_t_min;
    const qint64 t_pivot_ns = m_t_min
        + to_qint64_rounded(pivot * static_cast<double>(span_ns));
    const qint64 new_min_ns = t_pivot_ns - to_qint64_rounded(
        static_cast<double>(t_pivot_ns - m_t_min) * scale);
    const qint64 new_max_ns = t_pivot_ns + to_qint64_rounded(
        static_cast<double>(m_t_max - t_pivot_ns) * scale);
    adjust_t_to_target(new_min_ns, new_max_ns);
}

void Plot_time_axis::adjust_t_to_target(qint64 target_min_ns, qint64 target_max_ns)
{
    if (!(target_max_ns > target_min_ns)) {
        return;
    }

    const qint64 avail_span_ns = m_t_available_max - m_t_available_min;
    qint64 span_ns = target_max_ns - target_min_ns;
    if (avail_span_ns > 0 && span_ns > avail_span_ns) {
        span_ns = avail_span_ns;
    }

    const qint64 half_ns = span_ns / 2;
    const qint64 center_ns = target_min_ns + (target_max_ns - target_min_ns) / 2;
    qint64 new_min_ns = center_ns - half_ns;
    qint64 new_max_ns = new_min_ns + span_ns;

    if (avail_span_ns > 0) {
        if (new_max_ns > m_t_available_max) {
            new_max_ns = m_t_available_max;
            new_min_ns = new_max_ns - span_ns;
        }
        if (new_min_ns < m_t_available_min) {
            new_min_ns = m_t_available_min;
            new_max_ns = new_min_ns + span_ns;
        }
    }

    set_limits_if_changed(new_min_ns, new_max_ns, m_t_available_min, m_t_available_max);
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
    qint64 t_available_max_ns)
{
    const bool changed =
        m_t_min != t_min_ns ||
        m_t_max != t_max_ns ||
        m_t_available_min != t_available_min_ns ||
        m_t_available_max != t_available_max_ns;

    if (!changed) {
        return false;
    }

    m_t_min = t_min_ns;
    m_t_max = t_max_ns;
    m_t_available_min = t_available_min_ns;
    m_t_available_max = t_available_max_ns;
    emit t_limits_changed();
    return true;
}

} // namespace vnm::plot
