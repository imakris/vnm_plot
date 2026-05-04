// vnm_plot typed API tests

#include "test_macros.h"

#include <vnm_plot/core/access_policy.h>
#include <vnm_plot/core/series_builder.h>
#include <vnm_plot/core/types.h>
#include <vnm_plot/core/vertex_layout.h>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>

namespace plot = vnm::plot;

namespace {

struct sample_t
{
    // Timestamps are int64 nanoseconds (API convention).
    std::int64_t t = 0;
    float v = 0.0f;
    std::int32_t pad = 0;
    float v_min = 0.0f;
    float v_max = 0.0f;
};

bool test_vertex_attrib_type_for_mappings()
{
    TEST_ASSERT(plot::detail::vertex_attrib_type_for<float>() == plot::Vertex_attrib_type::FLOAT32,
        "expected float to map to FLOAT32");
    TEST_ASSERT(plot::detail::vertex_attrib_type_for<double>() == plot::Vertex_attrib_type::FLOAT64,
        "expected double to map to FLOAT64");
    TEST_ASSERT(plot::detail::vertex_attrib_type_for<std::int32_t>() == plot::Vertex_attrib_type::INT32,
        "expected int32_t to map to INT32");
    TEST_ASSERT(plot::detail::vertex_attrib_type_for<int>() == plot::Vertex_attrib_type::INT32,
        "expected int to map to INT32");
    TEST_ASSERT(plot::detail::vertex_attrib_type_for<std::uint32_t>() == plot::Vertex_attrib_type::UINT32,
        "expected uint32_t to map to UINT32");
    TEST_ASSERT(plot::detail::vertex_attrib_type_for<unsigned int>() == plot::Vertex_attrib_type::UINT32,
        "expected unsigned int to map to UINT32");
    return true;
}

bool test_member_offset_matches_offsetof()
{
    TEST_ASSERT(plot::detail::member_offset(&sample_t::t) == offsetof(sample_t, t),
        "member_offset mismatch for t");
    TEST_ASSERT(plot::detail::member_offset(&sample_t::v) == offsetof(sample_t, v),
        "member_offset mismatch for v");
    TEST_ASSERT(plot::detail::member_offset(&sample_t::v_min) == offsetof(sample_t, v_min),
        "member_offset mismatch for v_min");
    TEST_ASSERT(plot::detail::member_offset(&sample_t::v_max) == offsetof(sample_t, v_max),
        "member_offset mismatch for v_max");
    return true;
}

bool test_layout_key_for_variations()
{
    plot::Vertex_layout layout;
    layout.stride = sizeof(sample_t);
    layout.attributes = {
        {0, plot::Vertex_attrib_type::FLOAT64, 1, 0, false},
        {1, plot::Vertex_attrib_type::FLOAT32, 1, 8, false}
    };

    const uint64_t key_a = plot::layout_key_for(layout);
    const uint64_t key_b = plot::layout_key_for(layout);
    TEST_ASSERT(key_a == key_b, "layout_key should be deterministic");

    plot::Vertex_layout layout_changed = layout;
    layout_changed.attributes[1].offset = 12;
    const uint64_t key_c = plot::layout_key_for(layout_changed);
    TEST_ASSERT(key_a != key_c, "layout_key should change when offsets change");

    return true;
}

bool test_make_access_policy_and_erase()
{
    auto policy = plot::make_access_policy<sample_t>(
        &sample_t::t,
        &sample_t::v,
        &sample_t::v_min,
        &sample_t::v_max);

    TEST_ASSERT(policy.is_valid(), "expected typed policy to be valid");

    sample_t s{};
    // 12.5 seconds in nanoseconds. The typed API works in int64 ns; this
    // single literal stands for what the previous double-keyed test wrote
    // as 12.5.
    constexpr std::int64_t k_test_ts_ns = 12'500'000'000;
    s.t = k_test_ts_ns;
    s.v = 3.5f;
    s.v_min = 1.0f;
    s.v_max = 6.0f;

    TEST_ASSERT(policy.get_timestamp(s) == k_test_ts_ns, "timestamp accessor mismatch");
    TEST_ASSERT(policy.get_value(s) == 3.5f, "value accessor mismatch");
    const auto range = policy.get_range(s);
    TEST_ASSERT(range.first == 1.0f && range.second == 6.0f, "range accessor mismatch");

    const plot::Vertex_layout expected_layout =
        plot::make_standard_layout<sample_t>(&sample_t::t, &sample_t::v, &sample_t::v_min, &sample_t::v_max);
    TEST_ASSERT(policy.layout_key == plot::layout_key_for(expected_layout),
        "layout_key mismatch for typed policy");

    const plot::Data_access_policy erased = policy.erase();
    TEST_ASSERT(erased.get_timestamp(&s) == k_test_ts_ns, "erased timestamp accessor mismatch");
    TEST_ASSERT(erased.get_value(&s) == 3.5f, "erased value accessor mismatch");
    const auto erased_range = erased.get_range(&s);
    TEST_ASSERT(erased_range.first == 1.0f && erased_range.second == 6.0f,
        "erased range accessor mismatch");
    TEST_ASSERT(static_cast<bool>(policy.clone_with_timestamp), "typed clone_with_timestamp should be set");
    TEST_ASSERT(static_cast<bool>(erased.clone_with_timestamp), "erased clone_with_timestamp should be set");

    return true;
}

bool test_clone_with_timestamp_for_both_overloads()
{
    sample_t src{};
    // Source timestamp == 100.25 s in nanoseconds.
    constexpr std::int64_t k_src_ts_ns = 100'250'000'000;
    src.t = k_src_ts_ns;
    src.v = 7.0f;
    src.v_min = 6.5f;
    src.v_max = 7.5f;
    src.pad = 17;

    {
        auto value_only = plot::make_access_policy<sample_t>(&sample_t::t, &sample_t::v);
        TEST_ASSERT(static_cast<bool>(value_only.clone_with_timestamp),
            "value-only overload should set clone_with_timestamp");

        sample_t dst{};
        // 130.5 s in nanoseconds.
        constexpr std::int64_t k_value_only_ts_ns = 130'500'000'000;
        value_only.clone_with_timestamp(dst, src, k_value_only_ts_ns);
        TEST_ASSERT(dst.t == k_value_only_ts_ns, "value-only clone should overwrite timestamp");
        TEST_ASSERT(dst.v == src.v, "value-only clone should copy value");
        TEST_ASSERT(dst.pad == src.pad, "value-only clone should copy pad");
        TEST_ASSERT(dst.v_min == src.v_min && dst.v_max == src.v_max,
            "value-only clone should copy remaining fields");
    }

    {
        auto with_range = plot::make_access_policy<sample_t>(
            &sample_t::t,
            &sample_t::v,
            &sample_t::v_min,
            &sample_t::v_max);
        TEST_ASSERT(static_cast<bool>(with_range.clone_with_timestamp),
            "range overload should set clone_with_timestamp");

        sample_t dst{};
        // 222.0 s in nanoseconds.
        constexpr std::int64_t k_range_ts_ns = 222'000'000'000;
        with_range.clone_with_timestamp(dst, src, k_range_ts_ns);
        TEST_ASSERT(dst.t == k_range_ts_ns, "range clone should overwrite timestamp");
        TEST_ASSERT(dst.v == src.v, "range clone should copy value");
        TEST_ASSERT(dst.pad == src.pad, "range clone should copy pad");
        TEST_ASSERT(dst.v_min == src.v_min && dst.v_max == src.v_max,
            "range clone should copy range fields");
    }

    return true;
}

bool test_ssbo_sample_layout_metadata()
{
    // Series_renderer trusts sample_stride_bytes / timestamp_offset_bytes /
    // value_offset_bytes for every non-DOTS draw: AREA needs them to set
    // up the next-sample attribute binding, and the SSBO-backed line and
    // colormap-line shaders use them as uniform indices into the points
    // VBO. A default-constructed policy must read as "missing" so the
    // renderer's runtime validation can reject it instead of silently
    // collapsing every sample to the first record.

    plot::Data_access_policy empty;
    TEST_ASSERT(empty.sample_stride_bytes == 0,
        "default Data_access_policy::sample_stride_bytes should be zero");
    TEST_ASSERT(empty.timestamp_offset_bytes == 0,
        "default Data_access_policy::timestamp_offset_bytes should be zero");
    TEST_ASSERT(empty.value_offset_bytes == 0,
        "default Data_access_policy::value_offset_bytes should be zero");

    // make_access_policy goes through apply_layout, which is the canonical
    // path for users that follow the typed API. The metadata must mirror
    // the underlying Vertex_layout so the renderer can locate sample i+1.
    auto policy = plot::make_access_policy<sample_t>(
        &sample_t::t,
        &sample_t::v,
        &sample_t::v_min,
        &sample_t::v_max);

    TEST_ASSERT(policy.sample_stride_bytes == sizeof(sample_t),
        "typed policy stride should match sizeof(sample_t)");
    TEST_ASSERT(policy.timestamp_offset_bytes == offsetof(sample_t, t),
        "typed policy timestamp offset should match offsetof(t)");
    TEST_ASSERT(policy.value_offset_bytes == offsetof(sample_t, v),
        "typed policy value offset should match offsetof(v)");

    const plot::Data_access_policy erased = policy.erase();
    TEST_ASSERT(erased.sample_stride_bytes == policy.sample_stride_bytes,
        "erase() should propagate sample_stride_bytes");
    TEST_ASSERT(erased.timestamp_offset_bytes == policy.timestamp_offset_bytes,
        "erase() should propagate timestamp_offset_bytes");
    TEST_ASSERT(erased.value_offset_bytes == policy.value_offset_bytes,
        "erase() should propagate value_offset_bytes");

    // Apply a hand-built Vertex_layout to verify apply_layout reads the
    // location-0 timestamp slot and location-1 value slot, not whichever
    // attribute happens to come first.
    plot::Vertex_layout custom_layout;
    custom_layout.stride = 32;
    custom_layout.attributes = {
        {1, plot::Vertex_attrib_type::FLOAT32, 1, 16, false},
        {0, plot::Vertex_attrib_type::FLOAT64, 1, 4,  false}
    };
    plot::Data_access_policy_typed<sample_t> custom_policy;
    plot::apply_layout(custom_policy, custom_layout);
    TEST_ASSERT(custom_policy.sample_stride_bytes == 32,
        "apply_layout should set stride from Vertex_layout::stride");
    TEST_ASSERT(custom_policy.timestamp_offset_bytes == 4,
        "apply_layout should pull timestamp offset from location 0");
    TEST_ASSERT(custom_policy.value_offset_bytes == 16,
        "apply_layout should pull value offset from location 1");

    return true;
}

bool test_series_builder_preview_config()
{
    auto main_source = std::make_shared<plot::Vector_data_source<sample_t>>();
    auto preview_source = std::make_shared<plot::Vector_data_source<sample_t>>();
    auto policy = plot::make_access_policy<sample_t>(&sample_t::t, &sample_t::v, &sample_t::v_min, &sample_t::v_max);

    plot::preview_config_t preview_cfg;
    preview_cfg.data_source = preview_source;
    preview_cfg.access = policy.erase();
    preview_cfg.style = plot::Display_style::AREA;

    auto series = plot::Series_builder()
        .enabled(false)
        .style(plot::Display_style::LINE)
        .empty_window_behavior(plot::Empty_window_behavior::HOLD_LAST_FORWARD)
        .data_source(main_source)
        .access(policy)
        .preview(preview_cfg)
        .build_value();

    TEST_ASSERT(series.preview_config.has_value(), "expected preview_config to be set");
    TEST_ASSERT(series.preview_config->data_source.get() == preview_source.get(),
        "preview data_source mismatch");
    TEST_ASSERT(series.preview_config->style && *series.preview_config->style == plot::Display_style::AREA,
        "preview style mismatch");
    TEST_ASSERT(series.empty_window_behavior == plot::Empty_window_behavior::HOLD_LAST_FORWARD,
        "empty_window_behavior mismatch");

    return true;
}

} // namespace

int main()
{
    std::cout << "Typed API tests" << std::endl;

    int passed = 0;
    int failed = 0;

    RUN_TEST(test_vertex_attrib_type_for_mappings);
    RUN_TEST(test_member_offset_matches_offsetof);
    RUN_TEST(test_layout_key_for_variations);
    RUN_TEST(test_make_access_policy_and_erase);
    RUN_TEST(test_clone_with_timestamp_for_both_overloads);
    RUN_TEST(test_ssbo_sample_layout_metadata);
    RUN_TEST(test_series_builder_preview_config);

    std::cout << "Results: " << passed << " passed, " << failed << " failed" << std::endl;
    return failed > 0 ? 1 : 0;
}
