#pragma once

// VNM Plot Library - Typed Access Policy Helpers
// Provides typed access policies to avoid void* casts in user code.

#include <vnm_plot/core/types.h>
#include <vnm_plot/core/vertex_layout.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>
#include <utility>

namespace vnm::plot {

namespace detail {
template<typename>
struct always_false : std::false_type {};

template<typename T>
constexpr Vertex_attrib_type vertex_attrib_type_for()
{
    using U = std::remove_cv_t<T>;
    if constexpr (std::is_same_v<U, float>) {
        return Vertex_attrib_type::FLOAT32;
    }
    else
    if constexpr (std::is_same_v<U, double>) {
        return Vertex_attrib_type::FLOAT64;
    }
    else
    if constexpr (std::is_same_v<U, std::int32_t> || std::is_same_v<U, int>) {
        return Vertex_attrib_type::INT32;
    }
    else
    if constexpr (std::is_same_v<U, std::uint32_t> || std::is_same_v<U, unsigned int>) {
        return Vertex_attrib_type::UINT32;
    }
    else {
        static_assert(always_false<U>::value, "Unsupported vertex attribute type");
        return Vertex_attrib_type::FLOAT32;
    }
}

template<typename Sample, typename Member>
constexpr std::size_t member_offset(Member Sample::* member)
{
    static_assert(std::is_standard_layout_v<Sample>,
        "Sample type must be standard-layout for member offsets.");
    return static_cast<std::size_t>(
        reinterpret_cast<std::uintptr_t>(&(reinterpret_cast<const Sample*>(0)->*member)));
}

} // namespace detail

template<typename Sample>
struct Data_access_policy_typed
{
    std::function<double(const Sample&)> get_timestamp;
    std::function<float(const Sample&)> get_value;
    std::function<std::pair<float, float>(const Sample&)> get_range;

    std::function<double(const Sample&)> get_aux_metric;
    std::function<float(const Sample&)> get_signal;

    std::function<void()> setup_vertex_attributes;
    std::function<void(unsigned int)> bind_uniforms;
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
        policy.setup_vertex_attributes = setup_vertex_attributes;
        policy.bind_uniforms = bind_uniforms;
        policy.layout_key = layout_key;
        return policy;
    }
};

template<typename Sample, typename Timestamp_member, typename Value_member>
inline Vertex_layout make_standard_layout(
    Timestamp_member Sample::* timestamp_member,
    Value_member Sample::* value_member)
{
    Vertex_layout layout;
    layout.stride = sizeof(Sample);
    layout.attributes = {
        {
            0,
            detail::vertex_attrib_type_for<Timestamp_member>(),
            1,
            detail::member_offset(timestamp_member),
            false
        },
        {
            1,
            detail::vertex_attrib_type_for<Value_member>(),
            1,
            detail::member_offset(value_member),
            false
        }
    };
    return layout;
}

template<typename Sample, typename Timestamp_member, typename Value_member,
         typename Range_min_member, typename Range_max_member>
inline Vertex_layout make_standard_layout(
    Timestamp_member Sample::* timestamp_member,
    Value_member Sample::* value_member,
    Range_min_member Sample::* range_min_member,
    Range_max_member Sample::* range_max_member)
{
    Vertex_layout layout;
    layout.stride = sizeof(Sample);
    layout.attributes = {
        {
            0,
            detail::vertex_attrib_type_for<Timestamp_member>(),
            1,
            detail::member_offset(timestamp_member),
            false
        },
        {
            1,
            detail::vertex_attrib_type_for<Value_member>(),
            1,
            detail::member_offset(value_member),
            false
        },
        {
            2,
            detail::vertex_attrib_type_for<Range_min_member>(),
            1,
            detail::member_offset(range_min_member),
            false
        },
        {
            3,
            detail::vertex_attrib_type_for<Range_max_member>(),
            1,
            detail::member_offset(range_max_member),
            false
        }
    };
    return layout;
}

template<typename Sample>
inline void apply_layout(
    Data_access_policy_typed<Sample>& policy,
    const Vertex_layout& layout)
{
    policy.layout_key = layout_key_for(layout);
    policy.setup_vertex_attributes = [layout]() {
        setup_vertex_attributes_for_layout(layout);
    };
}

template<typename Sample, typename Timestamp_member, typename Value_member>
inline Data_access_policy_typed<Sample> make_access_policy(
    Timestamp_member Sample::* timestamp_member,
    Value_member Sample::* value_member)
{
    Data_access_policy_typed<Sample> policy;
    policy.get_timestamp = [timestamp_member](const Sample& sample) {
        return static_cast<double>(sample.*timestamp_member);
    };
    policy.get_value = [value_member](const Sample& sample) {
        return static_cast<float>(sample.*value_member);
    };

    const Vertex_layout layout =
        make_standard_layout<Sample>(timestamp_member, value_member);
    apply_layout(policy, layout);
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
    policy.get_timestamp = [timestamp_member](const Sample& sample) {
        return static_cast<double>(sample.*timestamp_member);
    };
    policy.get_value = [value_member](const Sample& sample) {
        return static_cast<float>(sample.*value_member);
    };
    policy.get_range = [range_min_member, range_max_member](const Sample& sample) {
        const float low = static_cast<float>(sample.*range_min_member);
        const float high = static_cast<float>(sample.*range_max_member);
        return std::make_pair(low, high);
    };

    const Vertex_layout layout =
        make_standard_layout<Sample>(timestamp_member, value_member, range_min_member, range_max_member);
    apply_layout(policy, layout);
    return policy;
}

} // namespace vnm::plot
