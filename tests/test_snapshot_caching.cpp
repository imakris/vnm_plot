// vnm_plot core snapshot cache tests

#include <vnm_plot/core/asset_loader.h>
#define private public
#include <vnm_plot/core/series_renderer.h>
#undef private
#include <vnm_plot/core/plot_config.h>

#include <array>
#include <cassert>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace vnm::plot;

namespace {

struct Test_sample {
    double t = 0.0;
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
    policy.get_timestamp = [](const void* sample) {
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

Data_access_policy make_policy_with_clone()
{
    Data_access_policy policy = make_policy();
    policy.clone_with_timestamp = [](void* dst_sample, const void* src_sample, double timestamp) {
        if (!dst_sample || !src_sample) {
            return;
        }
        Test_sample tmp_sample{};
        std::memcpy(&tmp_sample, src_sample, sizeof(Test_sample));
        tmp_sample.t = timestamp;
        std::memcpy(dst_sample, &tmp_sample, sizeof(Test_sample));
    };
    return policy;
}

frame_context_t make_context(const frame_layout_result_t& layout, Plot_config& config)
{
    frame_context_t ctx{layout};
    ctx.t0 = 0.0;
    ctx.t1 = 10.0;
    ctx.t_available_min = 0.0;
    ctx.t_available_max = 10.0;
    ctx.win_w = 200;
    ctx.win_h = 120;
    ctx.config = &config;
    return ctx;
}

void fill_lod_samples(Two_level_source& source)
{
    source.lod0.resize(100);
    for (size_t i = 0; i < source.lod0.size(); ++i) {
        source.lod0[i].t = static_cast<double>(i);
        source.lod0[i].v = 1.0f + static_cast<float>(i);
    }
    source.lod1.resize(25);
    for (size_t i = 0; i < source.lod1.size(); ++i) {
        const size_t src = i * 4;
        source.lod1[i].t = static_cast<double>(src);
        source.lod1[i].v = 1.0f + static_cast<float>(src);
    }
}

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

bool test_frame_scoped_cache_reuse()
{
    auto data_source = std::make_shared<Single_level_source>();
    data_source->samples.resize(16);
    for (size_t i = 0; i < data_source->samples.size(); ++i) {
        data_source->samples[i].t = static_cast<double>(i);
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
    config.skip_gl_calls = true;
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

    return true;
}

bool test_preview_uses_distinct_source_snapshot()
{
    auto main_source = std::make_shared<Single_level_source>();
    auto preview_source = std::make_shared<Single_level_source>();
    main_source->samples.resize(8);
    preview_source->samples.resize(8);
    for (size_t i = 0; i < main_source->samples.size(); ++i) {
        main_source->samples[i].t = static_cast<double>(i);
        main_source->samples[i].v = 1.0f + static_cast<float>(i);
        preview_source->samples[i].t = static_cast<double>(i);
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
    config.skip_gl_calls = true;
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
    config.skip_gl_calls = true;
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
        data_source->samples[i].t = static_cast<double>(i);
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
    config.skip_gl_calls = true;

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
        data_source->samples[i].t = static_cast<double>(i);
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
    config.skip_gl_calls = true;

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
    // skip_gl mode does not allocate GL buffers, so seed active_vbo to
    // exercise the CPU fast-path conditions in process_view().
    state_it->second.main_view.active_vbo = 1u;

    renderer.render(ctx, series_map);
    TEST_ASSERT(data_source->snapshot_calls == 1,
                "expected fast-path cache hit to skip snapshot");

    series->empty_window_behavior = Empty_window_behavior::HOLD_LAST_FORWARD;
    renderer.render(ctx, series_map);
    TEST_ASSERT(data_source->snapshot_calls == 2,
                "expected empty_window_behavior change to invalidate fast-path cache");

    return true;
}

bool test_preview_does_not_hold_last_forward()
{
    auto data_source = std::make_shared<Single_level_source>();
    data_source->samples.resize(16);
    for (size_t i = 0; i < data_source->samples.size(); ++i) {
        data_source->samples[i].t = static_cast<double>(i);
        data_source->samples[i].v = 20.0f + static_cast<float>(i);
    }

    const int series_id = 19;
    auto series = std::make_shared<series_data_t>();
    series->style = Display_style::LINE;
    series->data_source = data_source;
    series->access = make_policy_with_clone();
    series->empty_window_behavior = Empty_window_behavior::HOLD_LAST_FORWARD;

    frame_layout_result_t layout;
    layout.usable_width = 160.0;
    layout.usable_height = 80.0;

    Plot_config config;
    config.skip_gl_calls = true;
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
    TEST_ASSERT(!state_it->second.preview_view.last_hold_last_forward,
                "preview should ignore HOLD_LAST_FORWARD and keep DRAW_NOTHING behavior");

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
    config.skip_gl_calls = true;

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

bool test_snapshot_released_on_series_removal()
{
    auto data_source = std::make_shared<Single_level_source>();
    data_source->samples.resize(8);
    for (size_t i = 0; i < data_source->samples.size(); ++i) {
        data_source->samples[i].t = static_cast<double>(i);
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
    config.skip_gl_calls = true;

    Series_renderer renderer;
    Asset_loader asset_loader;
    renderer.initialize(asset_loader);

    std::map<int, std::shared_ptr<const series_data_t>> series_map;
    series_map[series_id] = series;

    frame_context_t ctx = make_context(layout, config);
    renderer.render(ctx, series_map);

    std::weak_ptr<void> hold = data_source->last_hold;
    TEST_ASSERT(!hold.expired(), "expected snapshot hold to stay alive in cache");

    std::map<int, std::shared_ptr<const series_data_t>> replacement_map;
    const int placeholder_id = 99;
    auto placeholder = std::make_shared<series_data_t>();
    placeholder->enabled = false;
    replacement_map[placeholder_id] = placeholder;
    renderer.render(ctx, replacement_map);

    TEST_ASSERT(hold.expired(), "expected snapshot hold to release after series removal");

    return true;
}

bool test_render_empty_series_map()
{
    frame_layout_result_t layout;
    layout.usable_width = 120.0;
    layout.usable_height = 80.0;

    Plot_config config;
    config.skip_gl_calls = true;

    frame_context_t ctx = make_context(layout, config);

    Series_renderer renderer;
    Asset_loader asset_loader;
    renderer.initialize(asset_loader);

    std::map<int, std::shared_ptr<const series_data_t>> empty_map;
    renderer.render(ctx, empty_map);

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
    config.skip_gl_calls = true;

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
    RUN_TEST(test_preview_does_not_hold_last_forward);
    RUN_TEST(test_lod_level_separation);
    RUN_TEST(test_snapshot_released_on_series_removal);
    RUN_TEST(test_render_empty_series_map);
    RUN_TEST(test_render_skips_invalid_series);

    std::cout << "Results: " << passed << " passed, " << failed << " failed" << std::endl;

    return failed > 0 ? 1 : 0;
}
