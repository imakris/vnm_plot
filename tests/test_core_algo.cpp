// vnm_plot core algorithm tests

#include "test_macros.h"

#include <vnm_plot/core/algo.h>
#include <vnm_plot/core/types.h>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

namespace plot = vnm::plot;

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
    RUN_TEST(test_lower_and_upper_bound_on_contiguous_buffer);
    RUN_TEST(test_bounds_on_segmented_snapshot);
    RUN_TEST(test_layout_cache_key_distinguishes_adjacent_int64_time_windows);
    RUN_TEST(test_format_axis_fixed_or_int);

    std::cout << "Results: " << passed << " passed, " << failed << " failed" << std::endl;
    return failed > 0 ? 1 : 0;
}
