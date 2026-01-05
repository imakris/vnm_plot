#pragma once

// VNM Plot Library - Core Algorithm Utilities
// Small, header-only helpers for axis calculations and formatting.
// Pure C++ with no framework dependencies.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace vnm::plot::core::algo {

// -----------------------------------------------------------------------------
// Number Formatting
// -----------------------------------------------------------------------------

// Format a numeric value with either integer or fixed precision.
inline std::string format_axis_fixed_or_int(double v, int digits)
{
    if (digits <= 0) {
        const std::int64_t iv = std::llround(v);
        if (iv == 0) return "0";
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

// -----------------------------------------------------------------------------
// Circular Indexing
// -----------------------------------------------------------------------------

// Circular index into a container (wrap-around, supports negatives).
template<typename ContainerT>
inline std::size_t circular_index(const ContainerT& c, int index)
{
    if (c.empty()) return 0;

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
        1, 2, 10, 30,           // seconds
        60, 300, 900, 1800,     // minutes: 1m, 5m, 15m, 30m
        3600, 7200, 21600, 43200, // hours: 1h, 2h, 6h, 12h
        86400                   // 1 day
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
    if (steps.empty()) return -1;

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

} // namespace vnm::plot::core::algo
