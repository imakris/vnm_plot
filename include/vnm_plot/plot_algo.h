#pragma once

// VNM Plot Library - Algorithm Utilities
// Re-exports core algorithms and adds wrapper-specific helpers.
// Pure C++ with no framework dependencies.

#include <vnm_plot/core/algo.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace vnm::plot::algo {

// -----------------------------------------------------------------------------
// Re-exported from core
// -----------------------------------------------------------------------------
using core::algo::format_axis_fixed_or_int;
using core::algo::circular_index;
using core::algo::get_shift;
using core::algo::build_time_steps_covering;
using core::algo::find_time_step_start_index;
using core::algo::min_v_span_for;
using core::algo::compute_lod_scales;
using core::algo::lower_bound_timestamp;
using core::algo::upper_bound_timestamp;
using core::algo::choose_lod_level;

// -----------------------------------------------------------------------------
// Decimal Analysis
// -----------------------------------------------------------------------------

// Check if any value has non-zero fractional part at the given precision.
inline bool any_fractional_at_precision(const std::vector<double>& values, int digits)
{
    if (digits <= 0) return false;

    const double scale = std::pow(10.0, double(digits));
    const double eps = 0.5 / scale;

    for (double v : values) {
        double r = std::round(v * scale) / scale;
        if (std::abs(r - std::round(r)) > eps) {
            return true;
        }
    }
    return false;
}

// Returns true if all values' last decimal digit is zero at given precision.
inline bool trailing_zero_decimal_for_all(const std::vector<double>& values, int digits)
{
    if (digits <= 0) return false;

    const double scale = std::pow(10.0, double(digits));
    for (double v : values) {
        const std::int64_t q = std::llround(std::abs(v) * scale);
        if ((q % 10) != 0) {
            return false;
        }
    }
    return true;
}

// Reduce digits while last decimal place is zero for all values.
inline int trim_trailing_zero_decimals(const std::vector<double>& values, int digits)
{
    int d = std::max(0, digits);
    while (d > 0 && trailing_zero_decimal_for_all(values, d)) {
        --d;
    }
    return d;
}

// -----------------------------------------------------------------------------
// Binary Search for Time Series (concept-based convenience)
// -----------------------------------------------------------------------------

// Concept: type has a .timestamp field convertible to double
template<typename T>
concept HasTimestamp = requires(T v) {
    { v.timestamp } -> std::convertible_to<double>;
};

// Find the sample at or just before time t using binary search.
// This is a convenience wrapper for types with a .timestamp member.
template<HasTimestamp T>
inline T* binary_search_time(T* p_begin, T* p_end, double t)
{
    T* bound = std::upper_bound(
        p_begin, p_end, t,
        [](double val, const T& e) { return val < e.timestamp; }
    );
    return (bound == p_begin) ? p_begin : (bound - 1);
}

} // namespace vnm::plot::algo
