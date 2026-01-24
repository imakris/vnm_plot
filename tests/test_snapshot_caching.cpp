// vnm_plot core snapshot cache tests

#include <vnm_plot/core/asset_loader.h>
#include <vnm_plot/core/series_renderer.h>
#include <vnm_plot/core/plot_config.h>

#include <array>
#include <cassert>
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
            return {data_snapshot_t{}, snapshot_result_t::Status::FAILED};
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
            return {data_snapshot_t{}, snapshot_result_t::Status::EMPTY};
        }
        return {snapshot, snapshot_result_t::Status::OK};
    }

    size_t lod_levels() const override { return 1; }
    size_t lod_scale(size_t /*level*/) const override { return 1; }
    size_t sample_stride() const override { return sizeof(Test_sample); }
    uint64_t current_sequence(size_t /*lod_level*/) const override { return sequence; }
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
    policy.sample_stride = sizeof(Test_sample);
    return policy;
}

frame_context_t make_context(const frame_layout_result_t& layout, Render_config& config)
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

    auto series = std::make_shared<series_data_t>();
    series->id = 7;
    series->style = Display_style::LINE;
    series->data_source = data_source;
    series->access = make_policy();

    frame_layout_result_t layout;
    layout.usable_width = 200.0;
    layout.usable_height = 80.0;

    Render_config config;
    config.skip_gl_calls = true;
    config.preview_visibility = 1.0;

    frame_context_t ctx = make_context(layout, config);
    ctx.adjusted_preview_height = 20.0;

    Series_renderer renderer;
    Asset_loader asset_loader;
    renderer.initialize(asset_loader);

    std::map<int, std::shared_ptr<series_data_t>> series_map;
    series_map[series->id] = series;

    renderer.render(ctx, series_map);

    TEST_ASSERT(data_source->snapshot_calls == 1,
                std::string("expected shared snapshot between main and preview views, got ") +
                    std::to_string(data_source->snapshot_calls));

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

    auto series = std::make_shared<series_data_t>();
    series->id = 3;
    series->style = Display_style::LINE;
    series->data_source = data_source;
    series->access = make_policy();

    frame_layout_result_t layout;
    layout.usable_width = 160.0;
    layout.usable_height = 80.0;

    Render_config config;
    config.skip_gl_calls = true;

    Series_renderer renderer;
    Asset_loader asset_loader;
    renderer.initialize(asset_loader);

    std::map<int, std::shared_ptr<series_data_t>> series_map;
    series_map[series->id] = series;

    frame_context_t ctx = make_context(layout, config);
    renderer.render(ctx, series_map);

    std::weak_ptr<void> hold = data_source->last_hold;
    TEST_ASSERT(!hold.expired(), "expected snapshot hold to stay alive in cache");

    std::map<int, std::shared_ptr<series_data_t>> replacement_map;
    auto placeholder = std::make_shared<series_data_t>();
    placeholder->id = 99;
    placeholder->enabled = false;
    replacement_map[placeholder->id] = placeholder;
    renderer.render(ctx, replacement_map);

    TEST_ASSERT(hold.expired(), "expected snapshot hold to release after series removal");

    return true;
}

}  // namespace

int main()
{
    std::cout << "Core snapshot cache tests" << std::endl;

    int passed = 0;
    int failed = 0;

    RUN_TEST(test_frame_scoped_cache_reuse);
    RUN_TEST(test_snapshot_released_on_series_removal);

    std::cout << "Results: " << passed << " passed, " << failed << " failed" << std::endl;

    return failed > 0 ? 1 : 0;
}
