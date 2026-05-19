#pragma once

// Shared math for time-axis pan/zoom operations. Plot_widget and
// Plot_time_axis each have a near-identical adjust_t_from_* family of
// functions; they only differ in how they load (t_min, t_max,
// t_available_min, t_available_max) and how they apply the result. The
// helpers below isolate the arithmetic so the two classes stay in sync.

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace vnm::plot::detail {

// Round-to-nearest helper for fp64 -> qint64 conversions on the time axis.
// Saturates instead of overflowing on extreme inputs so a stray Infinity
// from a misuse cannot tear the integer field.
inline qint64 to_qint64_rounded(double value)
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

// Snapshot of the four ns-timestamp fields the adjust_* family reads. The
// caller assembles this from whichever storage it owns (mutex-protected
// data_config_t in Plot_widget, plain members in Plot_time_axis).
struct t_view_snapshot_t
{
    qint64 t_min = 0;
    qint64 t_max = 0;
    qint64 t_available_min = 0;
    qint64 t_available_max = 0;
};

// Translate the view by `diff` pixels of horizontal motion expressed
// against `ref_width`. Commit receives the new (t_min, t_max).
template<typename Commit>
inline void adjust_t_from_mouse_diff_impl(
    const t_view_snapshot_t& view, double ref_width, double diff,
    Commit&& commit)
{
    if (ref_width <= 0.0) {
        return;
    }
    // Pixel deltas are double; the visible span is in int64 nanoseconds.
    // Convert through fp64 once, round when re-attaching to the qint64 axis.
    const qint64 span_ns = view.t_max - view.t_min;
    const qint64 delta_ns = to_qint64_rounded(
        diff * static_cast<double>(span_ns) / ref_width);
    commit(view.t_min - delta_ns, view.t_max - delta_ns);
}

// Preview-bar analogue of adjust_t_from_mouse_diff: the same pixel ratio is
// applied against the (t_available_min, t_available_max) span and the view
// moves in the same direction as the cursor.
template<typename Commit>
inline void adjust_t_from_mouse_diff_on_preview_impl(
    const t_view_snapshot_t& view, double ref_width, double diff,
    Commit&& commit)
{
    if (ref_width <= 0.0) {
        return;
    }
    const qint64 avail_span_ns = view.t_available_max - view.t_available_min;
    const qint64 delta_ns = to_qint64_rounded(
        diff * static_cast<double>(avail_span_ns) / ref_width);
    commit(view.t_min + delta_ns, view.t_max + delta_ns);
}

// Recenter the view on `x_pos` interpreted as a fraction of the preview-bar
// width, keeping the current span.
template<typename Commit>
inline void adjust_t_from_mouse_pos_on_preview_impl(
    const t_view_snapshot_t& view, double ref_width, double x_pos,
    Commit&& commit)
{
    if (ref_width <= 0.0) {
        return;
    }
    const qint64 span_ns = view.t_max - view.t_min;
    const qint64 avail_span_ns = view.t_available_max - view.t_available_min;
    const qint64 new_center_ns = view.t_available_min + to_qint64_rounded(
        (x_pos / ref_width) * static_cast<double>(avail_span_ns));
    const qint64 half_ns = span_ns / 2;
    commit(new_center_ns - half_ns, new_center_ns + (span_ns - half_ns));
}

// Zoom around a normalized pivot in [0, 1] by `scale` (1.0 = no change,
// <1.0 zooms in, >1.0 zooms out).
template<typename Commit>
inline void adjust_t_from_pivot_and_scale_impl(
    const t_view_snapshot_t& view, double pivot, double scale,
    Commit&& commit)
{
    if (scale <= 0.0) {
        return;
    }
    const qint64 span_ns = view.t_max - view.t_min;
    const qint64 t_pivot_ns = view.t_min + to_qint64_rounded(
        pivot * static_cast<double>(span_ns));
    const qint64 left_ns = to_qint64_rounded(
        static_cast<double>(t_pivot_ns - view.t_min) * scale);
    const qint64 right_ns = to_qint64_rounded(
        static_cast<double>(view.t_max - t_pivot_ns) * scale);
    commit(t_pivot_ns - left_ns, t_pivot_ns + right_ns);
}

} // namespace vnm::plot::detail
