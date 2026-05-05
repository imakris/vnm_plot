#pragma once

// VNM Plot Library - Typed Access Policy Helpers
// Provides typed access policies to avoid void* casts in user code.

#include <vnm_plot/core/types.h>

#include <cstring>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>
#include <utility>

namespace vnm::plot {

namespace detail {
template<typename>
struct always_false : std::false_type {};

constexpr std::uint64_t k_fnv_offset_basis = 1469598103934665603ULL;
constexpr std::uint64_t k_fnv_prime = 1099511628211ULL;

inline std::uint64_t fnv1a_mix(std::uint64_t h, std::uint64_t value)
{
    for (int i = 0; i < 8; ++i) {
        h ^= (value >> (i * 8)) & 0xFFu;
        h *= k_fnv_prime;
    }
    return h;
}

template<typename Sample, typename Member>
std::size_t member_offset(Member Sample::* member)
{
    static_assert(std::is_standard_layout_v<Sample>,
        "Sample type must be standard-layout for member offsets.");
    static_assert(std::is_default_constructible_v<Sample>,
        "Sample type must be default-constructible for member offsets.");
    const Sample instance{};
    const auto base = reinterpret_cast<const char*>(&instance);
    const auto field = reinterpret_cast<const char*>(&(instance.*member));
    return static_cast<std::size_t>(field - base);
}

template<typename Sample, typename Timestamp_member>
auto make_clone_with_timestamp(Timestamp_member Sample::* timestamp_member)
{
    return [timestamp_member](Sample& dst_sample, const Sample& src_sample, std::int64_t timestamp_ns) {
        dst_sample = src_sample;
        using timestamp_t = std::decay_t<decltype(dst_sample.*timestamp_member)>;
        // The vnm_plot API contract for timestamps is int64_t nanoseconds.
        // Sample types store the raw timestamp; if a sample's timestamp
        // member is floating-point, the convention is that the field
        // holds seconds (so it remains intuitive to the user populating
        // it). The boundary code in this file converts both directions.
        if constexpr (std::is_floating_point_v<timestamp_t>) {
            dst_sample.*timestamp_member = static_cast<timestamp_t>(timestamp_ns) * 1e-9;
        }
        else {
            dst_sample.*timestamp_member = static_cast<timestamp_t>(timestamp_ns);
        }
    };
}

// Produces a stable cache key from the byte-level identity of a Sample type's
// timestamp/value layout. Used by the renderer to key VAO and shader-selection
// caches; the renderer no longer reads sample bytes directly, but the key still
// distinguishes user sample types so caches stay correct across mixed-type
// series.
inline std::uint64_t compute_sample_layout_key(
    std::size_t sample_stride,
    std::size_t timestamp_offset,
    std::size_t value_offset,
    bool has_range,
    std::size_t range_min_offset,
    std::size_t range_max_offset)
{
    std::uint64_t h = k_fnv_offset_basis;
    h = fnv1a_mix(h, sample_stride);
    h = fnv1a_mix(h, timestamp_offset);
    h = fnv1a_mix(h, value_offset);
    h = fnv1a_mix(h, has_range ? 1ULL : 0ULL);
    h = fnv1a_mix(h, range_min_offset);
    h = fnv1a_mix(h, range_max_offset);
    return h;
}

} // namespace detail

template<typename Sample>
struct Data_access_policy_typed
{
    // Timestamps are int64_t nanoseconds (API convention).
    std::function<std::int64_t(const Sample&)> get_timestamp;
    std::function<float(const Sample&)> get_value;
    std::function<std::pair<float, float>(const Sample&)> get_range;

    std::function<double(const Sample&)> get_aux_metric;
    std::function<float(const Sample&)> get_signal;
    std::function<void(Sample& dst_sample, const Sample& src_sample, std::int64_t timestamp_ns)> clone_with_timestamp;

    uint64_t layout_key = 0;

    bool is_valid() const
    {
        return get_timestamp && (get_value || get_range);
    }

    Data_access_policy erase() const
    {
        Data_access_policy policy;
        if (get_timestamp) {
            policy.get_timestamp = [fn = get_timestamp](const void* sample) {
                return fn(*static_cast<const Sample*>(sample));
            };
        }
        if (get_value) {
            policy.get_value = [fn = get_value](const void* sample) {
                return fn(*static_cast<const Sample*>(sample));
            };
        }
        if (get_range) {
            policy.get_range = [fn = get_range](const void* sample) {
                return fn(*static_cast<const Sample*>(sample));
            };
        }
        if (get_aux_metric) {
            policy.get_aux_metric = [fn = get_aux_metric](const void* sample) {
                return fn(*static_cast<const Sample*>(sample));
            };
        }
        if (get_signal) {
            policy.get_signal = [fn = get_signal](const void* sample) {
                return fn(*static_cast<const Sample*>(sample));
            };
        }
        if (clone_with_timestamp) {
            policy.clone_with_timestamp = [fn = clone_with_timestamp](void* dst_sample, const void* src_sample, std::int64_t timestamp_ns) {
                if (!dst_sample || !src_sample) {
                    return;
                }
                // Write through a properly aligned local sample, then copy bytes
                // into caller-provided storage to avoid alignment UB.
                Sample tmp_sample{};
                fn(
                    tmp_sample,
                    *static_cast<const Sample*>(src_sample),
                    timestamp_ns);
                std::memcpy(dst_sample, &tmp_sample, sizeof(Sample));
            };
        }
        policy.layout_key = layout_key;
        return policy;
    }
};

template<typename Sample, typename Timestamp_member, typename Value_member>
inline void assign_standard_accessors(
    Data_access_policy_typed<Sample>& policy,
    Timestamp_member Sample::* timestamp_member,
    Value_member Sample::* value_member)
{
    policy.get_timestamp = [timestamp_member](const Sample& sample) -> std::int64_t {
        using timestamp_t = std::decay_t<decltype(sample.*timestamp_member)>;
        // API contract: timestamps are int64_t nanoseconds. If the user's
        // sample type stores its timestamp as a floating-point member, the
        // convention is that the value is in seconds; convert to ns at the
        // boundary so the rest of vnm_plot sees one consistent unit. An
        // integer-typed member is taken at face value; the user is
        // responsible for storing nanoseconds in that case.
        if constexpr (std::is_floating_point_v<timestamp_t>) {
            return static_cast<std::int64_t>((sample.*timestamp_member) * 1e9);
        }
        else {
            return static_cast<std::int64_t>(sample.*timestamp_member);
        }
    };
    policy.get_value = [value_member](const Sample& sample) {
        return static_cast<float>(sample.*value_member);
    };
    policy.clone_with_timestamp = detail::make_clone_with_timestamp(timestamp_member);
}

template<typename Sample, typename Timestamp_member, typename Value_member>
inline Data_access_policy_typed<Sample> make_access_policy(
    Timestamp_member Sample::* timestamp_member,
    Value_member Sample::* value_member)
{
    Data_access_policy_typed<Sample> policy;
    assign_standard_accessors(policy, timestamp_member, value_member);
    policy.layout_key = detail::compute_sample_layout_key(
        sizeof(Sample),
        detail::member_offset(timestamp_member),
        detail::member_offset(value_member),
        false,
        0,
        0);
    return policy;
}

template<typename Sample, typename Timestamp_member, typename Value_member,
         typename Range_min_member, typename Range_max_member>
inline Data_access_policy_typed<Sample> make_access_policy(
    Timestamp_member Sample::* timestamp_member,
    Value_member Sample::* value_member,
    Range_min_member Sample::* range_min_member,
    Range_max_member Sample::* range_max_member)
{
    Data_access_policy_typed<Sample> policy;
    assign_standard_accessors(policy, timestamp_member, value_member);
    policy.get_range = [range_min_member, range_max_member](const Sample& sample) {
        const float low = static_cast<float>(sample.*range_min_member);
        const float high = static_cast<float>(sample.*range_max_member);
        return std::make_pair(low, high);
    };
    policy.layout_key = detail::compute_sample_layout_key(
        sizeof(Sample),
        detail::member_offset(timestamp_member),
        detail::member_offset(value_member),
        true,
        detail::member_offset(range_min_member),
        detail::member_offset(range_max_member));
    return policy;
}

} // namespace vnm::plot
