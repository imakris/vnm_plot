#pragma once

// VNM Plot Library - Typed Access Policy Helpers
// Provides typed access policies to avoid void* casts in user code.

#include <vnm_plot/core/types.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace vnm::plot {

namespace detail {
template<typename>
struct always_false : std::false_type {};

constexpr std::uint64_t k_fnv_offset_basis = 1469598103934665603ULL;
constexpr std::uint64_t k_fnv_prime        = 1099511628211ULL;

// Member-pointer policies derive offsets from a real object; keep support to
// sample shapes whose default-initialization cannot execute user code.
template<typename Sample>
struct supports_member_pointer_access : std::bool_constant<
    std::is_standard_layout_v<Sample> &&
    std::is_trivially_default_constructible_v<Sample>> {};

template<typename Sample>
inline constexpr bool supports_member_pointer_access_v =
    supports_member_pointer_access<Sample>::value;

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
    if constexpr (supports_member_pointer_access_v<Sample>) {
        Sample instance;
        const auto* base = reinterpret_cast<const char*>(&instance);
        const auto* field = reinterpret_cast<const char*>(
            std::addressof(instance.*member));
        return static_cast<std::size_t>(field - base);
    }
    else {
        static_assert(always_false<Sample>::value,
            "Member-pointer access policies require standard-layout, trivially "
            "default-constructible Sample types. Use callable Data_access_policy "
            "with explicit layout/semantics keys for non-trivial sample types.");
        return 0;
    }
}

// The vnm_plot API contract for timestamps is int64_t nanoseconds. Sample
// types store the raw timestamp; if a sample's timestamp member is
// floating-point, the convention is that the field holds seconds (so it
// remains intuitive to the user populating it). This helper is the single
// point that crosses from typed sample storage into the public API;
// everything else in vnm_plot operates on int64_t nanoseconds.
template<typename Timestamp>
constexpr std::int64_t timestamp_member_to_ns(Timestamp value)
{
    if constexpr (std::is_floating_point_v<Timestamp>) {
        return static_cast<std::int64_t>(value * 1e9);
    }
    else {
        return static_cast<std::int64_t>(value);
    }
}

template<typename Member>
const std::decay_t<Member>& member_at_offset(
    const void*    sample,
    std::size_t    offset)
{
    const auto* bytes = static_cast<const std::uint8_t*>(sample);
    return *reinterpret_cast<const std::decay_t<Member>*>(bytes + offset);
}

template<typename Timestamp_member>
std::int64_t member_timestamp_access(
    const erased_access_policy_t&  access,
    const void*                    sample)
{
    using timestamp_t = std::decay_t<Timestamp_member>;
    return timestamp_member_to_ns<timestamp_t>(
        member_at_offset<timestamp_t>(sample, access.timestamp_offset));
}

template<typename Value_member>
float member_value_access(
    const erased_access_policy_t&  access,
    const void*                    sample)
{
    using value_t = std::decay_t<Value_member>;
    return static_cast<float>(
        member_at_offset<value_t>(sample, access.value_offset));
}

template<typename Range_min_member, typename Range_max_member>
std::pair<float, float> member_range_access(
    const erased_access_policy_t&  access,
    const void*                    sample)
{
    using range_min_t = std::decay_t<Range_min_member>;
    using range_max_t = std::decay_t<Range_max_member>;
    return std::make_pair(
        static_cast<float>(
            member_at_offset<range_min_t>(sample, access.range_min_offset)),
        static_cast<float>(
            member_at_offset<range_max_t>(sample, access.range_max_offset)));
}

// Produces a stable cache key from the byte-level identity of a Sample type's
// timestamp/value layout. Used by the renderer to key VAO and shader-selection
// caches; the renderer no longer reads sample bytes directly, but the key still
// distinguishes user sample types so caches stay correct across mixed-type
// series.
inline std::uint64_t compute_sample_layout_key(
    std::size_t    sample_stride,
    std::size_t    timestamp_offset,
    std::size_t    value_offset,
    bool           has_range,
    std::size_t    range_min_offset,
    std::size_t    range_max_offset)
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

template<typename Member>
constexpr std::uint64_t member_semantics_tag()
{
    using member_t = std::decay_t<Member>;
    constexpr std::uint64_t k_floating_tag          = 1ULL << 56;
    constexpr std::uint64_t k_signed_integral_tag   = 2ULL << 56;
    constexpr std::uint64_t k_unsigned_integral_tag = 3ULL << 56;
    constexpr std::uint64_t k_other_arithmetic_tag  = 4ULL << 56;

    if constexpr (std::is_floating_point_v<member_t>) {
        return k_floating_tag | sizeof(member_t);
    }
    else
    if constexpr (std::is_integral_v<member_t> && std::is_signed_v<member_t>) {
        return k_signed_integral_tag | sizeof(member_t);
    }
    else
    if constexpr (std::is_integral_v<member_t>) {
        return k_unsigned_integral_tag | sizeof(member_t);
    }
    else {
        return k_other_arithmetic_tag | sizeof(member_t);
    }
}

inline std::uint64_t compute_sample_semantics_key(
    std::size_t    sample_stride,
    std::size_t    timestamp_offset,
    std::uint64_t  timestamp_tag,
    std::size_t    value_offset,
    std::uint64_t  value_tag,
    bool           has_range,
    std::size_t    range_min_offset,
    std::uint64_t  range_min_tag,
    std::size_t    range_max_offset,
    std::uint64_t  range_max_tag)
{
    std::uint64_t h = k_fnv_offset_basis;
    h = fnv1a_mix(h, 0x53454D414E544943ULL);
    h = fnv1a_mix(h, sample_stride);
    h = fnv1a_mix(h, timestamp_offset);
    h = fnv1a_mix(h, timestamp_tag);
    h = fnv1a_mix(h, value_offset);
    h = fnv1a_mix(h, value_tag);
    h = fnv1a_mix(h, has_range ? 1ULL : 0ULL);
    h = fnv1a_mix(h, range_min_offset);
    h = fnv1a_mix(h, range_min_tag);
    h = fnv1a_mix(h, range_max_offset);
    h = fnv1a_mix(h, range_max_tag);
    return h;
}

} // namespace detail

template<typename Sample>
struct Data_access_policy_typed
{
    using timestamp_accessor_t =
        detail::access_function_slot_t<std::int64_t(const Sample&)>;
    using value_accessor_t     =
        detail::access_function_slot_t<float(const Sample&)>;
    using range_accessor_t     =
        detail::access_function_slot_t<std::pair<float, float>(const Sample&)>;

    Data_access_policy_typed()
    {
        bind_accessor_slots();
    }

    Data_access_policy_typed(const Data_access_policy_typed& other)
    :
        get_timestamp(other.get_timestamp),
        get_value(other.get_value),
        get_range(other.get_range),
        layout_key(other.layout_key),
        semantics_key(other.semantics_key),
        internal_access(other.internal_access),
        access_revision(other.access_revision)
    {
        bind_accessor_slots();
    }

    Data_access_policy_typed(Data_access_policy_typed&& other)
    :
        get_timestamp(other.get_timestamp),
        get_value(other.get_value),
        get_range(other.get_range),
        layout_key(other.layout_key),
        semantics_key(other.semantics_key),
        internal_access(other.internal_access),
        access_revision(other.access_revision)
    {
        bind_accessor_slots();
    }

    Data_access_policy_typed& operator=(const Data_access_policy_typed& other)
    {
        if (this != &other) {
            get_timestamp = other.get_timestamp;
            get_value = other.get_value;
            get_range = other.get_range;
            layout_key = other.layout_key;
            semantics_key = other.semantics_key;
            internal_access = other.internal_access;
            ++access_revision;
            bind_accessor_slots();
        }
        return *this;
    }

    Data_access_policy_typed& operator=(Data_access_policy_typed&& other)
    {
        if (this != &other) {
            get_timestamp = other.get_timestamp;
            get_value = other.get_value;
            get_range = other.get_range;
            layout_key = other.layout_key;
            semantics_key = other.semantics_key;
            internal_access = other.internal_access;
            ++access_revision;
            bind_accessor_slots();
        }
        return *this;
    }

    // Timestamps are int64_t nanoseconds (API convention).
    timestamp_accessor_t   get_timestamp;
    value_accessor_t       get_value;
    range_accessor_t       get_range;

    uint64_t               layout_key = 0;
    sample_semantics_key_t semantics_key;

    bool is_valid() const
    {
        return get_timestamp && (get_value || get_range);
    }

    Data_access_policy_typed& set_semantics_key(
        std::uint64_t  value,
        std::uint64_t  revision = 0) noexcept
    {
        semantics_key =
            detail::make_explicit_sample_semantics_key(value, revision);
        return *this;
    }

    Data_access_policy erase() const
    {
        Data_access_policy policy;
        const auto erase_accessor = [](const auto& accessor) {
            auto fn = accessor.function();
            return [fn = std::move(fn)](const void* sample) {
                return fn(*static_cast<const Sample*>(sample));
            };
        };
        if (get_timestamp) {
            policy.get_timestamp = erase_accessor(get_timestamp);
        }
        if (get_value) {
            policy.get_value = erase_accessor(get_value);
        }
        if (get_range) {
            policy.get_range = erase_accessor(get_range);
        }
        policy.layout_key = layout_key;
        policy.semantics_key = semantics_key;
        policy.set_internal_access(internal_access);
        return policy;
    }

private:
    template<typename S, typename Timestamp_member, typename Value_member>
    friend void assign_standard_accessors(
        Data_access_policy_typed<S>&   policy,
        Timestamp_member S::*          timestamp_member,
        Value_member S::*              value_member);

    template<typename S, typename Timestamp_member, typename Value_member>
    friend Data_access_policy_typed<S> make_access_policy(
        Timestamp_member S::*  timestamp_member,
        Value_member S::*      value_member);

    template<typename S, typename Timestamp_member, typename Value_member,
             typename Range_min_member, typename Range_max_member>
    friend Data_access_policy_typed<S> make_access_policy(
        Timestamp_member S::*  timestamp_member,
        Value_member S::*      value_member,
        Range_min_member S::*  range_min_member,
        Range_max_member S::*  range_max_member);

    void set_internal_access(detail::erased_access_policy_t access)
    {
        internal_access = access;
        ++access_revision;
        bind_accessor_slots();
    }

    void bind_accessor_slots() noexcept
    {
        get_timestamp.bind_internal_access(
            &internal_access,
            &access_revision,
            &semantics_key);
        get_value.bind_internal_access(
            &internal_access,
            &access_revision,
            &semantics_key);
        get_range.bind_internal_access(
            &internal_access,
            &access_revision,
            &semantics_key);
    }

    detail::erased_access_policy_t internal_access;
    std::uint64_t access_revision = 1;
};

template<typename Sample, typename Timestamp_member, typename Value_member>
inline void assign_standard_accessors(
    Data_access_policy_typed<Sample>&  policy,
    Timestamp_member Sample::*         timestamp_member,
    Value_member Sample::*             value_member)
{
    policy.get_timestamp = [timestamp_member](const Sample& sample) -> std::int64_t {
        using timestamp_t = std::decay_t<decltype(sample.*timestamp_member)>;
        return detail::timestamp_member_to_ns<timestamp_t>(sample.*timestamp_member);
    };
    policy.get_value = [value_member](const Sample& sample) {
        return static_cast<float>(sample.*value_member);
    };
    detail::erased_access_policy_t internal_access;
    internal_access.get_timestamp =
        &detail::member_timestamp_access<Timestamp_member>;
    internal_access.get_value =
        &detail::member_value_access<Value_member>;
    internal_access.timestamp_offset =
        detail::member_offset(timestamp_member);
    internal_access.value_offset = detail::member_offset(value_member);
    internal_access.dispatch_kind =
        detail::access_dispatch_kind_t::MEMBER_POINTER;
    policy.set_internal_access(internal_access);
}

template<typename Sample, typename Timestamp_member, typename Value_member,
         typename Range_min_member, typename Range_max_member>
inline Data_access_policy_typed<Sample> make_access_policy(
    Timestamp_member Sample::* timestamp_member,
    Value_member Sample::*     value_member,
    Range_min_member Sample::* range_min_member,
    Range_max_member Sample::* range_max_member)
{
    Data_access_policy_typed<Sample> policy;
    const std::size_t timestamp_offset = detail::member_offset(timestamp_member);
    const std::size_t value_offset     = detail::member_offset(value_member);
    const std::size_t range_min_offset = detail::member_offset(range_min_member);
    const std::size_t range_max_offset = detail::member_offset(range_max_member);

    assign_standard_accessors(policy, timestamp_member, value_member);
    policy.get_range = [range_min_member, range_max_member](const Sample& sample) {
        const float low  = static_cast<float>(sample.*range_min_member);
        const float high = static_cast<float>(sample.*range_max_member);
        return std::make_pair(low, high);
    };
    detail::erased_access_policy_t internal_access;
    internal_access.get_timestamp =
        &detail::member_timestamp_access<Timestamp_member>;
    internal_access.get_value =
        &detail::member_value_access<Value_member>;
    internal_access.get_range =
        &detail::member_range_access<Range_min_member, Range_max_member>;
    internal_access.timestamp_offset = timestamp_offset;
    internal_access.value_offset = value_offset;
    internal_access.range_min_offset = range_min_offset;
    internal_access.range_max_offset = range_max_offset;
    internal_access.dispatch_kind =
        detail::access_dispatch_kind_t::MEMBER_POINTER;
    policy.set_internal_access(internal_access);
    policy.layout_key = detail::compute_sample_layout_key(
        sizeof(Sample),
        timestamp_offset,
        value_offset,
        true,
        range_min_offset,
        range_max_offset);
    policy.semantics_key.value = detail::compute_sample_semantics_key(
        sizeof(Sample),
        timestamp_offset,
        detail::member_semantics_tag<Timestamp_member>(),
        value_offset,
        detail::member_semantics_tag<Value_member>(),
        true,
        range_min_offset,
        detail::member_semantics_tag<Range_min_member>(),
        range_max_offset,
        detail::member_semantics_tag<Range_max_member>());
    policy.semantics_key.revision = 0;
    policy.semantics_key.conservative = false;
    return policy;
}

template<typename Sample, typename Timestamp_member, typename Value_member>
inline Data_access_policy_typed<Sample> make_access_policy(
    Timestamp_member Sample::* timestamp_member,
    Value_member Sample::*     value_member)
{
    Data_access_policy_typed<Sample> policy;
    const std::size_t timestamp_offset = detail::member_offset(timestamp_member);
    const std::size_t value_offset     = detail::member_offset(value_member);

    assign_standard_accessors(policy, timestamp_member, value_member);
    policy.layout_key = detail::compute_sample_layout_key(
        sizeof(Sample),
        timestamp_offset,
        value_offset,
        false,
        0,
        0);
    policy.semantics_key.value = detail::compute_sample_semantics_key(
        sizeof(Sample),
        timestamp_offset,
        detail::member_semantics_tag<Timestamp_member>(),
        value_offset,
        detail::member_semantics_tag<Value_member>(),
        false,
        0,
        0,
        0,
        0);
    policy.semantics_key.revision = 0;
    policy.semantics_key.conservative = false;
    return policy;
}

} // namespace vnm::plot
