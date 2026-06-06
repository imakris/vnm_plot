#pragma once

// Shared math for time-axis pan/zoom operations. Plot_widget and
// Plot_time_axis each have a near-identical adjust_t_from_* family of
// functions; they only differ in how they load (t_min, t_max,
// t_available_min, t_available_max) and how they apply the result. The
// helpers below isolate the arithmetic so the two classes stay in sync.

#include <vnm_plot/core/time_units.h>

#include <QtGlobal>

#include <cmath>
#include <cstdint>

namespace vnm::plot::detail {

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

    const auto span_ns = positive_span_ns(view.t_min, view.t_max);
    if (!span_ns) {
        return;
    }

    const long double fraction =
        std::abs(static_cast<long double>(diff) / static_cast<long double>(ref_width));
    const std::uint64_t duration_ns = scaled_duration_ns(*span_ns, fraction);
    const auto direction = (diff >= 0.0)
        ? Time_translation_direction::BACKWARD
        : Time_translation_direction::FORWARD;
    const auto shifted = translate_time_range_by_duration_ns(
        time_range_t{view.t_min, view.t_max},
        duration_ns,
        direction);
    if (!shifted) {
        return;
    }
    commit(shifted->min_ns, shifted->max_ns);
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

    const auto avail_span_ns = positive_span_ns(
        view.t_available_min,
        view.t_available_max);
    if (!avail_span_ns) {
        return;
    }

    const long double fraction =
        std::abs(static_cast<long double>(diff) / static_cast<long double>(ref_width));
    const std::uint64_t duration_ns = scaled_duration_ns(*avail_span_ns, fraction);
    const auto direction = (diff >= 0.0)
        ? Time_translation_direction::FORWARD
        : Time_translation_direction::BACKWARD;
    const auto shifted = translate_time_range_by_duration_ns(
        time_range_t{view.t_min, view.t_max},
        duration_ns,
        direction);
    if (!shifted) {
        return;
    }
    commit(shifted->min_ns, shifted->max_ns);
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

    const auto span_ns = positive_span_ns(view.t_min, view.t_max);
    if (!span_ns) {
        return;
    }

    const auto new_center_ns = time_at_fraction_ns(
        time_range_t{view.t_available_min, view.t_available_max},
        static_cast<long double>(x_pos) / static_cast<long double>(ref_width));
    if (!new_center_ns) {
        return;
    }
    const time_range_t target = centered_time_range_ns(*new_center_ns, *span_ns);
    commit(target.min_ns, target.max_ns);
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

    const auto span_ns = positive_span_ns(view.t_min, view.t_max);
    if (!span_ns) {
        return;
    }

    const auto t_pivot_ns = time_at_fraction_ns(
        time_range_t{view.t_min, view.t_max},
        static_cast<long double>(pivot));
    if (!t_pivot_ns) {
        return;
    }

    const auto left_span_ns = positive_span_ns(view.t_min, *t_pivot_ns);
    const auto right_span_ns = positive_span_ns(*t_pivot_ns, view.t_max);
    const std::uint64_t left_ns = left_span_ns
        ? scaled_duration_ns(*left_span_ns, static_cast<long double>(scale))
        : 0;
    const std::uint64_t right_ns = right_span_ns
        ? scaled_duration_ns(*right_span_ns, static_cast<long double>(scale))
        : 0;
    const time_range_t target = time_range_around_pivot_ns(
        *t_pivot_ns,
        left_ns,
        right_ns);
    commit(target.min_ns, target.max_ns);
}

} // namespace vnm::plot::detail
