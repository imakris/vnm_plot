#pragma once

// VNM Plot Library - Algorithm Utilities
// Small, header-only helpers for axis calculations and formatting.
// Pure C++ with no framework dependencies.
//
// Public API (vnm::plot):
//   - format_axis_fixed_or_int: Format numeric values for axis labels
//
// Internal API (vnm::plot::detail):
//   - Grid/time step calculation helpers
//   - Binary search for timestamps
//   - LOD selection algorithms

#include "types.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace vnm::plot {

// =============================================================================
// Public API
// =============================================================================

// Format a numeric value with either integer or fixed precision.
// Used for axis label formatting. Can be used in custom format_timestamp callbacks.
inline std::string format_axis_fixed_or_int(double v, int digits)
{
    if (digits <= 0) {
        const std::int64_t iv = std::llround(v);
        if (iv == 0) {
            return "0";
        }
        return std::to_string(iv);
    }

    const double scale = std::pow(10.0, double(digits));
    double r = std::round(v * scale) / scale;
    const double eps = 0.5 / scale;

    if (std::abs(r) < eps) {
        r = 0.0;
    }

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(digits) << r;
    std::string s = oss.str();

    // Collapse "-0.0..." to "0.0..."
    if (s.size() >= 3 && s[0] == '-' && s[1] == '0' && s[2] == '.' && std::abs(r) < eps) {
        s.erase(0, 1);
    }

    return s;
}

// =============================================================================
// Internal Implementation Details
// =============================================================================

namespace detail {

// -----------------------------------------------------------------------------
// Decimal Analysis
// -----------------------------------------------------------------------------

// Check if any value has non-zero fractional part at the given precision.
inline bool any_fractional_at_precision(const std::vector<double>& values, int digits)
{
    if (digits <= 0) {
        return false;
    }

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
    if (digits <= 0) {
        return false;
    }

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
// Circular Indexing
// -----------------------------------------------------------------------------

// Circular index into a container (wrap-around, supports negatives).
template<typename ContainerT>
inline std::size_t circular_index(const ContainerT& c, int index)
{
    if (c.empty()) {
        return 0;
    }

    const auto size_as_int = static_cast<int>(c.size());
    int remainder = index % size_as_int;
    if (remainder < 0) {
        remainder += size_as_int;
    }
    return static_cast<std::size_t>(remainder);
}

// -----------------------------------------------------------------------------
// Grid Alignment
// -----------------------------------------------------------------------------

// Compute phase shift for grid lines to align nicely with a minimum value.
inline double get_shift(double section_size, double minval)
{
    double a = std::floor(std::log10(std::fabs(section_size)));
    double m = 1.0;
    if (a < 0.0) {
        m = std::pow(10.0, -a);
    }

    double ret = std::fmod(section_size * m - minval * m + section_size * m, section_size * m) / m;
    if (ret < 0) {
        ret += section_size;
    }
    return ret;
}

// -----------------------------------------------------------------------------
// Time Axis Steps
// -----------------------------------------------------------------------------

// Build ascending list of time steps (in seconds) covering max_span.
inline std::vector<double> build_time_steps_covering(double max_span)
{
    std::vector<double> steps;
    steps.reserve(64);

    // Sub-second: 1-2-5 from 1ms up to 0.5s
    static const double subsec[] = {0.001, 0.005, 0.01, 0.05, 0.1, 0.5};
    steps.insert(steps.end(), std::begin(subsec), std::end(subsec));

    // Exact sequence from seconds up to 2 days
    static const double exact[] = {
        1, 2, 10, 30,             // seconds
        60, 300, 900, 1800,       // minutes: 1m, 5m, 15m, 30m
        3600, 7200, 21600, 43200, // hours: 1h, 2h, 6h, 12h
        86400                     // 1 day
    };
    steps.insert(steps.end(), std::begin(exact), std::end(exact));

    // Generic 1-2-5 beyond 2 days
    double s = 172800.0; // 2 days
    const double limit = std::max(max_span * 2.0, s * 2.0);
    int cycle = 0;
    while (s < limit && s < 1e12) {
        const double mult = (cycle % 3 == 0) ? 2.5 : 2.0;
        s *= mult;
        steps.push_back(s);
        ++cycle;
    }
    return steps;
}

// Find index of largest step <= t_range.
inline int find_time_step_start_index(const std::vector<double>& steps, double t_range)
{
    if (steps.empty()) {
        return -1;
    }

    int idx = 0;
    while (idx + 1 < static_cast<int>(steps.size()) && steps[idx + 1] <= t_range) {
        ++idx;
    }
    return idx;
}

// -----------------------------------------------------------------------------
// Range Utilities
// -----------------------------------------------------------------------------

// Minimal usable span for the vertical axis (prevents float precision collapse).
inline float min_v_span_for(float a, float b)
{
    float mag = std::max(std::abs(a), std::abs(b));
    float ulps = 64.0f * std::numeric_limits<float>::epsilon() * std::max(1.0f, mag);
    float floor_abs = 1e-6f * std::max(1.0f, mag);
    return std::max(ulps, floor_abs);
}

// -----------------------------------------------------------------------------
// LOD Scale Computation
// -----------------------------------------------------------------------------

// Compute LOD scales vector from a data source.
// Works with any type that has lod_levels() and lod_scale(level) methods.
// Each scale represents the subsampling factor at that LOD level.
//
// Note: Scales are clamped to >= 1 to prevent division-by-zero and other
// edge cases downstream. A scale of 0 is semantically invalid (would mean
// "zero samples per coarse sample"). Data sources should always return >= 1.
template<typename DataSourceT>
std::vector<std::size_t> compute_lod_scales(const DataSourceT& data_source)
{
    std::vector<std::size_t> scales;
    const std::size_t levels = data_source.lod_levels();
    scales.reserve(levels);
    for (std::size_t lvl = 0; lvl < levels; ++lvl) {
        const std::size_t scale = std::max<std::size_t>(1, data_source.lod_scale(lvl));
        scales.push_back(scale);
    }
    return scales;
}

// -----------------------------------------------------------------------------
// Binary Search for Timestamps
// -----------------------------------------------------------------------------
// These functions perform binary search on a contiguous array of samples,
// using a callback to extract timestamps. Assumes ascending timestamp order.

// Returns index of first sample with timestamp >= t (lower_bound semantics).
template<typename GetTimestampFn>
std::size_t lower_bound_timestamp(
    const void* data,
    std::size_t count,
    std::size_t stride,
    GetTimestampFn&& get_timestamp,
    double t)
{
    if (data == nullptr || count == 0) {
        return 0;
    }

    const auto* base = static_cast<const std::uint8_t*>(data);
    std::size_t lo = 0;
    std::size_t hi = count;

    while (lo < hi) {
        std::size_t mid = lo + (hi - lo) / 2;
        const void* sample = base + mid * stride;
        if (get_timestamp(sample) < t) {
            lo = mid + 1;
        }
        else {
            hi = mid;
        }
    }
    return lo;
}

// Overload for data_snapshot_t (supports segmented snapshots).
template<typename GetTimestampFn>
std::size_t lower_bound_timestamp(
    const data_snapshot_t& snapshot,
    GetTimestampFn&& get_timestamp,
    double t)
{
    if (!snapshot || snapshot.count == 0) {
        return 0;
    }

    std::size_t lo = 0;
    std::size_t hi = snapshot.count;
    while (lo < hi) {
        std::size_t mid = lo + (hi - lo) / 2;
        const void* sample = snapshot.at(mid);
        if (!sample) {
            break;
        }
        if (get_timestamp(sample) < t) {
            lo = mid + 1;
        }
        else {
            hi = mid;
        }
    }
    return lo;
}

// Returns index of first sample with timestamp > t (upper_bound semantics).
template<typename GetTimestampFn>
std::size_t upper_bound_timestamp(
    const void* data,
    std::size_t count,
    std::size_t stride,
    GetTimestampFn&& get_timestamp,
    double t)
{
    if (data == nullptr || count == 0) {
        return 0;
    }

    const auto* base = static_cast<const std::uint8_t*>(data);
    std::size_t lo = 0;
    std::size_t hi = count;

    while (lo < hi) {
        std::size_t mid = lo + (hi - lo) / 2;
        const void* sample = base + mid * stride;
        if (get_timestamp(sample) <= t) {
            lo = mid + 1;
        }
        else {
            hi = mid;
        }
    }
    return lo;
}

// Overload for data_snapshot_t (supports segmented snapshots).
template<typename GetTimestampFn>
std::size_t upper_bound_timestamp(
    const data_snapshot_t& snapshot,
    GetTimestampFn&& get_timestamp,
    double t)
{
    if (!snapshot || snapshot.count == 0) {
        return 0;
    }

    std::size_t lo = 0;
    std::size_t hi = snapshot.count;
    while (lo < hi) {
        std::size_t mid = lo + (hi - lo) / 2;
        const void* sample = snapshot.at(mid);
        if (!sample) {
            break;
        }
        if (get_timestamp(sample) <= t) {
            lo = mid + 1;
        }
        else {
            hi = mid;
        }
    }
    return lo;
}

// -----------------------------------------------------------------------------
// LOD Level Selection
// -----------------------------------------------------------------------------
// Level selection based on pixels-per-sample, without hysteresis.
// Chooses the LOD level whose pixels-per-sample is closest to 1.0.

inline std::size_t choose_lod_level(
    const std::vector<std::size_t>& scales,
    std::size_t current_level,
    double base_pps)
{
    if (scales.empty() || !(base_pps > 0.0)) {
        return 0;
    }
    (void)current_level;

    constexpr double target_pps = 1.0;
    std::size_t best_level = 0;
    double best_error = std::abs(base_pps * static_cast<double>(scales[0]) - target_pps);

    for (std::size_t i = 1; i < scales.size(); ++i) {
        const double pps = base_pps * static_cast<double>(scales[i]);
        const double error = std::abs(pps - target_pps);
        if (error < best_error) {
            best_error = error;
            best_level = i;
        }
    }

    return best_level;
}

} // namespace detail
} // namespace vnm::plot
