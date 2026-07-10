#pragma once

// VNM Plot Library - Time Unit Helpers
// Centralized int64 nanosecond arithmetic for C++ and QML boundary code.

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace vnm::plot {

inline constexpr std::int64_t k_ns_per_ms = 1'000'000;
inline constexpr std::int64_t k_ns_per_second = 1'000'000'000;

struct time_range_t
{
    std::int64_t min_ns = 0;
    std::int64_t max_ns = 0;
};

enum class Time_translation_direction
{
    BACKWARD,
    FORWARD,
};

inline std::optional<std::int64_t> checked_add_ns(
    std::int64_t value_ns,
    std::int64_t delta_ns) noexcept
{
    if (delta_ns > 0
        && value_ns > std::numeric_limits<std::int64_t>::max() - delta_ns)
    {
        return std::nullopt;
    }
    if (delta_ns < 0
        && value_ns < std::numeric_limits<std::int64_t>::min() - delta_ns)
    {
        return std::nullopt;
    }
    return value_ns + delta_ns;
}

inline std::optional<std::int64_t> checked_sub_ns(
    std::int64_t value_ns,
    std::int64_t delta_ns) noexcept
{
    if (delta_ns > 0
        && value_ns < std::numeric_limits<std::int64_t>::min() + delta_ns)
    {
        return std::nullopt;
    }
    if (delta_ns < 0
        && value_ns > std::numeric_limits<std::int64_t>::max() + delta_ns)
    {
        return std::nullopt;
    }
    return value_ns - delta_ns;
}

inline std::int64_t saturating_add_ns(
    std::int64_t value_ns,
    std::int64_t delta_ns) noexcept
{
    const auto result = checked_add_ns(value_ns, delta_ns);
    if (result) {
        return *result;
    }
    return delta_ns > 0
        ? std::numeric_limits<std::int64_t>::max()
        : std::numeric_limits<std::int64_t>::min();
}

inline std::int64_t saturating_sub_ns(
    std::int64_t value_ns,
    std::int64_t delta_ns) noexcept
{
    const auto result = checked_sub_ns(value_ns, delta_ns);
    if (result) {
        return *result;
    }
    return delta_ns > 0
        ? std::numeric_limits<std::int64_t>::min()
        : std::numeric_limits<std::int64_t>::max();
}

inline std::int64_t saturating_add_duration_ns(
    std::int64_t value_ns,
    std::uint64_t duration_ns) noexcept
{
    std::int64_t result = value_ns;
    std::uint64_t remaining = duration_ns;
    constexpr std::uint64_t k_max_chunk =
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());

    while (remaining > 0) {
        const std::uint64_t chunk_u = (remaining > k_max_chunk) ? k_max_chunk : remaining;
        const auto chunk = static_cast<std::int64_t>(chunk_u);
        const auto next = checked_add_ns(result, chunk);
        if (!next) {
            return std::numeric_limits<std::int64_t>::max();
        }
        result = *next;
        remaining -= chunk_u;
    }

    return result;
}

inline std::int64_t saturating_sub_duration_ns(
    std::int64_t value_ns,
    std::uint64_t duration_ns) noexcept
{
    std::int64_t result = value_ns;
    std::uint64_t remaining = duration_ns;
    constexpr std::uint64_t k_max_chunk =
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());

    while (remaining > 0) {
        const std::uint64_t chunk_u = (remaining > k_max_chunk) ? k_max_chunk : remaining;
        const auto chunk = static_cast<std::int64_t>(chunk_u);
        const auto next = checked_sub_ns(result, chunk);
        if (!next) {
            return std::numeric_limits<std::int64_t>::min();
        }
        result = *next;
        remaining -= chunk_u;
    }

    return result;
}

inline std::optional<std::uint64_t> positive_span_ns(
    std::int64_t min_ns,
    std::int64_t max_ns) noexcept
{
    if (!(max_ns > min_ns)) {
        return std::nullopt;
    }

    return static_cast<std::uint64_t>(max_ns) -
        static_cast<std::uint64_t>(min_ns);
}

namespace detail {

inline std::int64_t positive_span_ns_for_signed_api(
    std::int64_t min_ns,
    std::int64_t max_ns) noexcept
{
    const auto span = positive_span_ns(min_ns, max_ns);
    if (!span) {
        return 0;
    }
    if (*span > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return std::numeric_limits<std::int64_t>::max();
    }
    return static_cast<std::int64_t>(*span);
}

} // namespace detail

inline long double span_ns_as_long_double(
    std::int64_t min_ns,
    std::int64_t max_ns) noexcept
{
    return static_cast<long double>(max_ns) -
        static_cast<long double>(min_ns);
}

inline std::optional<long double> positive_span_ns_as_long_double(
    std::int64_t min_ns,
    std::int64_t max_ns) noexcept
{
    if (!(max_ns > min_ns)) {
        return std::nullopt;
    }
    return span_ns_as_long_double(min_ns, max_ns);
}

inline std::int64_t midpoint_ns(
    std::int64_t min_ns,
    std::int64_t max_ns) noexcept
{
    if (min_ns <= max_ns) {
        const std::uint64_t span =
            static_cast<std::uint64_t>(max_ns) -
            static_cast<std::uint64_t>(min_ns);
        return saturating_add_duration_ns(min_ns, span / 2);
    }

    const std::uint64_t span =
        static_cast<std::uint64_t>(min_ns) -
        static_cast<std::uint64_t>(max_ns);
    return saturating_sub_duration_ns(min_ns, span / 2);
}

inline time_range_t centered_time_range_ns(
    std::int64_t center_ns,
    std::uint64_t span_ns) noexcept
{
    if (span_ns == 0) {
        return {center_ns, center_ns};
    }
    if (span_ns == std::numeric_limits<std::uint64_t>::max()) {
        return {
            std::numeric_limits<std::int64_t>::min(),
            std::numeric_limits<std::int64_t>::max()
        };
    }

    const std::uint64_t left_span_ns = span_ns / 2;
    const std::uint64_t right_span_ns = span_ns - left_span_ns;
    time_range_t range{
        saturating_sub_duration_ns(center_ns, left_span_ns),
        saturating_add_duration_ns(center_ns, right_span_ns)
    };
    const auto actual_span = positive_span_ns(range.min_ns, range.max_ns);
    if (actual_span && *actual_span == span_ns) {
        return range;
    }

    if (range.max_ns == std::numeric_limits<std::int64_t>::max()) {
        range.min_ns = saturating_sub_duration_ns(range.max_ns, span_ns);
    }
    else
    if (range.min_ns == std::numeric_limits<std::int64_t>::min()) {
        range.max_ns = saturating_add_duration_ns(range.min_ns, span_ns);
    }

    return range;
}

inline std::uint64_t duration_at_fraction_ns(
    std::uint64_t duration_ns,
    long double fraction) noexcept
{
    if (!std::isfinite(fraction) || fraction <= 0.0L) { return 0;           }
    if (fraction >= 1.0L)                             { return duration_ns; }

    const long double rounded =
        std::round(static_cast<long double>(duration_ns) * fraction);
    if (rounded <= 0.0L) {
        return 0;
    }

    const long double max_value = static_cast<long double>(duration_ns);
    if (rounded >= max_value) {
        return duration_ns;
    }

    return static_cast<std::uint64_t>(rounded);
}

inline std::optional<std::int64_t> time_at_fraction_ns(
    time_range_t range,
    long double fraction) noexcept
{
    const auto span = positive_span_ns(range.min_ns, range.max_ns);
    if (!span) {
        return std::nullopt;
    }

    return saturating_add_duration_ns(
        range.min_ns,
        duration_at_fraction_ns(*span, fraction));
}

inline std::uint64_t scaled_duration_ns(
    std::uint64_t duration_ns,
    long double scale) noexcept
{
    if (!std::isfinite(scale) || scale <= 0.0L || duration_ns == 0) {
        return 0;
    }

    const long double rounded =
        std::round(static_cast<long double>(duration_ns) * scale);
    if (rounded <= 0.0L) {
        return 0;
    }

    const long double max_value =
        static_cast<long double>(std::numeric_limits<std::uint64_t>::max());
    if (rounded >= max_value) {
        return std::numeric_limits<std::uint64_t>::max();
    }

    return static_cast<std::uint64_t>(rounded);
}

inline time_range_t time_range_around_pivot_ns(
    std::int64_t pivot_ns,
    std::uint64_t left_span_ns,
    std::uint64_t right_span_ns) noexcept
{
    if (left_span_ns > std::numeric_limits<std::uint64_t>::max() - right_span_ns) {
        return {
            std::numeric_limits<std::int64_t>::min(),
            std::numeric_limits<std::int64_t>::max()
        };
    }

    const std::uint64_t total_span_ns = left_span_ns + right_span_ns;
    if (total_span_ns >= std::numeric_limits<std::uint64_t>::max()) {
        return {
            std::numeric_limits<std::int64_t>::min(),
            std::numeric_limits<std::int64_t>::max()
        };
    }

    time_range_t range{
        saturating_sub_duration_ns(pivot_ns, left_span_ns),
        saturating_add_duration_ns(pivot_ns, right_span_ns)
    };
    const auto actual_span = positive_span_ns(range.min_ns, range.max_ns);
    if (actual_span && *actual_span == total_span_ns) {
        return range;
    }

    if (range.max_ns == std::numeric_limits<std::int64_t>::max()) {
        range.min_ns = saturating_sub_duration_ns(range.max_ns, total_span_ns);
    }
    else
    if (range.min_ns == std::numeric_limits<std::int64_t>::min()) {
        range.max_ns = saturating_add_duration_ns(range.min_ns, total_span_ns);
    }

    return range;
}

inline std::optional<time_range_t> translate_time_range_ns(
    time_range_t range,
    std::int64_t delta_ns) noexcept
{
    const auto span = positive_span_ns(range.min_ns, range.max_ns);
    if (!span) {
        return std::nullopt;
    }

    const auto new_min = checked_add_ns(range.min_ns, delta_ns);
    const auto new_max = checked_add_ns(range.max_ns, delta_ns);
    if (new_min && new_max) {
        return time_range_t{*new_min, *new_max};
    }

    if (delta_ns > 0) {
        const std::int64_t max_ns = std::numeric_limits<std::int64_t>::max();
        return time_range_t{ saturating_sub_duration_ns(max_ns, *span), max_ns };
    }

    const std::int64_t min_ns = std::numeric_limits<std::int64_t>::min();
    return time_range_t{ min_ns, saturating_add_duration_ns(min_ns, *span) };
}

inline std::optional<time_range_t> translate_time_range_by_duration_ns(
    time_range_t range,
    std::uint64_t duration_ns,
    Time_translation_direction direction) noexcept
{
    const auto span = positive_span_ns(range.min_ns, range.max_ns);
    if (!span)            { return std::nullopt; }
    if (duration_ns == 0) { return range;        }

    if (direction == Time_translation_direction::FORWARD) {
        time_range_t shifted{
            saturating_add_duration_ns(range.min_ns, duration_ns),
            saturating_add_duration_ns(range.max_ns, duration_ns)
        };
        const auto shifted_span = positive_span_ns(shifted.min_ns, shifted.max_ns);
        if (shifted_span && *shifted_span == *span) {
            return shifted;
        }

        const std::int64_t max_ns = std::numeric_limits<std::int64_t>::max();
        return time_range_t{ saturating_sub_duration_ns(max_ns, *span), max_ns };
    }

    time_range_t shifted{
        saturating_sub_duration_ns(range.min_ns, duration_ns),
        saturating_sub_duration_ns(range.max_ns, duration_ns)
    };
    const auto shifted_span = positive_span_ns(shifted.min_ns, shifted.max_ns);
    if (shifted_span && *shifted_span == *span) {
        return shifted;
    }

    const std::int64_t min_ns = std::numeric_limits<std::int64_t>::min();
    return time_range_t{ min_ns, saturating_add_duration_ns(min_ns, *span) };
}

inline std::optional<time_range_t> clamp_time_range_to_available_ns(
    time_range_t target,
    time_range_t available) noexcept
{
    const auto target_span = positive_span_ns(target.min_ns, target.max_ns);
    if (!target_span) {
        return std::nullopt;
    }

    const auto available_span = positive_span_ns(available.min_ns, available.max_ns);
    if (!available_span) {
        return target;
    }

    const std::uint64_t span =
        (*target_span > *available_span) ? *available_span : *target_span;
    time_range_t clamped = centered_time_range_ns(
        midpoint_ns(target.min_ns, target.max_ns),
        span);

    if (clamped.max_ns > available.max_ns) {
        clamped.max_ns = available.max_ns;
        clamped.min_ns = saturating_sub_duration_ns(clamped.max_ns, span);
    }
    if (clamped.min_ns < available.min_ns) {
        clamped.min_ns = available.min_ns;
        clamped.max_ns = saturating_add_duration_ns(clamped.min_ns, span);
    }

    return clamped;
}

inline std::int64_t floor_div_int64(std::int64_t value, std::int64_t denom) noexcept
{
    if (denom == 0) {
        return 0;
    }
    if (value == std::numeric_limits<std::int64_t>::min() && denom == -1) {
        return std::numeric_limits<std::int64_t>::max();
    }

    const std::int64_t q = value / denom;
    const std::int64_t r = value % denom;
    return (r != 0 && ((r < 0) != (denom < 0))) ? q - 1 : q;
}

inline std::int64_t ns_to_ms_for_qml(std::int64_t ns_value) noexcept
{
    return floor_div_int64(ns_value, k_ns_per_ms);
}

inline std::int64_t saturating_ms_to_ns(std::int64_t ms_value) noexcept
{
    constexpr std::int64_t k_max_ms =
        std::numeric_limits<std::int64_t>::max() / k_ns_per_ms;
    constexpr std::int64_t k_min_ms =
        std::numeric_limits<std::int64_t>::min() / k_ns_per_ms;

    if (ms_value > k_max_ms) { return std::numeric_limits<std::int64_t>::max(); }
    if (ms_value < k_min_ms) { return std::numeric_limits<std::int64_t>::min(); }
    return ms_value * k_ns_per_ms;
}

inline std::int64_t ms_for_qml_to_ns(std::int64_t ms_value) noexcept
{
    return saturating_ms_to_ns(ms_value);
}

} // namespace vnm::plot
