// vnm_plot typed API tests

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
    double t = 0.0;
    float v = 0.0f;
    std::int32_t pad = 0;
    float v_min = 0.0f;
    float v_max = 0.0f;
};

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL: " << msg << " (line " << __LINE__ << ")" << std::endl; \
            return false; \
        } \
    } while (0)

#define RUN_TEST(test_fn) \
    do { \
        std::cout << "Running " << #test_fn << "... "; \
        if (test_fn()) { \
            std::cout << "OK" << std::endl; \
            ++passed; \
        } else { \
            std::cout << "FAIL" << std::endl; \
            ++failed; \
        } \
    } while (0)

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
    s.t = 12.5;
    s.v = 3.5f;
    s.v_min = 1.0f;
    s.v_max = 6.0f;

    TEST_ASSERT(policy.get_timestamp(s) == 12.5, "timestamp accessor mismatch");
    TEST_ASSERT(policy.get_value(s) == 3.5f, "value accessor mismatch");
    const auto range = policy.get_range(s);
    TEST_ASSERT(range.first == 1.0f && range.second == 6.0f, "range accessor mismatch");

    const plot::Vertex_layout expected_layout =
        plot::make_standard_layout<sample_t>(&sample_t::t, &sample_t::v, &sample_t::v_min, &sample_t::v_max);
    TEST_ASSERT(policy.layout_key == plot::layout_key_for(expected_layout),
        "layout_key mismatch for typed policy");

    const plot::Data_access_policy erased = policy.erase();
    TEST_ASSERT(erased.get_timestamp(&s) == 12.5, "erased timestamp accessor mismatch");
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
    src.t = 100.25;
    src.v = 7.0f;
    src.v_min = 6.5f;
    src.v_max = 7.5f;
    src.pad = 17;

    {
        auto value_only = plot::make_access_policy<sample_t>(&sample_t::t, &sample_t::v);
        TEST_ASSERT(static_cast<bool>(value_only.clone_with_timestamp),
            "value-only overload should set clone_with_timestamp");

        sample_t dst{};
        value_only.clone_with_timestamp(dst, src, 130.5);
        TEST_ASSERT(dst.t == 130.5, "value-only clone should overwrite timestamp");
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
        with_range.clone_with_timestamp(dst, src, 222.0);
        TEST_ASSERT(dst.t == 222.0, "range clone should overwrite timestamp");
        TEST_ASSERT(dst.v == src.v, "range clone should copy value");
        TEST_ASSERT(dst.pad == src.pad, "range clone should copy pad");
        TEST_ASSERT(dst.v_min == src.v_min && dst.v_max == src.v_max,
            "range clone should copy range fields");
    }

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
    RUN_TEST(test_series_builder_preview_config);

    std::cout << "Results: " << passed << " passed, " << failed << " failed" << std::endl;
    return failed > 0 ? 1 : 0;
}
