// vnm_plot core algorithm tests

#include "test_macros.h"

#include <vnm_plot/core/algo.h>
#include <vnm_plot/core/plot_config.h>
#include <vnm_plot/core/text_renderer.h>
#include <vnm_plot/core/time_units.h>
#include <vnm_plot/core/types.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <type_traits>
#include <vector>

namespace plot = vnm::plot;

static_assert(
    std::is_same_v<plot::Text_renderer::vertical_axis_fade_tracker_t::key_type, double>,
    "vertical label fade tracking remains keyed by double values");
static_assert(
    std::is_same_v<plot::Text_renderer::horizontal_axis_fade_tracker_t::key_type, std::int64_t>,
    "horizontal label fade tracking must be keyed by int64 nanosecond timestamps");

namespace {

struct sample_t
{
    // Timestamps are int64 nanoseconds (API convention).
    std::int64_t t = 0;
    float v = 0.0f;
};

class Lod_source final : public plot::Data_source
{
public:
    explicit Lod_source(std::vector<std::size_t> scales)
        : m_scales(std::move(scales))
    {}

    plot::snapshot_result_t try_snapshot(std::size_t /*lod*/) override
    {
        return {plot::data_snapshot_t{}, plot::snapshot_result_t::Snapshot_status::EMPTY};
    }

    std::size_t lod_levels() const override { return m_scales.size(); }
    std::size_t lod_scale(std::size_t level) const override
    {
        return (level < m_scales.size()) ? m_scales[level] : 1;
    }
    std::size_t sample_stride() const override { return sizeof(sample_t); }

private:
    std::vector<std::size_t> m_scales;
};

bool test_choose_lod_level_picks_closest_pps()
{
    const std::vector<std::size_t> scales = {1, 4, 16, 64, 256, 1024};
    TEST_ASSERT(plot::detail::choose_lod_level(scales, 0.01) == 3,
        "expected choose_lod_level to pick scale 64");
    return true;
}

bool test_choose_lod_level_handles_degenerate_inputs()
{
    TEST_ASSERT(plot::detail::choose_lod_level({}, 1.0) == 0,
        "empty scales should yield level 0");
    TEST_ASSERT(plot::detail::choose_lod_level({1, 2, 4}, 0.0) == 0,
        "non-positive base_pps should yield level 0");
    TEST_ASSERT(plot::detail::choose_lod_level({1, 2, 4}, -1.0) == 0,
        "negative base_pps should yield level 0");
    return true;
}

bool test_compute_lod_scales_forces_minimum_of_one()
{
    class Zero_scale_source final : public plot::Data_source
    {
    public:
        plot::snapshot_result_t try_snapshot(std::size_t) override
        {
            return {plot::data_snapshot_t{}, plot::snapshot_result_t::Snapshot_status::EMPTY};
        }
        std::size_t lod_levels() const override { return 3; }
        std::size_t lod_scale(std::size_t level) const override
        {
            return (level == 0) ? 0 : (level == 1 ? 1 : 0);
        }
        std::size_t sample_stride() const override { return sizeof(sample_t); }
    };

    Zero_scale_source src;
    auto scales = plot::detail::compute_lod_scales(src);
    TEST_ASSERT(scales.size() == 3, "lod_levels should give 3 entries");
    TEST_ASSERT(scales[0] == 1, "zero scale at level 0 should be clamped to 1");
    TEST_ASSERT(scales[1] == 1, "unit scale at level 1 should survive");
    TEST_ASSERT(scales[2] == 1, "zero scale at level 2 should be clamped to 1");
    return true;
}

bool test_get_shift_handles_invalid_inputs()
{
    const double inf = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();

    TEST_ASSERT(plot::detail::get_shift(0.0, 10.0) == 0.0,
        "zero section size should produce zero shift");
    TEST_ASSERT(plot::detail::get_shift(-1.0, 10.0) == 0.0,
        "negative section size should produce zero shift");
    TEST_ASSERT(plot::detail::get_shift(inf, 10.0) == 0.0,
        "infinite section size should produce zero shift");
    TEST_ASSERT(plot::detail::get_shift(nan, 10.0) == 0.0,
        "NaN section size should produce zero shift");
    TEST_ASSERT(plot::detail::get_shift(10.0, inf) == 0.0,
        "infinite minimum value should produce zero shift");
    TEST_ASSERT(plot::detail::get_shift(10.0, nan) == 0.0,
        "NaN minimum value should produce zero shift");
    TEST_ASSERT(std::abs(plot::detail::get_shift(10.0, 23.0) - 7.0) < 1e-12,
        "positive finite get_shift behavior should be preserved");

    return true;
}

bool test_decimal_precision_helpers_reject_invalid_scaled_values()
{
    const double inf = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();

    TEST_ASSERT(!plot::detail::any_fractional_at_precision({1.0, 2.0}, 1),
        "integer values should not require fractional precision");
    TEST_ASSERT(plot::detail::any_fractional_at_precision({1.0, 2.5}, 1),
        "ordinary fractional values should still be detected");
    TEST_ASSERT(!plot::detail::any_fractional_at_precision({inf}, 1),
        "infinite values should be rejected cleanly");
    TEST_ASSERT(!plot::detail::any_fractional_at_precision({nan}, 1),
        "NaN values should be rejected cleanly");
    TEST_ASSERT(!plot::detail::any_fractional_at_precision(
            {std::numeric_limits<double>::max()},
            1),
        "values whose scaled form exceeds the safe integer range should be rejected");
    TEST_ASSERT(!plot::detail::any_fractional_at_precision({1.25}, 400),
        "overflowing precision scale should be rejected cleanly");

    return true;
}

bool test_lower_and_upper_bound_on_contiguous_buffer()
{
    // Sample timestamps are int64 nanoseconds; the test uses small ordinals
    // (0..9 ns) for clarity. Query values are also nanosecond integers.
    std::vector<sample_t> samples;
    for (int i = 0; i < 10; ++i) {
        samples.push_back({static_cast<std::int64_t>(i), static_cast<float>(i)});
    }

    const auto get_ts = [](const void* p) -> std::int64_t {
        return static_cast<const sample_t*>(p)->t;
    };

    // The original "3.5" probe has no integer-only equivalent; use 4 as the
    // first timestamp >= 3.5, which is what lower_bound is meant to find.
    TEST_ASSERT(
        plot::detail::lower_bound_timestamp(samples.data(), samples.size(), sizeof(sample_t), get_ts, std::int64_t{4}) == 4,
        "lower_bound(4) should land on index 4");
    TEST_ASSERT(
        plot::detail::lower_bound_timestamp(samples.data(), samples.size(), sizeof(sample_t), get_ts, std::int64_t{3}) == 3,
        "lower_bound(3) should land on index 3");
    TEST_ASSERT(
        plot::detail::upper_bound_timestamp(samples.data(), samples.size(), sizeof(sample_t), get_ts, std::int64_t{3}) == 4,
        "upper_bound(3) should land on index 4");
    TEST_ASSERT(
        plot::detail::lower_bound_timestamp(samples.data(), samples.size(), sizeof(sample_t), get_ts, std::int64_t{-5}) == 0,
        "lower_bound below range should be 0");
    TEST_ASSERT(
        plot::detail::lower_bound_timestamp(samples.data(), samples.size(), sizeof(sample_t), get_ts, std::int64_t{100})
            == samples.size(),
        "lower_bound above range should be count");
    return true;
}

bool test_timestamp_search_rejects_null_samples()
{
    std::vector<sample_t> samples;
    for (int i = 0; i < 4; ++i) {
        samples.push_back({static_cast<std::int64_t>(i), static_cast<float>(i)});
    }

    const auto get_ts = [](const void* p) -> std::int64_t {
        return static_cast<const sample_t*>(p)->t;
    };
    const auto result = plot::detail::bsearch_ts_impl(
        samples.size(),
        [&samples](std::size_t i) -> const void* {
            return i == 2 ? nullptr : static_cast<const void*>(&samples[i]);
        },
        get_ts,
        [](std::int64_t ts, std::int64_t t) { return ts < t; },
        std::int64_t{3});

    TEST_ASSERT(!result,
        "timestamp binary search should fail explicitly when addressing returns nullptr");

    return true;
}

bool test_raw_timestamp_search_rejects_zero_stride()
{
    std::vector<sample_t> samples = {
        {0, 0.0f},
        {1, 1.0f},
        {2, 2.0f}
    };

    int get_timestamp_calls = 0;
    const auto get_ts = [&get_timestamp_calls](const void* p) -> std::int64_t {
        ++get_timestamp_calls;
        return static_cast<const sample_t*>(p)->t;
    };

    TEST_ASSERT(plot::detail::lower_bound_timestamp(
            samples.data(),
            samples.size(),
            0,
            get_ts,
            std::int64_t{1}) == 0,
        "raw lower_bound should reject stride-zero input");
    TEST_ASSERT(plot::detail::upper_bound_timestamp(
            samples.data(),
            samples.size(),
            0,
            get_ts,
            std::int64_t{1}) == 0,
        "raw upper_bound should reject stride-zero input");
    TEST_ASSERT(get_timestamp_calls == 0,
        "stride-zero search should not call the timestamp accessor");

    return true;
}

bool test_bounds_on_segmented_snapshot()
{
    std::vector<sample_t> tail;
    std::vector<sample_t> head;
    for (int i = 0; i < 6; ++i) {
        tail.push_back({static_cast<std::int64_t>(i), 0.0f});
    }
    for (int i = 6; i < 10; ++i) {
        head.push_back({static_cast<std::int64_t>(i), 0.0f});
    }

    plot::data_snapshot_t snap;
    snap.data = tail.data();
    snap.count = 10;
    snap.stride = sizeof(sample_t);
    snap.data2 = head.data();
    snap.count2 = head.size();

    const auto get_ts = [](const void* p) -> std::int64_t {
        return static_cast<const sample_t*>(p)->t;
    };

    TEST_ASSERT(get_ts(snap.at(0)) == 0, "segmented at(0) should be 0");
    TEST_ASSERT(get_ts(snap.at(5)) == 5, "segmented at(5) should be 5");
    TEST_ASSERT(get_ts(snap.at(6)) == 6, "segmented at(6) should cross into second segment");
    TEST_ASSERT(get_ts(snap.at(9)) == 9, "segmented at(9) should be 9");
    // Original "6.5" probe equivalent: first index with timestamp >= 7 is 7.
    TEST_ASSERT(plot::detail::lower_bound_timestamp(snap, get_ts, std::int64_t{7}) == 7,
        "segmented lower_bound(7) should land on index 7");
    TEST_ASSERT(plot::detail::upper_bound_timestamp(snap, get_ts, std::int64_t{6}) == 7,
        "segmented upper_bound(6) should land on index 7");
    return true;
}

bool test_snapshot_truthiness_requires_usable_stride()
{
    std::vector<sample_t> samples = {{0, 1.0f}};

    plot::data_snapshot_t snap;
    snap.data = samples.data();
    snap.count = samples.size();
    snap.stride = 0;

    TEST_ASSERT(!snap.is_valid(), "stride-zero snapshot should be invalid");
    TEST_ASSERT(!static_cast<bool>(snap), "stride-zero snapshot should be falsey");

    plot::snapshot_result_t result{
        snap,
        plot::snapshot_result_t::Snapshot_status::READY
    };
    TEST_ASSERT(!static_cast<bool>(result),
        "READY snapshot_result_t should be falsey when snapshot is unusable");

    return true;
}

bool test_snapshot_count1_clamps_malformed_count2()
{
    std::vector<sample_t> first = {{0, 1.0f}};
    std::vector<sample_t> second = {{1, 2.0f}};

    plot::data_snapshot_t snap;
    snap.data = first.data();
    snap.count = first.size();
    snap.stride = sizeof(sample_t);
    snap.data2 = second.data();
    snap.count2 = 2;

    TEST_ASSERT(snap.count1() == 0,
        "malformed count2 > count should clamp count1 to zero");
    TEST_ASSERT(!snap.is_valid(), "count2 > count should be invalid");
    TEST_ASSERT(!static_cast<bool>(snap), "count2 > count should be falsey");
    TEST_ASSERT(snap.at(0) == nullptr,
        "count2 > count should make sample access fail explicitly");

    return true;
}

bool test_layout_cache_key_distinguishes_adjacent_int64_time_windows()
{
    // The horizontal-axis label cache keys layouts by t0/t1 in nanoseconds.
    // Two windows that differ by a single nanosecond must be treated as
    // distinct keys, otherwise the layout cache returns stale axis labels
    // when the user pans or zooms by a sub-step amount. Before the int64
    // migration, t0/t1 lived as doubles; adjacent ns-resolution windows
    // collided in the cache because doubles cannot hold modern absolute
    // timestamps with single-nanosecond precision.

    plot::layout_cache_key_t base{};
    base.v0 = 0.0f;
    base.v1 = 1.0f;
    base.t0 = 1'700'000'000'000'000'000LL; // ~late 2023 in ns since epoch
    base.t1 = base.t0 + 60LL * 1'000'000'000LL; // 60-second window
    base.viewport_size = plot::Size_2i{1024, 768};
    base.adjusted_reserved_height     = 24.0;
    base.adjusted_preview_height      = 32.0;
    base.adjusted_font_size_in_pixels = 13.5;
    base.vbar_width_pixels            = 56.0;
    base.font_metrics_key             = 0xfeedface;

    // Adjacent t0 windows (differ by 1 ns) must compare unequal.
    plot::layout_cache_key_t shifted_t0 = base;
    shifted_t0.t0 = base.t0 + 1;
    TEST_ASSERT(!(base == shifted_t0),
        "layout_cache_key_t must distinguish t0 windows that differ by 1 ns");

    // Adjacent t1 windows (differ by 1 ns) must compare unequal.
    plot::layout_cache_key_t shifted_t1 = base;
    shifted_t1.t1 = base.t1 + 1;
    TEST_ASSERT(!(base == shifted_t1),
        "layout_cache_key_t must distinguish t1 windows that differ by 1 ns");

    // Identical keys still equal (sanity).
    plot::layout_cache_key_t copy = base;
    TEST_ASSERT(base == copy,
        "layout_cache_key_t equality must hold for identical fields");

    // INT64_MAX-adjacent windows: the boundary case where double-typed
    // fields would lose precision entirely.
    plot::layout_cache_key_t high_a = base;
    plot::layout_cache_key_t high_b = base;
    high_a.t0 = std::numeric_limits<std::int64_t>::max() - 1;
    high_a.t1 = std::numeric_limits<std::int64_t>::max();
    high_b.t0 = std::numeric_limits<std::int64_t>::max() - 2;
    high_b.t1 = std::numeric_limits<std::int64_t>::max() - 1;
    TEST_ASSERT(!(high_a == high_b),
        "layout_cache_key_t must distinguish adjacent ns windows even "
        "near INT64_MAX where double fields would lose precision");

    return true;
}

bool test_format_axis_fixed_or_int()
{
    TEST_ASSERT(plot::format_axis_fixed_or_int(0.0, 0) == "0", "zero as integer");
    TEST_ASSERT(plot::format_axis_fixed_or_int(3.0, 0) == "3", "integer rounding");
    TEST_ASSERT(plot::format_axis_fixed_or_int(-3.49999, 0) == "-3",
        "negative rounding towards zero at 0 digits");
    TEST_ASSERT(plot::format_axis_fixed_or_int(1.234, 2) == "1.23", "fixed 2 digits");
    TEST_ASSERT(plot::format_axis_fixed_or_int(-0.0001, 2) == "0.00",
        "near-zero negative value should format as 0.00");
    TEST_ASSERT(plot::format_axis_fixed_or_int(std::numeric_limits<double>::max(), 1) == "0",
        "finite values that overflow fixed rounding should use the invalid-value label");
    return true;
}

bool test_time_unit_helpers_handle_edges()
{
    constexpr std::int64_t k_min = std::numeric_limits<std::int64_t>::min();
    constexpr std::int64_t k_max = std::numeric_limits<std::int64_t>::max();

    TEST_ASSERT(plot::ns_to_ms_for_qml(-1) == -1,
        "negative ns-to-ms conversion should floor");
    TEST_ASSERT(
        plot::floor_div_int64(k_min, -1) == k_max,
        "INT64_MIN / -1 floor division should saturate instead of overflowing");
    TEST_ASSERT(
        plot::ms_for_qml_to_ns(k_max) == k_max,
        "positive ms-to-ns overflow should saturate");
    TEST_ASSERT(
        plot::ms_for_qml_to_ns(k_min) == k_min,
        "negative ms-to-ns overflow should saturate");

    TEST_ASSERT(!plot::checked_add_ns(k_max, 1),
        "checked add should reject positive int64 overflow");
    TEST_ASSERT(!plot::checked_sub_ns(k_min, 1),
        "checked subtract should reject negative int64 overflow");
    TEST_ASSERT(plot::saturating_add_ns(k_max, 1) == k_max,
        "saturating add should clamp positive int64 overflow");
    TEST_ASSERT(plot::saturating_sub_ns(k_min, 1) == k_min,
        "saturating subtract should clamp negative int64 overflow");

    const auto full_range_span = plot::positive_span_ns(k_min, k_max);
    TEST_ASSERT(full_range_span && *full_range_span == std::numeric_limits<std::uint64_t>::max(),
        "positive_span_ns should represent the full int64 range without signed overflow");
    TEST_ASSERT(plot::span_ns_as_long_double(k_min, k_max) > static_cast<long double>(k_max),
        "long-double span conversion should handle spans larger than INT64_MAX");
    TEST_ASSERT(plot::midpoint_ns(k_min, k_max) == -1,
        "midpoint should avoid overflow for full int64 range");

    const plot::time_range_t full_centered =
        plot::centered_time_range_ns(-1, std::numeric_limits<std::uint64_t>::max());
    TEST_ASSERT(full_centered.min_ns == k_min && full_centered.max_ns == k_max,
        "centered full-range construction should preserve both int64 endpoints");

    const plot::time_range_t centered_at_max = plot::centered_time_range_ns(k_max, 100);
    TEST_ASSERT(centered_at_max.min_ns == k_max - 100 && centered_at_max.max_ns == k_max,
        "centered range at INT64_MAX should edge-clamp while preserving span");

    const plot::time_range_t centered_at_min = plot::centered_time_range_ns(k_min, 100);
    TEST_ASSERT(centered_at_min.min_ns == k_min && centered_at_min.max_ns == k_min + 100,
        "centered range at INT64_MIN should edge-clamp while preserving span");

    const auto full_right_edge = plot::time_at_fraction_ns(
        plot::time_range_t{k_min, k_max},
        1.0L);
    TEST_ASSERT(full_right_edge && *full_right_edge == k_max,
        "fractional mapping across the full int64 range should reach INT64_MAX");

    const auto shifted_right = plot::translate_time_range_ns(
        plot::time_range_t{k_max - 20, k_max - 10},
        50);
    TEST_ASSERT(shifted_right
        && shifted_right->min_ns == k_max - 10
        && shifted_right->max_ns == k_max,
        "positive translation at INT64_MAX should clamp while preserving span");

    const auto shifted_left = plot::translate_time_range_ns(
        plot::time_range_t{k_min + 10, k_min + 20},
        -50);
    TEST_ASSERT(shifted_left
        && shifted_left->min_ns == k_min
        && shifted_left->max_ns == k_min + 10,
        "negative translation at INT64_MIN should clamp while preserving span");

    const auto clamped_to_edge = plot::clamp_time_range_to_available_ns(
        plot::time_range_t{k_max - 12, k_max - 2},
        plot::time_range_t{k_min + 4, k_max - 5});
    TEST_ASSERT(clamped_to_edge
        && clamped_to_edge->min_ns == k_max - 15
        && clamped_to_edge->max_ns == k_max - 5,
        "availability clamp should preserve target span near INT64_MAX");

    const auto clamped_full_target = plot::clamp_time_range_to_available_ns(
        plot::time_range_t{k_min, k_max},
        plot::time_range_t{-500, 500});
    TEST_ASSERT(clamped_full_target
        && clamped_full_target->min_ns == -500
        && clamped_full_target->max_ns == 500,
        "target wider than availability should adopt the available range");

    const std::string formatted = plot::default_format_timestamp(k_min, plot::k_ns_per_second);
    TEST_ASSERT(!formatted.empty(),
        "default timestamp formatter should handle INT64_MIN without overflow");

    return true;
}

bool test_horizontal_label_fade_keys_preserve_int64_timestamps()
{
    constexpr std::int64_t k_min = std::numeric_limits<std::int64_t>::min();
    constexpr std::int64_t k_max = std::numeric_limits<std::int64_t>::max();

    plot::Text_renderer::horizontal_axis_fade_tracker_t tracker;
    tracker.states.emplace(k_min, plot::Text_renderer::label_fade_state_t{});
    tracker.states.emplace(k_min + 1, plot::Text_renderer::label_fade_state_t{});
    tracker.states.emplace(k_max - 1, plot::Text_renderer::label_fade_state_t{});
    tracker.states.emplace(k_max, plot::Text_renderer::label_fade_state_t{});

    TEST_ASSERT(tracker.states.size() == 4,
        "horizontal fade tracker should distinguish adjacent extreme int64 timestamps");

    const auto full_span = plot::positive_span_ns_as_long_double(k_min, k_max);
    TEST_ASSERT(full_span && std::isfinite(*full_span),
        "horizontal label span math should represent the full int64 range");

    const long double center_delta = plot::span_ns_as_long_double(k_min, -1);
    const long double right_delta = plot::span_ns_as_long_double(k_min, k_max);
    const long double center_fraction = center_delta / *full_span;
    const long double right_fraction = right_delta / *full_span;

    TEST_ASSERT(center_fraction > 0.499999999999999L
        && center_fraction < 0.500000000000001L,
        "full-range horizontal label math should keep the midpoint near 0.5");
    TEST_ASSERT(right_fraction == 1.0L,
        "full-range horizontal label math should reach the right edge exactly");

    return true;
}

} // namespace

int main()
{
    std::cout << "Core algorithm tests" << std::endl;

    int passed = 0;
    int failed = 0;

    RUN_TEST(test_choose_lod_level_picks_closest_pps);
    RUN_TEST(test_choose_lod_level_handles_degenerate_inputs);
    RUN_TEST(test_compute_lod_scales_forces_minimum_of_one);
    RUN_TEST(test_get_shift_handles_invalid_inputs);
    RUN_TEST(test_decimal_precision_helpers_reject_invalid_scaled_values);
    RUN_TEST(test_lower_and_upper_bound_on_contiguous_buffer);
    RUN_TEST(test_timestamp_search_rejects_null_samples);
    RUN_TEST(test_raw_timestamp_search_rejects_zero_stride);
    RUN_TEST(test_bounds_on_segmented_snapshot);
    RUN_TEST(test_snapshot_truthiness_requires_usable_stride);
    RUN_TEST(test_snapshot_count1_clamps_malformed_count2);
    RUN_TEST(test_layout_cache_key_distinguishes_adjacent_int64_time_windows);
    RUN_TEST(test_format_axis_fixed_or_int);
    RUN_TEST(test_time_unit_helpers_handle_edges);
    RUN_TEST(test_horizontal_label_fade_keys_preserve_int64_timestamps);

    std::cout << "Results: " << passed << " passed, " << failed << " failed" << std::endl;
    return failed > 0 ? 1 : 0;
}
