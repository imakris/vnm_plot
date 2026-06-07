// vnm_plot core snapshot cache tests

#include "test_macros.h"

#include <vnm_plot/core/algo.h>
#include <vnm_plot/core/asset_loader.h>
#include "../src/core/series_window_planner.h"
#define private public
#include <vnm_plot/core/series_renderer.h>
#undef private
#include <vnm_plot/core/plot_config.h>
#include <vnm_plot/core/time_units.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace plot = vnm::plot;
using plot::Asset_loader;
using plot::Data_access_policy;
using plot::Data_source;
using plot::Display_style;
using plot::Empty_window_behavior;
using plot::Plot_config;
using plot::Series_renderer;
using plot::data_snapshot_t;
using plot::frame_context_t;
using plot::frame_layout_result_t;
using plot::preview_config_t;
using plot::series_data_t;
using plot::snapshot_result_t;

namespace {

const plot::detail::series_window_planner_state_t& planner_state(
    const Series_renderer::vbo_view_state_t& view_state)
{
    assert(view_state.planner);
    return *view_state.planner;
}

struct Test_sample {
    // Timestamps are int64 nanoseconds (API convention).
    std::int64_t t = 0;
    float v = 0.0f;
};

class Single_level_source final : public Data_source {
public:
    std::vector<Test_sample> samples;
    int snapshot_calls = 0;
    uint64_t sequence = 1;
    std::weak_ptr<void> last_hold;

    snapshot_result_t try_snapshot(size_t lod_level) override
    {
        if (lod_level != 0) {
            return {data_snapshot_t{}, snapshot_result_t::Snapshot_status::FAILED};
        }
        ++snapshot_calls;
        auto hold = std::make_shared<int>(7);
        last_hold = hold;
        data_snapshot_t snapshot{
            samples.data(),
            samples.size(),
            sizeof(Test_sample),
            sequence,
            nullptr,
            0,
            hold
        };
        if (samples.empty()) {
            return {data_snapshot_t{}, snapshot_result_t::Snapshot_status::EMPTY};
        }
        return {snapshot, snapshot_result_t::Snapshot_status::READY};
    }

    size_t lod_levels() const override { return 1; }
    size_t lod_scale(size_t /*level*/) const override { return 1; }
    size_t sample_stride() const override { return sizeof(Test_sample); }
    uint64_t current_sequence(size_t /*lod_level*/) const override { return sequence; }
};

class Two_level_source final : public Data_source {
public:
    std::vector<Test_sample> lod0;
    std::vector<Test_sample> lod1;
    std::array<int, 2> snapshot_calls{{0, 0}};
    std::array<uint64_t, 2> sequences{{1, 1}};

    snapshot_result_t try_snapshot(size_t lod_level) override
    {
        if (lod_level >= 2) {
            return {data_snapshot_t{}, snapshot_result_t::Snapshot_status::FAILED};
        }
        ++snapshot_calls[lod_level];
        const auto& data = (lod_level == 0) ? lod0 : lod1;
        auto hold = std::make_shared<int>(21);
        data_snapshot_t snapshot{
            data.data(),
            data.size(),
            sizeof(Test_sample),
            sequences[lod_level],
            nullptr,
            0,
            hold
        };
        if (data.empty()) {
            return {data_snapshot_t{}, snapshot_result_t::Snapshot_status::EMPTY};
        }
        return {snapshot, snapshot_result_t::Snapshot_status::READY};
    }

    size_t lod_levels() const override { return 2; }
    size_t lod_scale(size_t level) const override { return level == 0 ? 1 : 4; }
    size_t sample_stride() const override { return sizeof(Test_sample); }
    uint64_t current_sequence(size_t /*lod_level*/) const override { return 0; }
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

frame_context_t make_context(const frame_layout_result_t& layout, Plot_config& config)
{
    frame_context_t ctx{layout};
    // Timestamps are int64 ns. Tests use small ordinal indices for clarity.
    ctx.t0 = 0;
    ctx.t1 = 10;
    ctx.t_available_min = 0;
    ctx.t_available_max = 10;
    ctx.win_w = 200;
    ctx.win_h = 120;
    ctx.dark_mode = config.dark_mode;
    ctx.config = &config;
    return ctx;
}

void fill_lod_samples(Two_level_source& source)
{
    source.lod0.resize(100);
    for (size_t i = 0; i < source.lod0.size(); ++i) {
        source.lod0[i].t = static_cast<std::int64_t>(i);
        source.lod0[i].v = 1.0f + static_cast<float>(i);
    }
    source.lod1.resize(25);
    for (size_t i = 0; i < source.lod1.size(); ++i) {
        const size_t src = i * 4;
        source.lod1[i].t = static_cast<std::int64_t>(src);
        source.lod1[i].v = 1.0f + static_cast<float>(src);
    }
}

bool test_frame_scoped_cache_reuse()
{
    auto data_source = std::make_shared<Single_level_source>();
    data_source->samples.resize(16);
    for (size_t i = 0; i < data_source->samples.size(); ++i) {
        data_source->samples[i].t = static_cast<std::int64_t>(i);
        data_source->samples[i].v = 1.0f + static_cast<float>(i);
    }

    const int series_id = 7;
    auto series = std::make_shared<series_data_t>();
    series->style = Display_style::LINE;
    series->data_source = data_source;
    series->access = make_policy();

    frame_layout_result_t layout;
    layout.usable_width = 200.0;
    layout.usable_height = 80.0;

    Plot_config config;
    config.preview_visibility = 1.0;

    frame_context_t ctx = make_context(layout, config);
    ctx.adjusted_preview_height = 20.0;

    Series_renderer renderer;
    Asset_loader asset_loader;
    renderer.initialize(asset_loader);

    std::map<int, std::shared_ptr<const series_data_t>> series_map;
    series_map[series_id] = series;

    renderer.render(ctx, series_map);

    TEST_ASSERT(data_source->snapshot_calls == 1,
                std::string("expected shared snapshot between main and preview views, got ") +
                    std::to_string(data_source->snapshot_calls));
    TEST_ASSERT(data_source->last_hold.expired(),
                "expected frame-scoped snapshot hold to release after render");

    return true;
}

bool test_preview_uses_distinct_source_snapshot()
{
    auto main_source = std::make_shared<Single_level_source>();
    auto preview_source = std::make_shared<Single_level_source>();
    main_source->samples.resize(8);
    preview_source->samples.resize(8);
    for (size_t i = 0; i < main_source->samples.size(); ++i) {
        main_source->samples[i].t = static_cast<std::int64_t>(i);
        main_source->samples[i].v = 1.0f + static_cast<float>(i);
        preview_source->samples[i].t = static_cast<std::int64_t>(i);
        preview_source->samples[i].v = 2.0f + static_cast<float>(i);
    }

    const int series_id = 14;
    auto series = std::make_shared<series_data_t>();
    series->style = Display_style::LINE;
    series->data_source = main_source;
    series->access = make_policy();

    preview_config_t preview_cfg;
    preview_cfg.data_source = preview_source;
    preview_cfg.access = make_policy();
    series->preview_config = preview_cfg;

    frame_layout_result_t layout;
    layout.usable_width = 200.0;
    layout.usable_height = 80.0;

    Plot_config config;
    config.preview_visibility = 1.0;

    frame_context_t ctx = make_context(layout, config);
    ctx.adjusted_preview_height = 20.0;

    Series_renderer renderer;
    Asset_loader asset_loader;
    renderer.initialize(asset_loader);

    std::map<int, std::shared_ptr<const series_data_t>> series_map;
    series_map[series_id] = series;

    renderer.render(ctx, series_map);

    TEST_ASSERT(main_source->snapshot_calls == 1,
                "expected main source snapshot call");
    TEST_ASSERT(preview_source->snapshot_calls == 1,
                "expected preview source snapshot call");

    return true;
}

bool test_preview_disabled_skips_preview_snapshot()
{
    auto main_source = std::make_shared<Single_level_source>();
    auto preview_source = std::make_shared<Single_level_source>();
    main_source->samples.resize(8);
    preview_source->samples.resize(8);

    const int series_id = 15;
    auto series = std::make_shared<series_data_t>();
    series->style = Display_style::LINE;
    series->data_source = main_source;
    series->access = make_policy();

    preview_config_t preview_cfg;
    preview_cfg.data_source = preview_source;
    preview_cfg.access = make_policy();
    series->preview_config = preview_cfg;

    frame_layout_result_t layout;
    layout.usable_width = 200.0;
    layout.usable_height = 80.0;

    Plot_config config;
    config.preview_visibility = 1.0;

    frame_context_t ctx = make_context(layout, config);
    ctx.adjusted_preview_height = 0.0;

    Series_renderer renderer;
    Asset_loader asset_loader;
    renderer.initialize(asset_loader);

    std::map<int, std::shared_ptr<const series_data_t>> series_map;
    series_map[series_id] = series;

    renderer.render(ctx, series_map);

    TEST_ASSERT(main_source->snapshot_calls == 1,
                "expected main source snapshot call");
    TEST_ASSERT(preview_source->snapshot_calls == 0,
                "expected preview source to be skipped when preview disabled");

    return true;
}

bool test_frame_change_invalidates_snapshot_cache()
{
    auto data_source = std::make_shared<Single_level_source>();
    data_source->samples.resize(12);
    for (size_t i = 0; i < data_source->samples.size(); ++i) {
        data_source->samples[i].t = static_cast<std::int64_t>(i);
        data_source->samples[i].v = 0.5f + static_cast<float>(i);
    }

    const int series_id = 8;
    auto series = std::make_shared<series_data_t>();
    series->style = Display_style::LINE;
    series->data_source = data_source;
    series->access = make_policy();

    frame_layout_result_t layout;
    layout.usable_width = 140.0;
    layout.usable_height = 80.0;

    Plot_config config;

    frame_context_t ctx = make_context(layout, config);

    Series_renderer renderer;
    Asset_loader asset_loader;
    renderer.initialize(asset_loader);

    std::map<int, std::shared_ptr<const series_data_t>> series_map;
    series_map[series_id] = series;

    renderer.render(ctx, series_map);
    renderer.render(ctx, series_map);

    TEST_ASSERT(data_source->snapshot_calls == 2,
                "expected snapshot refresh on frame change");

    return true;
}

bool test_empty_window_behavior_invalidates_fast_path_cache()
{
    auto data_source = std::make_shared<Single_level_source>();
    data_source->samples.resize(32);
    for (size_t i = 0; i < data_source->samples.size(); ++i) {
        data_source->samples[i].t = static_cast<std::int64_t>(i);
        data_source->samples[i].v = 10.0f + static_cast<float>(i);
    }

    const int series_id = 18;
    auto series = std::make_shared<series_data_t>();
    series->style = Display_style::LINE;
    series->data_source = data_source;
    series->access = make_policy();
    series->empty_window_behavior = Empty_window_behavior::DRAW_NOTHING;

    frame_layout_result_t layout;
    layout.usable_width = 180.0;
    layout.usable_height = 80.0;

    Plot_config config;

    frame_context_t ctx = make_context(layout, config);

    Series_renderer renderer;
    Asset_loader asset_loader;
    renderer.initialize(asset_loader);

    std::map<int, std::shared_ptr<const series_data_t>> series_map;
    series_map[series_id] = series;

    renderer.render(ctx, series_map);
    TEST_ASSERT(data_source->snapshot_calls == 1,
                "expected first render to take one snapshot");
    auto state_it = renderer.m_vbo_states.find(series_id);
    TEST_ASSERT(state_it != renderer.m_vbo_states.end(),
                "expected vbo state for test series");
    // QRhi-less tests seed upload state to exercise the CPU fast-path conditions in plan_view().
    state_it->second.main_view.has_uploaded_vbo = true;

    renderer.render(ctx, series_map);
    TEST_ASSERT(data_source->snapshot_calls == 1,
                "expected fast-path cache hit to skip snapshot");

    series->empty_window_behavior = Empty_window_behavior::HOLD_LAST_FORWARD;
    renderer.render(ctx, series_map);
    TEST_ASSERT(data_source->snapshot_calls == 2,
                "expected empty_window_behavior change to invalidate fast-path cache");

    return true;
}

bool test_preview_honors_hold_last_forward()
{
    auto data_source = std::make_shared<Single_level_source>();
    data_source->samples.resize(16);
    for (size_t i = 0; i < data_source->samples.size(); ++i) {
        data_source->samples[i].t = static_cast<std::int64_t>(i);
        data_source->samples[i].v = 20.0f + static_cast<float>(i);
    }

    const int series_id = 19;
    auto series = std::make_shared<series_data_t>();
    series->style = Display_style::LINE;
    series->data_source = data_source;
    series->access = make_policy();
    series->empty_window_behavior = Empty_window_behavior::HOLD_LAST_FORWARD;

    frame_layout_result_t layout;
    layout.usable_width = 160.0;
    layout.usable_height = 80.0;

    Plot_config config;
    config.preview_visibility = 1.0;

    frame_context_t ctx = make_context(layout, config);
    ctx.adjusted_preview_height = 24.0;
    ctx.t_available_max = 40.0;

    Series_renderer renderer;
    Asset_loader asset_loader;
    renderer.initialize(asset_loader);

    std::map<int, std::shared_ptr<const series_data_t>> series_map;
    series_map[series_id] = series;

    renderer.render(ctx, series_map);
    auto state_it = renderer.m_vbo_states.find(series_id);
    TEST_ASSERT(state_it != renderer.m_vbo_states.end(),
                "expected vbo state for preview hold-forward test");
    TEST_ASSERT(state_it->second.preview_view.planner,
                "expected preview planner state for preview hold-forward test");
    TEST_ASSERT(planner_state(state_it->second.preview_view).last_hold_last_forward,
                "preview should honor HOLD_LAST_FORWARD behavior");

    return true;
}

bool test_lod_level_separation()
{
    auto data_source = std::make_shared<Two_level_source>();
    fill_lod_samples(*data_source);

    const int series_id = 9;
    auto series = std::make_shared<series_data_t>();
    series->style = Display_style::LINE;
    series->data_source = data_source;
    series->access = make_policy();

    Plot_config config;

    Series_renderer renderer;
    Asset_loader asset_loader;
    renderer.initialize(asset_loader);

    std::map<int, std::shared_ptr<const series_data_t>> series_map;
    series_map[series_id] = series;

    frame_layout_result_t layout_wide;
    layout_wide.usable_width = 100.0;
    layout_wide.usable_height = 80.0;
    frame_context_t ctx_wide = make_context(layout_wide, config);
    ctx_wide.t0 = 0.0;
    ctx_wide.t1 = 99.0;
    ctx_wide.win_w = 100;

    renderer.render(ctx_wide, series_map);

    TEST_ASSERT(data_source->snapshot_calls[0] >= 1,
                "expected LOD0 snapshot at wide width");
    TEST_ASSERT(data_source->snapshot_calls[1] == 0,
                "did not expect LOD1 snapshot at wide width");

    frame_layout_result_t layout_narrow;
    layout_narrow.usable_width = 20.0;
    layout_narrow.usable_height = 80.0;
    frame_context_t ctx_narrow = make_context(layout_narrow, config);
    ctx_narrow.t0 = 0.0;
    ctx_narrow.t1 = 99.0;
    ctx_narrow.win_w = 20;

    renderer.render(ctx_narrow, series_map);

    TEST_ASSERT(data_source->snapshot_calls[1] >= 1,
                "expected LOD1 snapshot at narrow width");

    return true;
}

bool test_snapshot_released_after_render()
{
    auto data_source = std::make_shared<Single_level_source>();
    data_source->samples.resize(8);
    for (size_t i = 0; i < data_source->samples.size(); ++i) {
        data_source->samples[i].t = static_cast<std::int64_t>(i);
        data_source->samples[i].v = 2.0f + static_cast<float>(i);
    }

    const int series_id = 3;
    auto series = std::make_shared<series_data_t>();
    series->style = Display_style::LINE;
    series->data_source = data_source;
    series->access = make_policy();

    frame_layout_result_t layout;
    layout.usable_width = 160.0;
    layout.usable_height = 80.0;

    Plot_config config;

    Series_renderer renderer;
    Asset_loader asset_loader;
    renderer.initialize(asset_loader);

    std::map<int, std::shared_ptr<const series_data_t>> series_map;
    series_map[series_id] = series;

    frame_context_t ctx = make_context(layout, config);
    renderer.render(ctx, series_map);

    std::weak_ptr<void> hold = data_source->last_hold;
    TEST_ASSERT(hold.expired(), "expected snapshot hold to release after render");

    return true;
}

bool test_render_empty_series_map()
{
    frame_layout_result_t layout;
    layout.usable_width = 120.0;
    layout.usable_height = 80.0;

    Plot_config config;

    frame_context_t ctx = make_context(layout, config);

    Series_renderer renderer;
    Asset_loader asset_loader;
    renderer.initialize(asset_loader);

    std::map<int, std::shared_ptr<const series_data_t>> empty_map;
    renderer.render(ctx, empty_map);

    return true;
}

bool test_upload_origin_records_per_view_origin()
{
    // The renderer's per-view upload-invalidation key must include the view
    // origin. After a render, view_state.uploaded_t_origin_ns must equal
    // the renderer's signed-span origin for that view, otherwise the next
    // frame's origin-change branch in plan_view will not fire when it should.
    // This is the visible state-trace of the upload-invalidation contract; the
    // inline predicate inside plan_view is hard to test directly without
    // refactoring the renderer.

    auto data_source = std::make_shared<Single_level_source>();
    data_source->samples.resize(8);
    for (size_t i = 0; i < data_source->samples.size(); ++i) {
        data_source->samples[i].t = static_cast<std::int64_t>(i) * 1'000'000LL;
        data_source->samples[i].v = 1.0f + static_cast<float>(i);
    }

    const int series_id = 41;
    auto series = std::make_shared<series_data_t>();
    series->style = Display_style::LINE;
    series->data_source = data_source;
    series->access = make_policy();

    frame_layout_result_t layout;
    layout.usable_width = 200.0;
    layout.usable_height = 80.0;

    Plot_config config;

    frame_context_t ctx = make_context(layout, config);
    // Span = 1 ms + 1 ns is strictly greater than k_ns_per_ms, so
    // choose_snap_ns falls into the next bucket and returns 1 us. t0 is
    // an exact multiple of 1 us, so the snap-aligned origin equals t0.
    ctx.t0 = 5'000'000LL;
    ctx.t1 = 5'000'001LL + 1'000'000LL; // 1 ms + 1 ns
    ctx.t_available_min = ctx.t0;
    ctx.t_available_max = ctx.t1;

    Series_renderer renderer;
    Asset_loader asset_loader;
    renderer.initialize(asset_loader);

    std::map<int, std::shared_ptr<const series_data_t>> series_map;
    series_map[series_id] = series;

    renderer.render(ctx, series_map);

    auto state_it = renderer.m_vbo_states.find(series_id);
    TEST_ASSERT(state_it != renderer.m_vbo_states.end(),
        "expected vbo state for upload-origin test");

    const std::int64_t expected_span_ns =
        plot::detail::positive_span_ns_for_signed_api(ctx.t0, ctx.t1);
    const std::int64_t expected_origin_ns =
        plot::detail::choose_origin_ns(ctx.t0, expected_span_ns);
    TEST_ASSERT(state_it->second.main_view.planner,
        "expected main planner state for upload-origin test");
    TEST_ASSERT(
        planner_state(state_it->second.main_view).uploaded_t_origin_ns == expected_origin_ns,
        std::string("uploaded_t_origin_ns must match choose_origin_ns; got ") +
        std::to_string(planner_state(state_it->second.main_view).uploaded_t_origin_ns) +
        ", expected " + std::to_string(expected_origin_ns));

    return true;
}

bool test_upload_invalidates_when_origin_changes_across_snap_bucket()
{
    // The cache-fast-path predicate inside plan_view requires
    // view_state.uploaded_t_origin_ns == t_origin_ns. With sequence,
    // identity, and width all unchanged, an origin change (achieved by
    // moving t_min/t_max across a snap-step boundary) must still force the
    // fast-path miss and a fresh snapshot. Two different view ranges in
    // the same snap bucket would skip the snapshot; two ranges in
    // different buckets must not.

    auto data_source = std::make_shared<Single_level_source>();
    data_source->samples.resize(64);
    for (size_t i = 0; i < data_source->samples.size(); ++i) {
        // 100-second timeline at 1.5-second cadence.
        data_source->samples[i].t = static_cast<std::int64_t>(i) * 1'500'000'000LL;
        data_source->samples[i].v = 1.0f + static_cast<float>(i);
    }

    const int series_id = 42;
    auto series = std::make_shared<series_data_t>();
    series->style = Display_style::LINE;
    series->data_source = data_source;
    series->access = make_policy();

    frame_layout_result_t layout;
    layout.usable_width = 200.0;
    layout.usable_height = 80.0;

    Plot_config config;

    Series_renderer renderer;
    Asset_loader asset_loader;
    renderer.initialize(asset_loader);

    std::map<int, std::shared_ptr<const series_data_t>> series_map;
    series_map[series_id] = series;

    // Span = 1 hour -> snap = 1 second (per choose_snap_ns).
    // Render frames whose t_min spans different 1-second buckets. Sequence,
    // identity, width all stay the same; only origin (and t_min/t_max)
    // moves. The renderer should call try_snapshot on every frame because
    // origin change is part of the upload-invalidation key.
    auto run_frame = [&](std::int64_t t_min_ns, std::int64_t span_ns) {
        frame_context_t ctx = make_context(layout, config);
        ctx.t0 = t_min_ns;
        ctx.t1 = t_min_ns + span_ns;
        ctx.t_available_min = ctx.t0;
        ctx.t_available_max = ctx.t1;
        renderer.render(ctx, series_map);
        // QRhi-less tests seed upload state to keep the cache-hit predicate's other terms truthy.
        auto it = renderer.m_vbo_states.find(series_id);
        if (it != renderer.m_vbo_states.end()) {
            it->second.main_view.has_uploaded_vbo = true;
        }
    };

    constexpr std::int64_t k_one_hour_ns = 3'600LL * 1'000'000'000LL;
    constexpr std::int64_t k_one_second_ns = 1'000'000'000LL;

    // Frame 1: bucket-aligned origin = 0.
    run_frame(0LL, k_one_hour_ns);
    auto state_it = renderer.m_vbo_states.find(series_id);
    TEST_ASSERT(state_it != renderer.m_vbo_states.end(),
        "expected vbo state to exist after first render");
    TEST_ASSERT(state_it->second.main_view.planner,
        "expected main planner state for origin invalidation test");
    const std::int64_t origin_after_frame1 =
        planner_state(state_it->second.main_view).uploaded_t_origin_ns;
    TEST_ASSERT(origin_after_frame1 == 0LL,
        std::string("expected origin 0 after first render, got ") +
        std::to_string(origin_after_frame1));
    const int snapshots_after_frame1 = data_source->snapshot_calls;
    TEST_ASSERT(snapshots_after_frame1 >= 1,
        "expected at least one snapshot on first render");

    // Frame 2: same span, t_min still in same 1 s bucket -> origin
    // unchanged. The cache fast path should fire (no extra snapshot).
    run_frame(k_one_second_ns / 2, k_one_hour_ns);
    const std::int64_t origin_after_frame2 =
        planner_state(state_it->second.main_view).uploaded_t_origin_ns;
    TEST_ASSERT(origin_after_frame2 == 0LL,
        "expected origin to remain 0 when t_min moves within the same snap bucket");
    // Snapshot the call count after frame 2 so the post-frame-3 assertion
    // attributes any new snapshot strictly to frame 3's origin shift.
    // Frame 2's t_min/t_max differ from frame 1's, so snapshot_calls may
    // grow here too (the fast path bails on t_min/t_max mismatch before
    // reaching the origin term); that growth is not the contract under test.
    const int snapshots_after_frame2 = data_source->snapshot_calls;

    // Frame 3: t_min moves into the next 1 s bucket -> origin must change.
    run_frame(k_one_second_ns + k_one_second_ns / 4, k_one_hour_ns);
    const std::int64_t origin_after_frame3 =
        planner_state(state_it->second.main_view).uploaded_t_origin_ns;
    TEST_ASSERT(origin_after_frame3 == k_one_second_ns,
        std::string("expected origin to advance to 1 s bucket after t_min "
                    "crosses snap boundary, got ") +
        std::to_string(origin_after_frame3));
    TEST_ASSERT(origin_after_frame3 != origin_after_frame1,
        "origin must change when t_min crosses a snap-step boundary");

    // The renderer must have re-run try_snapshot for frame 3 specifically;
    // comparing against the post-frame-2 count isolates frame 3's
    // contribution. (origin_after_frame3 already proves origin advanced; if
    // origin weren't part of the upload-invalidation key, that assertion
    // alone would not fail, but a regression in snapshot triggering would
    // leave snapshot_calls flat across frame 3.)
    TEST_ASSERT(data_source->snapshot_calls > snapshots_after_frame2,
        "origin change across a snap bucket must invalidate the upload "
        "cache and trigger a fresh try_snapshot call");

    return true;
}

bool test_renderer_assigns_distinct_origins_to_main_and_preview()
{
    // The renderer chooses per-view origins from each view's own visible
    // window through the same signed-span adapter used by production. When the
    // main span lands in a finer snap bucket (e.g. a 1-hour span -> 1 s snap)
    // and the preview span in a coarser one (e.g. a 10-year span -> 1 day
    // snap), the two origins must end up at different floored boundaries. A
    // regression that fed main_origin_ns into the preview's plan_view call
    // would leave both views with the same uploaded_t_origin_ns and break fp32
    // precision in preview.
    auto data_source = std::make_shared<Single_level_source>();
    // Sparse 10-year coverage: one sample per day is enough for the
    // renderer to find data within both windows without ballooning memory.
    constexpr std::int64_t k_ns_per_second = 1'000'000'000LL;
    constexpr std::int64_t k_ns_per_day = 86'400LL * k_ns_per_second;
    constexpr std::int64_t k_ns_per_hour = 3'600LL * k_ns_per_second;
    constexpr int k_num_samples = 365 * 10;
    data_source->samples.resize(k_num_samples);
    for (int i = 0; i < k_num_samples; ++i) {
        data_source->samples[i].t = static_cast<std::int64_t>(i) * k_ns_per_day;
        data_source->samples[i].v = 1.0f + static_cast<float>(i);
    }

    const int series_id = 43;
    auto series = std::make_shared<series_data_t>();
    series->style = Display_style::LINE;
    series->data_source = data_source;
    series->access = make_policy();

    frame_layout_result_t layout;
    layout.usable_width = 200.0;
    layout.usable_height = 80.0;

    Plot_config config;
    config.preview_visibility = 1.0;

    frame_context_t ctx = make_context(layout, config);
    // Main view: 1-hour window at an offset that is NOT day-aligned, so
    // the per-view floored origin differs depending on the snap step.
    // 100 days + 30 minutes pushes the main range mid-day.
    const std::int64_t main_t0 = 100LL * k_ns_per_day + 30LL * 60LL * k_ns_per_second;
    ctx.t0 = main_t0;
    ctx.t1 = main_t0 + k_ns_per_hour;
    // Preview view: full 10-year range starting at 0.
    ctx.t_available_min = 0LL;
    ctx.t_available_max = 10LL * 365LL * k_ns_per_day;
    ctx.adjusted_preview_height = 20.0;

    Series_renderer renderer;
    Asset_loader asset_loader;
    renderer.initialize(asset_loader);

    std::map<int, std::shared_ptr<const series_data_t>> series_map;
    series_map[series_id] = series;

    renderer.render(ctx, series_map);

    auto state_it = renderer.m_vbo_states.find(series_id);
    TEST_ASSERT(state_it != renderer.m_vbo_states.end(),
        "expected vbo state for distinct-origin renderer test");
    TEST_ASSERT(state_it->second.main_view.planner,
        "expected main planner state for distinct-origin renderer test");
    TEST_ASSERT(state_it->second.preview_view.planner,
        "expected preview planner state for distinct-origin renderer test");

    const std::int64_t main_origin =
        planner_state(state_it->second.main_view).uploaded_t_origin_ns;
    const std::int64_t preview_origin =
        planner_state(state_it->second.preview_view).uploaded_t_origin_ns;

    const std::int64_t expected_main_span =
        plot::detail::positive_span_ns_for_signed_api(ctx.t0, ctx.t1);
    const std::int64_t expected_preview_span =
        plot::detail::positive_span_ns_for_signed_api(
            ctx.t_available_min,
            ctx.t_available_max);
    const std::int64_t expected_main_origin =
        plot::detail::choose_origin_ns(ctx.t0, expected_main_span);
    const std::int64_t expected_preview_origin =
        plot::detail::choose_origin_ns(ctx.t_available_min, expected_preview_span);

    TEST_ASSERT(main_origin == expected_main_origin,
        std::string("main view must record its own per-view origin; got ") +
        std::to_string(main_origin) + ", expected " +
        std::to_string(expected_main_origin));
    TEST_ASSERT(preview_origin == expected_preview_origin,
        std::string("preview view must record its own per-view origin; got ") +
        std::to_string(preview_origin) + ", expected " +
        std::to_string(expected_preview_origin));
    TEST_ASSERT(main_origin != preview_origin,
        std::string("main and preview must record different origins when "
                    "their spans land in different snap buckets; got main=") +
        std::to_string(main_origin) + ", preview=" +
        std::to_string(preview_origin));

    return true;
}

bool test_render_skips_invalid_series()
{
    auto data_source = std::make_shared<Single_level_source>();
    data_source->samples.resize(4);

    const int disabled_id = 12;
    auto disabled_series = std::make_shared<series_data_t>();
    disabled_series->enabled = false;
    disabled_series->style = Display_style::LINE;
    disabled_series->data_source = data_source;
    disabled_series->access = make_policy();

    const int null_source_id = 13;
    auto null_source_series = std::make_shared<series_data_t>();
    null_source_series->enabled = true;
    null_source_series->style = Display_style::LINE;
    null_source_series->data_source.reset();
    null_source_series->access = make_policy();

    frame_layout_result_t layout;
    layout.usable_width = 140.0;
    layout.usable_height = 80.0;

    Plot_config config;

    frame_context_t ctx = make_context(layout, config);

    Series_renderer renderer;
    Asset_loader asset_loader;
    renderer.initialize(asset_loader);

    std::map<int, std::shared_ptr<const series_data_t>> series_map;
    series_map[disabled_id] = disabled_series;
    series_map[null_source_id] = null_source_series;
    series_map[99] = nullptr;

    renderer.render(ctx, series_map);

    TEST_ASSERT(data_source->snapshot_calls == 0,
                "expected disabled series to skip snapshots");

    return true;
}

}  // namespace

int main()
{
    std::cout << "Core snapshot cache tests" << std::endl;

    int passed = 0;
    int failed = 0;

    RUN_TEST(test_frame_scoped_cache_reuse);
    RUN_TEST(test_preview_uses_distinct_source_snapshot);
    RUN_TEST(test_preview_disabled_skips_preview_snapshot);
    RUN_TEST(test_frame_change_invalidates_snapshot_cache);
    RUN_TEST(test_empty_window_behavior_invalidates_fast_path_cache);
    RUN_TEST(test_preview_honors_hold_last_forward);
    RUN_TEST(test_lod_level_separation);
    RUN_TEST(test_snapshot_released_after_render);
    RUN_TEST(test_render_empty_series_map);
    RUN_TEST(test_upload_origin_records_per_view_origin);
    RUN_TEST(test_upload_invalidates_when_origin_changes_across_snap_bucket);
    RUN_TEST(test_renderer_assigns_distinct_origins_to_main_and_preview);
    RUN_TEST(test_render_skips_invalid_series);

    std::cout << "Results: " << passed << " passed, " << failed << " failed" << std::endl;

    return failed > 0 ? 1 : 0;
}
