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

    // Power-of-two chain beyond 2 days to keep exact multiples.
    double s = 172800.0; // 2 days
    const double limit = std::max(max_span * 2.0, s * 2.0);
    while (s < limit && s < 1e12) {
        s *= 2.0;
        steps.push_back(s);
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

// Returns index of first sample with timestamp >= t_ns (lower_bound semantics).
// Query value t_ns is in nanoseconds (API convention); the user-supplied
// get_timestamp accessor must return the same unit.
template<typename GetTimestampFn>
std::size_t lower_bound_timestamp(
    const void* data,
    std::size_t count,
    std::size_t stride,
    GetTimestampFn&& get_timestamp,
    std::int64_t t_ns)
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
        if (get_timestamp(sample) < t_ns) {
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
    std::int64_t t_ns)
{
    if (!snapshot.is_valid()) {
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
        if (get_timestamp(sample) < t_ns) {
            lo = mid + 1;
        }
        else {
            hi = mid;
        }
    }
    return lo;
}

// Returns index of first sample with timestamp > t_ns (upper_bound semantics).
// Query value t_ns is in nanoseconds (API convention).
template<typename GetTimestampFn>
std::size_t upper_bound_timestamp(
    const void* data,
    std::size_t count,
    std::size_t stride,
    GetTimestampFn&& get_timestamp,
    std::int64_t t_ns)
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
        if (get_timestamp(sample) <= t_ns) {
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
    std::int64_t t_ns)
{
    if (!snapshot.is_valid()) {
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
        if (get_timestamp(sample) <= t_ns) {
            lo = mid + 1;
        }
        else {
            hi = mid;
        }
    }
    return lo;
}

// -----------------------------------------------------------------------------
// Origin Selection for fp32 GPU Time Rebasing
// -----------------------------------------------------------------------------
// The renderer uploads sample timestamps as fp32 seconds relative to a
// per-view origin. Origin selection has two jobs: pick a snap step coarse
// enough that rebased seconds keep usable fp32 precision over the view
// span, and align the origin to that step so the cache key stays stable
// for small camera moves within the same snap bucket.
//
// Precision argument: fp32 has 24 mantissa bits, so it represents every
// integer up to 2^24 ~= 1.677e7 exactly. The maximum absolute rebased
// value is bounded by (snap_ns + span_ns) * 1e-9. The bucket policy below
// keeps span_ns/snap_ns <= ~1e6 across all spans, so the snap-step
// resolution within the rebased range is preserved by fp32's mantissa
// even when the view span itself exceeds that range. For spans wider
// than ~193 days (1.67e16 ns) the absolute rebased value exceeds 2^24
// seconds; precision then degrades to whole snap steps, but the snap
// step is coarse enough (>= 1 day) that this is the intended trade-off.

inline std::int64_t choose_snap_ns(std::int64_t span_ns)
{
    constexpr std::int64_t k_ns_per_us     = 1000LL;
    constexpr std::int64_t k_ns_per_ms     = 1000000LL;
    constexpr std::int64_t k_ns_per_second = 1000000000LL;
    constexpr std::int64_t k_ns_per_hour   = 3600LL * k_ns_per_second;
    constexpr std::int64_t k_ns_per_day    = 86400LL * k_ns_per_second;
    constexpr std::int64_t k_ns_per_year   = 365LL * k_ns_per_day;

    if (span_ns <= k_ns_per_ms)     { return 1LL; }
    if (span_ns <= k_ns_per_second) { return k_ns_per_us; }
    if (span_ns <= k_ns_per_day)    { return k_ns_per_second; }
    if (span_ns <= k_ns_per_year)   { return k_ns_per_hour; }
    return k_ns_per_day;
}

// Signed floor division of a by b, rounding toward negative infinity.
// Required because C++ integer division truncates toward zero, which
// rounds the wrong way for negative numerators and would push the origin
// above t_view_min for negative timestamps.
inline std::int64_t floor_div_i64(std::int64_t a, std::int64_t b)
{
    const std::int64_t q = a / b;
    const std::int64_t r = a % b;
    return r != 0 && ((r < 0) != (b < 0)) ? q - 1 : q;
}

// Pick a per-view time origin in nanoseconds. The result is the largest
// multiple of choose_snap_ns(span_ns) that is <= t_view_min_ns, so the
// origin lies on a stable bucket boundary and small camera moves within
// the same bucket reuse the same upload cache key.
//
// Saturates at the int64 floor: t_view_min_ns within one snap step of
// INT64_MIN cannot have a snap-aligned origin <= t_view_min_ns inside
// int64 range, so we return INT64_MIN. INT64_MIN is also used as a
// sentinel by callers (e.g. Plot_time_axis::k_t_unset), and saturating
// keeps the multiply from overflowing into UB on signed wrap.
inline std::int64_t choose_origin_ns(std::int64_t t_view_min_ns, std::int64_t span_ns)
{
    const std::int64_t snap_ns = choose_snap_ns(span_ns);
    const std::int64_t q       = floor_div_i64(t_view_min_ns, snap_ns);
    constexpr std::int64_t k_min = std::numeric_limits<std::int64_t>::min();
    if (snap_ns > 1 && q < k_min / snap_ns) {
        return k_min;
    }
    return q * snap_ns;
}

// -----------------------------------------------------------------------------
// LOD Level Selection
// -----------------------------------------------------------------------------
// Level selection based on pixels-per-sample, without hysteresis.
// Chooses the LOD level whose pixels-per-sample is closest to 1.0.

inline std::size_t choose_lod_level(
    const std::vector<std::size_t>& scales,
    double base_pps)
{
    if (scales.empty() || !(base_pps > 0.0)) {
        return 0;
    }

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
