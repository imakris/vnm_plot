// vnm_plot QRhi series layer lifecycle tests

#include "test_macros.h"

#include <vnm_plot/core/asset_loader.h>
#include <vnm_plot/core/plot_config.h>
#include <vnm_plot/core/series_renderer.h>
#include <vnm_plot/qt/qrhi_series_layer.h>

#include <QColor>
#include <QSize>
#include <rhi/qrhi.h>

#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace plot = vnm::plot;

namespace {

struct test_sample_t
{
    std::int64_t timestamp_ns = 0;
    float value = 0.0f;
};

struct layer_event_t
{
    std::string layer_id;
    std::string action;
    bool resources_changed = false;
    plot::Series_view_kind view_kind = plot::Series_view_kind::MAIN;
    std::int32_t first = 0;
    std::int32_t count = 0;
    std::uint64_t sample_sequence = 0;
    std::int64_t t_min_ns = 0;
    std::int64_t t_max_ns = 0;
    std::int64_t t_origin_ns = 0;
};

class Test_source final : public plot::Data_source
{
public:
    Test_source()
    {
        set_samples({
            {0LL, 1.0f},
            {1'000'000'000LL, 2.0f},
            {2'000'000'000LL, 3.0f},
            {3'000'000'000LL, 4.0f},
            {4'000'000'000LL, 5.0f},
            {5'000'000'000LL, 6.0f},
            {6'000'000'000LL, 7.0f},
            {7'000'000'000LL, 8.0f}
        });
    }

    plot::snapshot_result_t try_snapshot(std::size_t lod_level) override
    {
        if (lod_level != 0) {
            return {plot::data_snapshot_t{}, plot::snapshot_result_t::Snapshot_status::FAILED};
        }
        plot::data_snapshot_t snapshot;
        snapshot.data = m_samples.data();
        snapshot.count = m_samples.size();
        snapshot.stride = sizeof(test_sample_t);
        snapshot.sequence = m_sequence;
        snapshot.hold = m_hold;
        return {
            snapshot,
            m_samples.empty()
                ? plot::snapshot_result_t::Snapshot_status::EMPTY
                : plot::snapshot_result_t::Snapshot_status::READY
        };
    }

    std::size_t sample_stride() const override { return sizeof(test_sample_t); }
    std::uint64_t current_sequence(std::size_t lod_level = 0) const override
    {
        (void)lod_level;
        return m_sequence;
    }
    const void* identity() const override { return m_identity; }

    void set_identity(const void* identity) { m_identity = identity; }

    void set_samples(std::vector<test_sample_t> samples)
    {
        m_samples = std::move(samples);
        ++m_sequence;
        m_hold = std::make_shared<int>(17);
    }

    void notify_changed()
    {
        ++m_sequence;
    }

private:
    std::vector<test_sample_t> m_samples;
    std::shared_ptr<void> m_hold;
    const void* m_identity = this;
    std::uint64_t m_sequence = 0;
};

plot::Data_access_policy make_access_policy()
{
    plot::Data_access_policy policy;
    policy.get_timestamp = [](const void* sample) {
        return static_cast<const test_sample_t*>(sample)->timestamp_ns;
    };
    policy.get_value = [](const void* sample) {
        return static_cast<const test_sample_t*>(sample)->value;
    };
    policy.get_range = [](const void* sample) {
        const float value = static_cast<const test_sample_t*>(sample)->value;
        return std::make_pair(value, value);
    };
    policy.layout_key = 11;
    return policy;
}

class Recording_layer_state final : public plot::Qrhi_series_layer_state
{
public:
    Recording_layer_state(std::string layer_id, std::vector<layer_event_t>& events)
    :
        m_layer_id(std::move(layer_id)),
        m_events(events)
    {}

    void cleanup_qrhi_resources(QRhi* rhi) override
    {
        (void)rhi;
        m_events.push_back({m_layer_id, "cleanup"});
    }

    bool prepare(const plot::qrhi_series_prepare_context_t& ctx) override
    {
        layer_event_t event;
        event.layer_id = m_layer_id;
        event.action = "prepare";
        event.resources_changed = ctx.resources_changed;
        event.view_kind = ctx.window.view_kind;
        event.first = ctx.window.first;
        event.count = ctx.window.count;
        event.sample_sequence = ctx.window.sample_sequence;
        event.t_min_ns = ctx.window.t_min_ns;
        event.t_max_ns = ctx.window.t_max_ns;
        event.t_origin_ns = ctx.window.t_origin_ns;
        m_events.push_back(event);
        return true;
    }

    void record(const plot::qrhi_series_record_context_t& ctx) override
    {
        layer_event_t event;
        event.layer_id = m_layer_id;
        event.action = "record";
        event.view_kind = ctx.window.view_kind;
        event.first = ctx.window.first;
        event.count = ctx.window.count;
        event.sample_sequence = ctx.window.sample_sequence;
        event.t_min_ns = ctx.window.t_min_ns;
        event.t_max_ns = ctx.window.t_max_ns;
        event.t_origin_ns = ctx.window.t_origin_ns;
        m_events.push_back(event);
    }

private:
    std::string m_layer_id;
    std::vector<layer_event_t>& m_events;
};

class Recording_layer final : public plot::Qrhi_series_layer
{
public:
    Recording_layer(
        std::string id,
        std::uint64_t revision,
        int z_order,
        std::vector<layer_event_t>& events,
        int& create_count)
    :
        m_id(std::move(id)),
        m_revision(revision),
        m_z_order(z_order),
        m_events(events),
        m_create_count(create_count)
    {}

    std::string_view id() const override { return m_id; }
    std::uint64_t revision() const override { return m_revision; }
    int z_order() const override { return m_z_order; }

    bool draws_view(plot::Series_view_kind view_kind) const override
    {
        return view_kind == plot::Series_view_kind::MAIN;
    }

    std::unique_ptr<plot::Qrhi_series_layer_state> create_state(QRhi& rhi) const override
    {
        (void)rhi;
        ++m_create_count;
        m_events.push_back({m_id, "create"});
        return std::make_unique<Recording_layer_state>(m_id, m_events);
    }

    void set_id(std::string id) { m_id = std::move(id); }
    void set_revision(std::uint64_t revision) { m_revision = revision; }

private:
    std::string m_id;
    std::uint64_t m_revision = 0;
    int m_z_order = 0;
    std::vector<layer_event_t>& m_events;
    int& m_create_count;
};

class Offscreen_rhi_fixture
{
public:
    bool initialize(std::string& error_message)
    {
        QRhiNullInitParams params;
        m_rhi.reset(QRhi::create(QRhi::Null, &params));
        if (!m_rhi) {
            error_message = "failed to create QRhi Null backend";
            return false;
        }

        const QSize size(320, 180);
        m_color_buffer.reset(m_rhi->newRenderBuffer(
            QRhiRenderBuffer::Color,
            size,
            1,
            {},
            QRhiTexture::RGBA8));
        if (!m_color_buffer || !m_color_buffer->create()) {
            error_message = "failed to create offscreen color buffer";
            return false;
        }

        QRhiColorAttachment color_attachment;
        color_attachment.setRenderBuffer(m_color_buffer.get());
        QRhiTextureRenderTargetDescription target_description(color_attachment);
        m_render_target.reset(m_rhi->newTextureRenderTarget(target_description));
        m_render_pass_descriptor.reset(m_render_target->newCompatibleRenderPassDescriptor());
        m_render_target->setRenderPassDescriptor(m_render_pass_descriptor.get());
        if (!m_render_target || !m_render_target->create()) {
            error_message = "failed to create offscreen render target";
            return false;
        }

        return true;
    }

    QRhi* rhi() const { return m_rhi.get(); }
    QRhiTextureRenderTarget* render_target() const { return m_render_target.get(); }

    bool render_layer_frame(
        plot::Series_renderer& renderer,
        plot::frame_context_t& ctx,
        const std::map<int, std::shared_ptr<const plot::series_data_t>>& series_map,
        std::vector<layer_event_t>& events,
        std::string& error_message)
    {
        QRhiCommandBuffer* command_buffer = nullptr;
        if (m_rhi->beginOffscreenFrame(&command_buffer) != QRhi::FrameOpSuccess || !command_buffer) {
            error_message = "QRhi beginOffscreenFrame failed";
            return false;
        }

        QRhiResourceUpdateBatch* updates = m_rhi->nextResourceUpdateBatch();
        ctx.rhi = m_rhi.get();
        ctx.cb = command_buffer;
        ctx.render_target = m_render_target.get();
        ctx.rhi_updates = updates;

        renderer.prepare(ctx, series_map);
        events.push_back({"frame", "begin_pass"});
        command_buffer->beginPass(
            m_render_target.get(),
            QColor::fromRgbF(0.0, 0.0, 0.0, 1.0),
            QRhiDepthStencilClearValue(1.0f, 0),
            updates);
        renderer.render(ctx, series_map);
        command_buffer->endPass();
        m_rhi->endOffscreenFrame();
        return true;
    }

private:
    std::unique_ptr<QRhi> m_rhi;
    std::unique_ptr<QRhiRenderBuffer> m_color_buffer;
    std::unique_ptr<QRhiTextureRenderTarget> m_render_target;
    std::unique_ptr<QRhiRenderPassDescriptor> m_render_pass_descriptor;
};

plot::frame_layout_result_t make_layout()
{
    plot::frame_layout_result_t layout;
    layout.usable_width = 240.0;
    layout.usable_height = 120.0;
    return layout;
}

plot::frame_context_t make_context(const plot::frame_layout_result_t& layout, const plot::Plot_config& config)
{
    plot::frame_context_t ctx{layout};
    ctx.t0 = 0;
    ctx.t1 = 3'000'000'000LL;
    ctx.t_available_min = 0;
    ctx.t_available_max = 7'000'000'000LL;
    ctx.v0 = 0.0f;
    ctx.v1 = 10.0f;
    ctx.win_w = 320;
    ctx.win_h = 180;
    ctx.config = &config;
    return ctx;
}

std::shared_ptr<plot::series_data_t> make_layer_only_series(
    std::shared_ptr<Test_source> source,
    std::vector<std::shared_ptr<const plot::Qrhi_series_layer>> layers)
{
    auto series = std::make_shared<plot::series_data_t>();
    series->style = static_cast<plot::Display_style>(0);
    series->data_source = source;
    series->access = make_access_policy();
    series->qrhi_layers = std::move(layers);
    return series;
}

const layer_event_t* find_prepare_event(
    const std::vector<layer_event_t>& events,
    std::string_view layer_id)
{
    for (const auto& event : events) {
        if (event.layer_id == layer_id && event.action == "prepare") {
            return &event;
        }
    }
    return nullptr;
}

std::size_t find_event_index(
    const std::vector<layer_event_t>& events,
    std::string_view layer_id,
    std::string_view action)
{
    for (std::size_t i = 0; i < events.size(); ++i) {
        if (events[i].layer_id == layer_id && events[i].action == action) {
            return i;
        }
    }
    return events.size();
}

bool test_layer_only_zero_style_prepare_record_order()
{
    std::vector<layer_event_t> events;
    int low_create_count = 0;
    int high_create_count = 0;
    auto low_layer = std::make_shared<Recording_layer>(
        "low", 1, -5, events, low_create_count);
    auto high_layer = std::make_shared<Recording_layer>(
        "high", 1, 10, events, high_create_count);

    auto source = std::make_shared<Test_source>();
    auto series = make_layer_only_series(source, {high_layer, low_layer});
    std::map<int, std::shared_ptr<const plot::series_data_t>> series_map;
    series_map[7] = series;

    plot::Asset_loader asset_loader;
    plot::Series_renderer renderer;
    renderer.initialize(asset_loader);

    Offscreen_rhi_fixture rhi_fixture;
    std::string error_message;
    TEST_ASSERT(rhi_fixture.initialize(error_message), error_message);

    plot::Plot_config config;
    const plot::frame_layout_result_t layout = make_layout();
    plot::frame_context_t ctx = make_context(layout, config);
    TEST_ASSERT(
        rhi_fixture.render_layer_frame(renderer, ctx, series_map, events, error_message),
        error_message);

    TEST_ASSERT(low_create_count == 1, "low z-order layer state should be created once");
    TEST_ASSERT(high_create_count == 1, "high z-order layer state should be created once");
    const std::size_t low_prepare = find_event_index(events, "low", "prepare");
    const std::size_t high_prepare = find_event_index(events, "high", "prepare");
    const std::size_t begin_pass = find_event_index(events, "frame", "begin_pass");
    const std::size_t low_record = find_event_index(events, "low", "record");
    const std::size_t high_record = find_event_index(events, "high", "record");

    TEST_ASSERT(low_prepare < events.size(), "expected low layer prepare event");
    TEST_ASSERT(high_prepare < events.size(), "expected high layer prepare event");
    TEST_ASSERT(begin_pass < events.size(), "expected beginPass event");
    TEST_ASSERT(low_record < events.size(), "expected low layer record event");
    TEST_ASSERT(high_record < events.size(), "expected high layer record event");
    TEST_ASSERT(low_prepare < high_prepare, "lower z-order layer must prepare first");
    TEST_ASSERT(high_prepare < begin_pass, "prepare must finish before beginPass");
    TEST_ASSERT(begin_pass < low_record, "record must happen after beginPass");
    TEST_ASSERT(low_record < high_record, "lower z-order layer must record first");
    TEST_ASSERT(events[low_prepare].count > 0 && events[high_prepare].count > 0,
        "layer-only series with zero Display_style must not be skipped");

    return true;
}

bool test_resources_changed_tracks_data_and_window_changes()
{
    std::vector<layer_event_t> events;
    int create_count = 0;
    auto layer = std::make_shared<Recording_layer>(
        "tracked", 1, 0, events, create_count);
    auto source = std::make_shared<Test_source>();
    auto series = make_layer_only_series(source, {layer});
    std::map<int, std::shared_ptr<const plot::series_data_t>> series_map;
    series_map[9] = series;

    plot::Asset_loader asset_loader;
    plot::Series_renderer renderer;
    renderer.initialize(asset_loader);

    Offscreen_rhi_fixture rhi_fixture;
    std::string error_message;
    TEST_ASSERT(rhi_fixture.initialize(error_message), error_message);

    plot::Plot_config config;
    const plot::frame_layout_result_t layout = make_layout();
    plot::frame_context_t ctx = make_context(layout, config);

    TEST_ASSERT(
        rhi_fixture.render_layer_frame(renderer, ctx, series_map, events, error_message),
        error_message);
    const layer_event_t* initial_prepare = find_prepare_event(events, "tracked");
    TEST_ASSERT(initial_prepare && initial_prepare->resources_changed,
        "first prepare must report resources_changed");

    events.clear();
    TEST_ASSERT(
        rhi_fixture.render_layer_frame(renderer, ctx, series_map, events, error_message),
        error_message);
    const layer_event_t* stable_prepare = find_prepare_event(events, "tracked");
    TEST_ASSERT(stable_prepare && !stable_prepare->resources_changed,
        "unchanged data and window must not report resources_changed");

    source->notify_changed();
    events.clear();
    TEST_ASSERT(
        rhi_fixture.render_layer_frame(renderer, ctx, series_map, events, error_message),
        error_message);
    const layer_event_t* data_prepare = find_prepare_event(events, "tracked");
    TEST_ASSERT(data_prepare && data_prepare->resources_changed,
        "sample sequence change must report resources_changed");

    events.clear();
    ctx.t0 = 4'000'000'000LL;
    ctx.t1 = 7'000'000'000LL;
    TEST_ASSERT(
        rhi_fixture.render_layer_frame(renderer, ctx, series_map, events, error_message),
        error_message);
    const layer_event_t* window_prepare = find_prepare_event(events, "tracked");
    TEST_ASSERT(window_prepare && window_prepare->resources_changed,
        "visible window change must report resources_changed");
    TEST_ASSERT(window_prepare->t_min_ns == ctx.t0 && window_prepare->t_max_ns == ctx.t1,
        "prepare context must carry the changed visible window");

    return true;
}

bool test_layer_state_recreated_for_program_identity_changes()
{
    std::vector<layer_event_t> events;
    int create_count = 0;
    auto layer = std::make_shared<Recording_layer>(
        "program", 1, 0, events, create_count);
    auto source = std::make_shared<Test_source>();
    auto series = make_layer_only_series(source, {layer});
    std::map<int, std::shared_ptr<const plot::series_data_t>> series_map;
    series_map[11] = series;

    plot::Asset_loader asset_loader;
    plot::Series_renderer renderer;
    renderer.initialize(asset_loader);

    Offscreen_rhi_fixture rhi_fixture;
    std::string error_message;
    TEST_ASSERT(rhi_fixture.initialize(error_message), error_message);

    plot::Plot_config config;
    const plot::frame_layout_result_t layout = make_layout();
    plot::frame_context_t ctx = make_context(layout, config);

    TEST_ASSERT(
        rhi_fixture.render_layer_frame(renderer, ctx, series_map, events, error_message),
        error_message);
    TEST_ASSERT(create_count == 1, "initial frame must create one layer state");

    events.clear();
    TEST_ASSERT(
        rhi_fixture.render_layer_frame(renderer, ctx, series_map, events, error_message),
        error_message);
    TEST_ASSERT(create_count == 1, "unchanged program key must reuse the layer state");

    layer->set_revision(2);
    events.clear();
    TEST_ASSERT(
        rhi_fixture.render_layer_frame(renderer, ctx, series_map, events, error_message),
        error_message);
    TEST_ASSERT(create_count == 2, "revision change must create a new layer state");

    layer->set_id("program-renamed");
    events.clear();
    TEST_ASSERT(
        rhi_fixture.render_layer_frame(renderer, ctx, series_map, events, error_message),
        error_message);
    TEST_ASSERT(create_count == 3, "layer id change must create a new layer state");

    int identity_token = 0;
    source->set_identity(&identity_token);
    events.clear();
    TEST_ASSERT(
        rhi_fixture.render_layer_frame(renderer, ctx, series_map, events, error_message),
        error_message);
    TEST_ASSERT(create_count == 4, "data identity change must create a new layer state");

    return true;
}

} // namespace

int main()
{
    std::cout << "QRhi layer lifecycle tests" << std::endl;

    int passed = 0;
    int failed = 0;

    RUN_TEST(test_layer_only_zero_style_prepare_record_order);
    RUN_TEST(test_resources_changed_tracks_data_and_window_changes);
    RUN_TEST(test_layer_state_recreated_for_program_identity_changes);

    std::cout << "Results: " << passed << " passed, " << failed << " failed" << std::endl;
    return failed > 0 ? 1 : 0;
}
