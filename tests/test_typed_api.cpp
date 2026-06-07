// vnm_plot typed API tests

#include "test_macros.h"

#include <vnm_plot/core/access_policy.h>
#include <vnm_plot/core/series_builder.h>
#include <vnm_plot/core/types.h>

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

    return true;
}

bool test_erased_access_view_uses_direct_member_accessors()
{
    auto policy = plot::make_access_policy<sample_t>(
        &sample_t::t,
        &sample_t::v,
        &sample_t::v_min,
        &sample_t::v_max);

    sample_t s{};
    constexpr std::int64_t k_sample_timestamp_ns = 15'000'000'000;
    s.t = k_sample_timestamp_ns;
    s.v = 3.5f;
    s.v_min = 1.25f;
    s.v_max = 5.5f;

    const plot::Data_access_policy erased = policy.erase();
    const auto direct_view = plot::detail::make_erased_access_policy_view(erased);
    TEST_ASSERT(
        direct_view.dispatch_kind == plot::detail::access_dispatch_kind_t::MEMBER_POINTER,
        "member-pointer policy should expose a direct erased access view");
    TEST_ASSERT(direct_view.timestamp(&s) == k_sample_timestamp_ns,
        "direct erased timestamp accessor mismatch");
    TEST_ASSERT(direct_view.value(&s) == 3.5f,
        "direct erased value accessor mismatch");
    const auto direct_range = direct_view.range(&s);
    TEST_ASSERT(direct_range.first == 1.25f && direct_range.second == 5.5f,
        "direct erased range accessor mismatch");

    int timestamp_calls = 0;
    plot::Data_access_policy fallback;
    fallback.get_timestamp = [&timestamp_calls](const void* sample) {
        ++timestamp_calls;
        return static_cast<const sample_t*>(sample)->t;
    };
    fallback.get_value = [](const void* sample) {
        return static_cast<const sample_t*>(sample)->v;
    };
    const auto fallback_view =
        plot::detail::make_erased_access_policy_view(fallback);
    TEST_ASSERT(
        fallback_view.dispatch_kind == plot::detail::access_dispatch_kind_t::STD_FUNCTION,
        "capturing callable policy should expose a std::function fallback view");
    TEST_ASSERT(fallback_view.timestamp(&s) == k_sample_timestamp_ns,
        "fallback erased timestamp accessor mismatch");
    TEST_ASSERT(timestamp_calls == 1,
        "fallback erased accessor should invoke the public callable once");

    plot::Data_access_policy mutated = erased;
    int mutated_timestamp_calls = 0;
    int mutated_value_calls = 0;
    int mutated_range_calls = 0;
    constexpr std::int64_t k_replacement_timestamp_ns = 21'000'000'000;
    mutated.get_timestamp = [&mutated_timestamp_calls](const void*) {
        ++mutated_timestamp_calls;
        return k_replacement_timestamp_ns;
    };
    mutated.get_value = [&mutated_value_calls](const void*) {
        ++mutated_value_calls;
        return 9.25f;
    };
    mutated.get_range = [&mutated_range_calls](const void*) {
        ++mutated_range_calls;
        return std::make_pair(7.0f, 8.0f);
    };

    const auto mutated_view =
        plot::detail::make_erased_access_policy_view(mutated);
    TEST_ASSERT(
        mutated_view.dispatch_kind == plot::detail::access_dispatch_kind_t::STD_FUNCTION,
        "mutating an erased direct policy should clear member-pointer dispatch");
    TEST_ASSERT(mutated_view.timestamp(&s) == k_replacement_timestamp_ns,
        "mutated erased timestamp accessor should use the replacement callable");
    TEST_ASSERT(mutated_view.value(&s) == 9.25f,
        "mutated erased value accessor should use the replacement callable");
    const auto mutated_range = mutated_view.range(&s);
    TEST_ASSERT(mutated_range.first == 7.0f && mutated_range.second == 8.0f,
        "mutated erased range accessor should use the replacement callable");
    TEST_ASSERT(mutated_timestamp_calls == 1 &&
            mutated_value_calls == 1 &&
            mutated_range_calls == 1,
        "mutated erased policy should invoke each replacement callable once");

    plot::Data_access_policy slot_source;
    int slot_source_timestamp_calls = 0;
    slot_source.get_timestamp = [&slot_source_timestamp_calls](const void*) {
        ++slot_source_timestamp_calls;
        return std::int64_t{22'000'000'000};
    };
    slot_source.get_value = [](const void*) {
        return 10.5f;
    };

    plot::Data_access_policy slot_assigned = erased;
    slot_assigned.get_timestamp = slot_source.get_timestamp;
    slot_assigned.get_value = slot_source.get_value;
    const auto slot_assigned_view =
        plot::detail::make_erased_access_policy_view(slot_assigned);
    TEST_ASSERT(
        slot_assigned_view.dispatch_kind ==
            plot::detail::access_dispatch_kind_t::STD_FUNCTION,
        "assigning accessor slots should clear member-pointer dispatch");
    TEST_ASSERT(slot_assigned_view.timestamp(&s) == std::int64_t{22'000'000'000},
        "slot-assigned policy should use the replacement timestamp slot");
    TEST_ASSERT(slot_assigned_view.value(&s) == 10.5f,
        "slot-assigned policy should use the replacement value slot");
    TEST_ASSERT(slot_source_timestamp_calls == 1,
        "slot-assigned policy should invoke the replacement timestamp slot");

    auto detached_timestamp_slot = erased.get_timestamp;
    detached_timestamp_slot = [](const void*) {
        return std::int64_t{23'000'000'000};
    };
    const auto after_detached_slot_mutation_view =
        plot::detail::make_erased_access_policy_view(erased);
    TEST_ASSERT(
        after_detached_slot_mutation_view.dispatch_kind ==
            plot::detail::access_dispatch_kind_t::MEMBER_POINTER,
        "mutating a detached accessor copy must not clear the source policy");
    TEST_ASSERT(after_detached_slot_mutation_view.timestamp(&s) ==
            k_sample_timestamp_ns,
        "detached accessor mutation must not affect source policy semantics");

    plot::Data_access_policy move_source = erased;
    auto moved_timestamp_slot = std::move(move_source.get_timestamp);
    (void)moved_timestamp_slot;
    const auto after_slot_move_view =
        plot::detail::make_erased_access_policy_view(move_source);
    TEST_ASSERT(
        after_slot_move_view.dispatch_kind !=
            plot::detail::access_dispatch_kind_t::MEMBER_POINTER,
        "moving an accessor slot out should clear the source policy fast path");

    plot::Data_access_policy whole_move_source = erased;
    plot::Data_access_policy whole_move_destination =
        std::move(whole_move_source);
    const auto after_whole_policy_move_view =
        plot::detail::make_erased_access_policy_view(whole_move_destination);
    TEST_ASSERT(
        after_whole_policy_move_view.dispatch_kind ==
            plot::detail::access_dispatch_kind_t::MEMBER_POINTER,
        "moving a whole policy should preserve destination member-pointer dispatch");
    TEST_ASSERT(after_whole_policy_move_view.timestamp(&s) ==
            k_sample_timestamp_ns,
        "whole-policy move should preserve direct timestamp semantics");

    auto typed_mutated = policy;
    int typed_mutated_timestamp_calls = 0;
    typed_mutated.get_timestamp = [&typed_mutated_timestamp_calls](
        const sample_t& sample) {
        ++typed_mutated_timestamp_calls;
        return sample.t + std::int64_t{1};
    };
    const plot::Data_access_policy erased_typed_mutated =
        typed_mutated.erase();
    const auto typed_mutated_view =
        plot::detail::make_erased_access_policy_view(erased_typed_mutated);
    TEST_ASSERT(
        typed_mutated_view.dispatch_kind ==
            plot::detail::access_dispatch_kind_t::STD_FUNCTION,
        "mutating a typed policy should clear member-pointer dispatch before erase");
    TEST_ASSERT(typed_mutated_view.timestamp(&s) == k_sample_timestamp_ns + 1,
        "typed policy mutation should survive erase through std::function dispatch");
    TEST_ASSERT(typed_mutated_timestamp_calls == 1,
        "typed policy mutation should invoke the replacement callable");

    plot::Data_access_policy revision_policy = erased;
    const auto revision_view_before =
        plot::detail::make_erased_access_policy_view(revision_policy);
    const auto revision_key_before =
        plot::detail::make_access_policy_cache_key(
            &revision_policy,
            revision_view_before);
    revision_policy.get_value = [](const void*) {
        return 11.0f;
    };
    const auto revision_view_after =
        plot::detail::make_erased_access_policy_view(revision_policy);
    const auto revision_key_after =
        plot::detail::make_access_policy_cache_key(
            &revision_policy,
            revision_view_after);
    TEST_ASSERT(revision_key_before != revision_key_after,
        "access policy cache key should change after accessor mutation");

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

    // Erased policy must propagate the same conversion.
    const plot::Data_access_policy erased = policy.erase();
    TEST_ASSERT(erased.get_timestamp(&s) == k_expected_ns,
        "erased fp timestamp accessor mismatch");

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
    preview_cfg.interpolation = plot::Series_interpolation::LINEAR;

    auto series = plot::Series_builder()
        .enabled(false)
        .style(plot::Display_style::LINE)
        .interpolation(plot::Series_interpolation::STEP_AFTER)
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
    TEST_ASSERT(series.interpolation == plot::Series_interpolation::STEP_AFTER,
        "series interpolation mismatch");
    TEST_ASSERT(
        series.preview_config->interpolation &&
            *series.preview_config->interpolation == plot::Series_interpolation::LINEAR,
        "preview interpolation mismatch");
    TEST_ASSERT(series.effective_preview_interpolation() == plot::Series_interpolation::LINEAR,
        "effective preview interpolation mismatch");
    TEST_ASSERT(!series.preview_matches_main(),
        "preview should not match main when interpolation/style/source differ");
    TEST_ASSERT(series.empty_window_behavior == plot::Empty_window_behavior::HOLD_LAST_FORWARD,
        "empty_window_behavior mismatch");

    return true;
}

bool test_series_builder_default_interpolation_is_linear()
{
    auto series = plot::Series_builder().build_value();

    TEST_ASSERT(series.interpolation == plot::Series_interpolation::LINEAR,
        "default series interpolation should be linear");
    TEST_ASSERT(series.effective_preview_interpolation() == plot::Series_interpolation::LINEAR,
        "default preview interpolation should be linear");

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
    RUN_TEST(test_erased_access_view_uses_direct_member_accessors);
    RUN_TEST(test_typed_api_floating_point_timestamp_member);
    RUN_TEST(test_series_builder_preview_config);
    RUN_TEST(test_series_builder_default_interpolation_is_linear);

    std::cout << "Results: " << passed << " passed, " << failed << " failed" << std::endl;
    return failed > 0 ? 1 : 0;
}
