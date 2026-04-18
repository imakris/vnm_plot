// vnm_plot core algorithm tests
// Covers LOD level selection, binary-search on timestamps (including segmented
// snapshots), and the small axis formatter used by default_format_timestamp.

#include <vnm_plot/core/algo.h>
#include <vnm_plot/core/types.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace plot = vnm::plot;

namespace {

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
        } \
        else { \
            std::cout << "FAIL" << std::endl; \
            ++failed; \
        } \
    } while (0)

struct sample_t
{
    double t = 0.0;
    float  v = 0.0f;
};

// Minimal Data_source exposing a configurable LOD ladder.
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
    // Scales 1..1024 at pps=0.01; expect level whose scale * pps is closest to 1.
    const std::vector<std::size_t> scales = {1, 4, 16, 64, 256, 1024};
    const std::size_t level = plot::detail::choose_lod_level(scales, 0.01);
    // 0.01 * 64 = 0.64 (err 0.36); 0.01 * 256 = 2.56 (err 1.56). Closest is 64.
    TEST_ASSERT(level == 3, "expected choose_lod_level to pick scale 64");
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

bool test_choose_lod_level_prefers_level_zero_on_tie()
{
    // When two levels give identical absolute error, the first one wins because
    // the loop only updates on strict <.
    const std::vector<std::size_t> scales = {1, 2};
    // 0.75 * 1 = 0.75 (err 0.25); 0.75 * 2 = 1.50 (err 0.50); best is level 0.
    TEST_ASSERT(plot::detail::choose_lod_level(scales, 0.75) == 0,
        "closer-to-1.0 pps on coarser level should stay when finer is equally close");
    return true;
}

bool test_compute_lod_scales_forces_minimum_of_one()
{
    // A malformed Data_source returning 0 should be clamped up to 1.
    class Zero_scale_source final : public plot::Data_source
    {
    public:
        plot::snapshot_result_t try_snapshot(std::size_t) override {
            return {plot::data_snapshot_t{}, plot::snapshot_result_t::Snapshot_status::EMPTY};
        }
        std::size_t lod_levels() const override { return 3; }
        std::size_t lod_scale(std::size_t level) const override {
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
    std::vector<sample_t> samples;
    for (int i = 0; i < 10; ++i) {
        samples.push_back({static_cast<double>(i), static_cast<float>(i)});
    }

    const auto get_ts = [](const void* p) {
        return static_cast<const sample_t*>(p)->t;
    };

    auto lb = plot::detail::lower_bound_timestamp(
        samples.data(), samples.size(), sizeof(sample_t), get_ts, 3.5);
    TEST_ASSERT(lb == 4, "lower_bound(3.5) should land on index 4");

    auto lb_exact = plot::detail::lower_bound_timestamp(
        samples.data(), samples.size(), sizeof(sample_t), get_ts, 3.0);
    TEST_ASSERT(lb_exact == 3, "lower_bound(3.0) should land on index 3");

    auto ub = plot::detail::upper_bound_timestamp(
        samples.data(), samples.size(), sizeof(sample_t), get_ts, 3.0);
    TEST_ASSERT(ub == 4, "upper_bound(3.0) should land on index 4");

    // Out of range probes should clamp to the ends.
    auto lb_below = plot::detail::lower_bound_timestamp(
        samples.data(), samples.size(), sizeof(sample_t), get_ts, -5.0);
    TEST_ASSERT(lb_below == 0, "lower_bound below range should be 0");

    auto lb_above = plot::detail::lower_bound_timestamp(
        samples.data(), samples.size(), sizeof(sample_t), get_ts, 100.0);
    TEST_ASSERT(lb_above == samples.size(), "lower_bound above range should be count");

    // Empty buffer is safe.
    auto lb_empty = plot::detail::lower_bound_timestamp(
        nullptr, 0, sizeof(sample_t), get_ts, 0.0);
    TEST_ASSERT(lb_empty == 0, "lower_bound on empty buffer should be 0");

    return true;
}

bool test_bounds_on_segmented_snapshot()
{
    // Build a ring-buffer-style snapshot: first physical block holds newer
    // samples, second holds older ones — but logical order (as the caller sees
    // via snapshot.at) must be ascending.
    std::vector<sample_t> tail;  // older logical samples (first half)
    std::vector<sample_t> head;  // newer logical samples (second half)
    for (int i = 0; i < 6; ++i) {
        tail.push_back({static_cast<double>(i), 0.0f});
    }
    for (int i = 6; i < 10; ++i) {
        head.push_back({static_cast<double>(i), 0.0f});
    }

    plot::data_snapshot_t snap;
    snap.data = tail.data();
    snap.count = 10;
    snap.stride = sizeof(sample_t);
    snap.data2 = head.data();
    snap.count2 = head.size();

    const auto get_ts = [](const void* p) {
        return static_cast<const sample_t*>(p)->t;
    };

    // Verify at() spans both segments monotonically.
    TEST_ASSERT(get_ts(snap.at(0)) == 0.0, "segmented at(0) should be 0");
    TEST_ASSERT(get_ts(snap.at(5)) == 5.0, "segmented at(5) should be 5");
    TEST_ASSERT(get_ts(snap.at(6)) == 6.0, "segmented at(6) should cross into second segment");
    TEST_ASSERT(get_ts(snap.at(9)) == 9.0, "segmented at(9) should be 9");

    auto lb = plot::detail::lower_bound_timestamp(snap, get_ts, 6.5);
    TEST_ASSERT(lb == 7, "segmented lower_bound(6.5) should land on index 7");

    auto ub = plot::detail::upper_bound_timestamp(snap, get_ts, 6.0);
    TEST_ASSERT(ub == 7, "segmented upper_bound(6.0) should land on index 7");

    // Empty snapshot.
    plot::data_snapshot_t empty;
    TEST_ASSERT(plot::detail::lower_bound_timestamp(empty, get_ts, 0.0) == 0,
        "lower_bound on empty snapshot should be 0");

    return true;
}

bool test_format_axis_fixed_or_int()
{
    TEST_ASSERT(plot::format_axis_fixed_or_int(0.0, 0) == "0",
        "zero as integer");
    TEST_ASSERT(plot::format_axis_fixed_or_int(3.0, 0) == "3",
        "integer rounding");
    TEST_ASSERT(plot::format_axis_fixed_or_int(-3.49999, 0) == "-3",
        "negative rounding towards zero at 0 digits");
    TEST_ASSERT(plot::format_axis_fixed_or_int(1.234, 2) == "1.23",
        "fixed 2 digits");
    // Rounds to -0.00 but should print without sign.
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
    RUN_TEST(test_choose_lod_level_prefers_level_zero_on_tie);
    RUN_TEST(test_compute_lod_scales_forces_minimum_of_one);
    RUN_TEST(test_lower_and_upper_bound_on_contiguous_buffer);
    RUN_TEST(test_bounds_on_segmented_snapshot);
    RUN_TEST(test_format_axis_fixed_or_int);

    std::cout << "Results: " << passed << " passed, " << failed << " failed" << std::endl;
    return failed > 0 ? 1 : 0;
}
