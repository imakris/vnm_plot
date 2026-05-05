// vnm_plot typed API tests

#include "test_macros.h"

#include <vnm_plot/core/access_policy.h>
#include <vnm_plot/core/function_sample.h>
#include <vnm_plot/core/series_builder.h>
#include <vnm_plot/core/types.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>

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

bool test_layout_key_distinguishes_sample_types()
{
    // Cache key is the only renderer-visible identifier of a user sample
    // type now that the byte-level layout is no longer surfaced. Two
    // structurally different sample shapes must produce distinct, non-zero
    // keys so the renderer's caches do not collide across types.
    struct other_sample_t
    {
        std::int64_t t = 0;
        float v = 0.0f;
    };

    const auto policy_a = plot::make_access_policy<sample_t>(
        &sample_t::t, &sample_t::v, &sample_t::v_min, &sample_t::v_max);
    const auto policy_b = plot::make_access_policy<other_sample_t>(
        &other_sample_t::t, &other_sample_t::v);

    TEST_ASSERT(policy_a.layout_key != 0, "layout_key should be non-zero");
    TEST_ASSERT(policy_b.layout_key != 0, "layout_key should be non-zero");
    TEST_ASSERT(policy_a.layout_key != policy_b.layout_key,
        "layout_key should differ for differently shaped sample types");

    const auto policy_a2 = plot::make_access_policy<sample_t>(
        &sample_t::t, &sample_t::v, &sample_t::v_min, &sample_t::v_max);
    TEST_ASSERT(policy_a.layout_key == policy_a2.layout_key,
        "layout_key should be deterministic for the same sample shape");

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

    TEST_ASSERT(policy.layout_key != 0,
        "make_access_policy should populate a non-zero layout_key");

    const plot::Data_access_policy erased = policy.erase();
    TEST_ASSERT(erased.layout_key == policy.layout_key,
        "erase() should propagate layout_key");
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

bool test_typed_api_floating_point_timestamp_member()
{
    // Some legacy / convenience sample types store the timestamp as a
    // floating-point value in seconds. The vnm_plot API works in int64
    // nanoseconds, so the typed API must auto-convert at the boundary
    // instead of silently truncating with static_cast<int64_t>(double).
    //
    // The original failure mode this test guards against: a Sample with
    // double t = 12.5 (seconds) used to produce get_timestamp() == 12 (ns)
    // because of a blind static_cast. Anything that constructed a view
    // window in seconds (-10 .. 10) ended up with a 20-ns view instead.

    struct fp_sample_t
    {
        double t_seconds = 0.0;
        float  v = 0.0f;
    };

    auto policy = plot::make_access_policy<fp_sample_t>(
        &fp_sample_t::t_seconds,
        &fp_sample_t::v);

    fp_sample_t s{};
    s.t_seconds = 12.5;
    s.v = 3.5f;

    // Forward direction: seconds -> int64 ns.
    constexpr std::int64_t k_expected_ns = 12'500'000'000;
    TEST_ASSERT(policy.get_timestamp(s) == k_expected_ns,
        std::string("expected fp timestamp seconds to convert to ns; got ") +
            std::to_string(policy.get_timestamp(s)));

    // Reverse direction: int64 ns written through clone_with_timestamp
    // lands back in the floating-point member as seconds.
    fp_sample_t dst{};
    constexpr std::int64_t k_clone_ns = -7'250'000'000;
    policy.clone_with_timestamp(dst, s, k_clone_ns);
    const double k_expected_seconds = -7.25;
    TEST_ASSERT(std::abs(dst.t_seconds - k_expected_seconds) < 1e-9,
        std::string("expected ns -> fp seconds round-trip; got ") +
            std::to_string(dst.t_seconds));

    // Erased policy must propagate the same conversion.
    const plot::Data_access_policy erased = policy.erase();
    TEST_ASSERT(erased.get_timestamp(&s) == k_expected_ns,
        "erased fp timestamp accessor mismatch");

    return true;
}

bool test_function_sample_layout_key_matches_typed_factory()
{
    // Contract: make_function_sample_policy_typed().layout_key must equal
    // function_sample_layout_key() so renderer-internal caches recognize the
    // typed factory consistently.
    const auto policy = plot::make_function_sample_policy_typed();
    TEST_ASSERT(policy.layout_key == plot::function_sample_layout_key(),
        "make_function_sample_policy_typed().layout_key must equal "
        "function_sample_layout_key()");
    TEST_ASSERT(policy.layout_key != 0,
        "function_sample layout_key should be non-zero");

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

    RUN_TEST(test_member_offset_matches_offsetof);
    RUN_TEST(test_layout_key_distinguishes_sample_types);
    RUN_TEST(test_make_access_policy_and_erase);
    RUN_TEST(test_clone_with_timestamp_for_both_overloads);
    RUN_TEST(test_typed_api_floating_point_timestamp_member);
    RUN_TEST(test_function_sample_layout_key_matches_typed_factory);
    RUN_TEST(test_series_builder_preview_config);

    std::cout << "Results: " << passed << " passed, " << failed << " failed" << std::endl;
    return failed > 0 ? 1 : 0;
}
