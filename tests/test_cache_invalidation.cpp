// vnm_plot core cache tests

#include <vnm_plot/core/asset_loader.h>
#include <vnm_plot/core/series_renderer.h>
#include <vnm_plot/core/plot_config.h>

#include <glm/glm.hpp>

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
    double aux = 0.0;
};

class Lod_data_source final : public Data_source {
public:
    std::vector<Test_sample> lod0;
    std::vector<Test_sample> lod1;
    std::array<uint64_t, 2> sequences{{100, 50}};
    std::array<int, 2> snapshot_calls{{0, 0}};

    snapshot_result_t try_snapshot(size_t lod_level) override
    {
        if (lod_level >= 2) {
            return {data_snapshot_t{}, snapshot_result_t::Status::FAILED};
        }
        ++snapshot_calls[lod_level];
        const auto& data = (lod_level == 0) ? lod0 : lod1;
        auto hold = std::make_shared<int>(42);
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
            return {data_snapshot_t{}, snapshot_result_t::Status::EMPTY};
        }
        return {snapshot, snapshot_result_t::Status::OK};
    }

    size_t lod_levels() const override { return 2; }
    size_t lod_scale(size_t level) const override { return level == 0 ? 1 : 4; }
    size_t sample_stride() const override { return sizeof(Test_sample); }
    uint64_t current_sequence(size_t /*lod_level*/) const override { return 0; }

    bool has_aux_metric_range() const override { return true; }
    std::pair<double, double> aux_metric_range() const override { return {0.0, 10.0}; }
    bool aux_metric_range_needs_rescan() const override { return false; }
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
    policy.get_aux_metric = [](const void* sample) {
        return static_cast<const Test_sample*>(sample)->aux;
    };
    policy.sample_stride = sizeof(Test_sample);
    return policy;
}

frame_context_t make_context(const frame_layout_result_t& layout, Render_config& config)
{
    frame_context_t ctx{layout};
    ctx.t0 = 0.0;
    ctx.t1 = 399.0;
    ctx.t_available_min = 0.0;
    ctx.t_available_max = 399.0;
    ctx.win_w = 100;
    ctx.win_h = 100;
    ctx.config = &config;
    return ctx;
}

void fill_lod_data(Lod_data_source& ds)
{
    ds.lod0.resize(400);
    for (size_t i = 0; i < ds.lod0.size(); ++i) {
        ds.lod0[i].t = static_cast<double>(i);
        ds.lod0[i].v = 1.0f + static_cast<float>(i) * 0.01f;
        ds.lod0[i].aux = 0.5 + static_cast<double>(i) * 0.1;
    }

    ds.lod1.resize(100);
    for (size_t i = 0; i < ds.lod1.size(); ++i) {
        const size_t src = i * 4;
        ds.lod1[i].t = static_cast<double>(src);
        ds.lod1[i].v = 1.0f + static_cast<float>(src) * 0.01f;
        ds.lod1[i].aux = 0.5 + static_cast<double>(src) * 0.1;
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

bool test_lod0_sequence_fallback_calls_snapshot()
{
    auto data_source = std::make_shared<Lod_data_source>();
    fill_lod_data(*data_source);

    auto series = std::make_shared<series_data_t>();
    series->id = 1;
    series->style = Display_style::COLORMAP_AREA;
    series->data_source = data_source;
    series->access = make_policy();
    series->colormap_area.samples = {glm::vec4(0.0f, 0.0f, 0.0f, 1.0f),
                                     glm::vec4(1.0f, 1.0f, 1.0f, 1.0f)};

    frame_layout_result_t layout;
    layout.usable_width = 100.0;
    layout.usable_height = 80.0;

    Render_config config;
    config.skip_gl_calls = true;

    frame_context_t ctx = make_context(layout, config);

    Series_renderer renderer;
    Asset_loader asset_loader;
    renderer.initialize(asset_loader);

    std::map<int, std::shared_ptr<series_data_t>> series_map;
    series_map[series->id] = series;

    renderer.render(ctx, series_map);

    TEST_ASSERT(data_source->snapshot_calls[0] == 2,
                std::string("expected LOD0 snapshot for sequence fallback, got ") +
                    std::to_string(data_source->snapshot_calls[0]));
    TEST_ASSERT(data_source->snapshot_calls[1] == 1,
                std::string("expected single LOD1 snapshot, got ") +
                    std::to_string(data_source->snapshot_calls[1]));

    return true;
}

}  // namespace

int main()
{
    std::cout << "Core cache invalidation tests" << std::endl;

    int passed = 0;
    int failed = 0;

    RUN_TEST(test_lod0_sequence_fallback_calls_snapshot);

    std::cout << "Results: " << passed << " passed, " << failed << " failed" << std::endl;

    return failed > 0 ? 1 : 0;
}
