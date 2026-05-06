// vnm_plot core cache tests

#include "test_macros.h"

#include <vnm_plot/core/asset_loader.h>
#include <vnm_plot/core/series_renderer.h>
#include <vnm_plot/core/plot_config.h>
#include <vnm_plot/core/range_cache.h>

#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace plot = vnm::plot;
using plot::Auto_v_range_mode;
using plot::Data_access_policy;
using plot::Data_source;
using plot::Display_style;
using plot::data_snapshot_t;
using plot::lod_minmax_cache_t;
using plot::preview_config_t;
using plot::series_data_t;
using plot::series_minmax_cache_t;
using plot::snapshot_result_t;
using plot::validate_range_cache_sequences;

namespace {

struct Test_sample {
    // Timestamps are int64 nanoseconds (API convention).
    std::int64_t t = 0;
    float v = 0.0f;
};

class Range_cache_source final : public Data_source {
public:
    std::vector<Test_sample> samples;
    size_t levels = 1;
    uint64_t current_sequence_value = 1;
    uint64_t snapshot_sequence_value = 1;
    bool fail_snapshot = false;
    int snapshot_calls = 0;

    snapshot_result_t try_snapshot(size_t lod_level) override
    {
        if (lod_level >= levels) {
            return {data_snapshot_t{}, snapshot_result_t::Snapshot_status::FAILED};
        }
        ++snapshot_calls;
        if (fail_snapshot) {
            return {data_snapshot_t{}, snapshot_result_t::Snapshot_status::FAILED};
        }
        auto hold = std::make_shared<int>(13);
        data_snapshot_t snapshot{
            samples.data(),
            samples.size(),
            sizeof(Test_sample),
            snapshot_sequence_value,
            nullptr,
            0,
            hold
        };
        if (samples.empty()) {
            return {data_snapshot_t{}, snapshot_result_t::Snapshot_status::EMPTY};
        }
        return {snapshot, snapshot_result_t::Snapshot_status::READY};
    }

    size_t lod_levels() const override { return levels; }
    size_t lod_scale(size_t level) const override { return level == 0 ? 1 : 4; }
    size_t sample_stride() const override { return sizeof(Test_sample); }
    uint64_t current_sequence(size_t /*lod_level*/) const override { return current_sequence_value; }
};

Data_access_policy make_policy()
{
    Data_access_policy policy;
    policy.get_timestamp = [](const void* sample) -> std::int64_t {
        return static_cast<const Test_sample*>(sample)->t;
    };
    policy.get_value = [](const void* sample) {
        return static_cast<const Test_sample*>(sample)->v;
    };
    policy.get_range = [](const void* sample) {
        const float value = static_cast<const Test_sample*>(sample)->v;
        return std::make_pair(value, value);
    };
    return policy;
}

std::shared_ptr<series_data_t> make_series(const std::shared_ptr<Data_source>& source)
{
    auto series = std::make_shared<series_data_t>();
    series->style = Display_style::LINE;
    series->data_source = source;
    series->access = make_policy();
    return series;
}

bool test_failed_snapshot_invalidates_range_cache()
{
    auto data_source = std::make_shared<Range_cache_source>();
    data_source->samples.resize(1);
    data_source->current_sequence_value = 0;
    data_source->snapshot_sequence_value = 5;
    data_source->fail_snapshot = true;

    const int series_id = 10;
    auto series = make_series(data_source);

    std::map<int, std::shared_ptr<const series_data_t>> series_map;
    series_map[series_id] = series;

    std::unordered_map<int, series_minmax_cache_t> cache_map;
    auto& cache = cache_map[series_id];
    cache.identity = data_source->identity();
    cache.lods.assign(data_source->lod_levels(), lod_minmax_cache_t{});
    cache.lods[0].valid = true;
    cache.lods[0].sequence = 5;

    const bool valid = validate_range_cache_sequences(
        series_map,
        cache_map,
        Auto_v_range_mode::GLOBAL);

    TEST_ASSERT(!valid, "expected range cache invalidation on failed snapshot");
    TEST_ASSERT(data_source->snapshot_calls == 1,
                "expected snapshot attempt when current_sequence is 0");

    return true;
}

bool test_sequence_change_invalidates_range_cache()
{
    auto data_source = std::make_shared<Range_cache_source>();
    data_source->samples.resize(1);
    data_source->current_sequence_value = 7;
    data_source->snapshot_sequence_value = 7;

    const int series_id = 11;
    auto series = make_series(data_source);

    std::map<int, std::shared_ptr<const series_data_t>> series_map;
    series_map[series_id] = series;

    std::unordered_map<int, series_minmax_cache_t> cache_map;
    auto& cache = cache_map[series_id];
    cache.identity = data_source->identity();
    cache.lods.assign(data_source->lod_levels(), lod_minmax_cache_t{});
    cache.lods[0].valid = true;
    cache.lods[0].sequence = 7;

    const bool valid = validate_range_cache_sequences(
        series_map,
        cache_map,
        Auto_v_range_mode::GLOBAL);
    TEST_ASSERT(valid, "expected range cache to stay valid when sequence matches");

    data_source->current_sequence_value = 8;
    const bool valid_after = validate_range_cache_sequences(
        series_map,
        cache_map,
        Auto_v_range_mode::GLOBAL);
    TEST_ASSERT(!valid_after, "expected range cache invalidation on sequence change");

    return true;
}

bool test_validate_range_cache_with_empty_series()
{
    std::map<int, std::shared_ptr<const series_data_t>> series_map;
    std::unordered_map<int, series_minmax_cache_t> cache_map;

    const bool valid = validate_range_cache_sequences(
        series_map,
        cache_map,
        Auto_v_range_mode::GLOBAL);

    TEST_ASSERT(valid, "expected empty series map to keep cache valid");

    return true;
}

bool test_validate_range_cache_skips_null_series()
{
    std::map<int, std::shared_ptr<const series_data_t>> series_map;
    series_map[1] = nullptr;

    std::unordered_map<int, series_minmax_cache_t> cache_map;

    const bool valid = validate_range_cache_sequences(
        series_map,
        cache_map,
        Auto_v_range_mode::GLOBAL);

    TEST_ASSERT(valid, "expected null series entries to be ignored");

    return true;
}

bool test_validate_range_cache_skips_null_data_source()
{
    const int series_id = 2;
    auto series = std::make_shared<series_data_t>();
    series->data_source.reset();
    series->access = make_policy();

    std::map<int, std::shared_ptr<const series_data_t>> series_map;
    series_map[series_id] = series;

    std::unordered_map<int, series_minmax_cache_t> cache_map;

    const bool valid = validate_range_cache_sequences(
        series_map,
        cache_map,
        Auto_v_range_mode::GLOBAL);

    TEST_ASSERT(valid, "expected null data sources to be ignored");

    return true;
}

bool test_validate_range_cache_skips_missing_accessors()
{
    auto data_source = std::make_shared<Range_cache_source>();
    data_source->samples.resize(1);

    const int series_id = 3;
    auto series = std::make_shared<series_data_t>();
    series->data_source = data_source;
    series->access = Data_access_policy{};

    std::map<int, std::shared_ptr<const series_data_t>> series_map;
    series_map[series_id] = series;

    std::unordered_map<int, series_minmax_cache_t> cache_map;

    const bool valid = validate_range_cache_sequences(
        series_map,
        cache_map,
        Auto_v_range_mode::GLOBAL);

    TEST_ASSERT(valid, "expected series without value accessors to be ignored");

    return true;
}

bool test_validate_range_cache_skips_disabled_series()
{
    auto data_source = std::make_shared<Range_cache_source>();
    data_source->samples.resize(1);

    const int series_id = 4;
    auto series = std::make_shared<series_data_t>();
    series->enabled = false;
    series->data_source = data_source;
    series->access = make_policy();

    std::map<int, std::shared_ptr<const series_data_t>> series_map;
    series_map[series_id] = series;

    std::unordered_map<int, series_minmax_cache_t> cache_map;

    const bool valid = validate_range_cache_sequences(
        series_map,
        cache_map,
        Auto_v_range_mode::GLOBAL);

    TEST_ASSERT(valid, "expected disabled series to be ignored");

    return true;
}

bool test_preview_matches_main_helpers()
{
    auto data_source = std::make_shared<Range_cache_source>();
    data_source->samples.resize(1);

    auto series = make_series(data_source);
    series->preview_config = preview_config_t{};
    series->preview_config->data_source = data_source;
    series->preview_config->access = Data_access_policy{};

    TEST_ASSERT(series->preview_matches_main(),
                "expected preview matches main with same source and style");

    series->preview_config->style = Display_style::AREA;
    TEST_ASSERT(!series->preview_matches_main(),
                "expected preview mismatch when preview style differs");

    series->preview_config->style.reset();
    series->preview_config->access = make_policy();
    series->preview_config->access.layout_key = 0x9999;
    TEST_ASSERT(!series->preview_matches_main(),
                "expected preview mismatch when layout_key differs");

    series->preview_config->data_source.reset();
    TEST_ASSERT(!series->preview_matches_main(),
                "expected preview mismatch when preview source is null");

    return true;
}

bool test_validate_range_cache_sequences()
{
    auto main_source = std::make_shared<Range_cache_source>();
    main_source->samples.resize(1);

    auto preview_source = std::make_shared<Range_cache_source>();
    preview_source->samples.resize(1);
    preview_source->current_sequence_value = 3;
    preview_source->snapshot_sequence_value = 3;

    const int series_id = 30;
    auto series = make_series(main_source);
    preview_config_t preview_cfg;
    preview_cfg.data_source = preview_source;
    preview_cfg.access = make_policy();
    series->preview_config = preview_cfg;

    std::map<int, std::shared_ptr<const series_data_t>> series_map;
    series_map[series_id] = series;

    std::unordered_map<int, series_minmax_cache_t> cache_map;
    auto& cache = cache_map[series_id];
    cache.identity = preview_source->identity();
    cache.lods.assign(preview_source->lod_levels(), lod_minmax_cache_t{});
    cache.lods[0].valid = true;
    cache.lods[0].sequence = preview_source->current_sequence(0);

    const bool valid = validate_range_cache_sequences(
        series_map,
        cache_map,
        Auto_v_range_mode::GLOBAL, /*preview=*/true);
    TEST_ASSERT(valid, "expected preview cache to be valid when sequences match");

    preview_source->current_sequence_value = cache.lods[0].sequence + 1;
    const bool valid_after = validate_range_cache_sequences(
        series_map,
        cache_map,
        Auto_v_range_mode::GLOBAL, /*preview=*/true);
    TEST_ASSERT(!valid_after, "expected preview cache invalidation on sequence change");

    return true;
}

}  // namespace

int main()
{
    std::cout << "Core cache invalidation tests" << std::endl;

    int passed = 0;
    int failed = 0;

    RUN_TEST(test_failed_snapshot_invalidates_range_cache);
    RUN_TEST(test_sequence_change_invalidates_range_cache);
    RUN_TEST(test_validate_range_cache_with_empty_series);
    RUN_TEST(test_validate_range_cache_skips_null_series);
    RUN_TEST(test_validate_range_cache_skips_null_data_source);
    RUN_TEST(test_validate_range_cache_skips_missing_accessors);
    RUN_TEST(test_validate_range_cache_skips_disabled_series);
    RUN_TEST(test_preview_matches_main_helpers);
    RUN_TEST(test_validate_range_cache_sequences);

    std::cout << "Results: " << passed << " passed, " << failed << " failed" << std::endl;

    return failed > 0 ? 1 : 0;
}
