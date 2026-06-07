// vnm_plot QRhi series layer lifecycle tests

#include "test_macros.h"

#include <vnm_plot/core/asset_loader.h>
#include <vnm_plot/core/plot_config.h>
#define private public
#include <vnm_plot/core/series_renderer.h>
#undef private
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
    bool snapshot_valid = false;
    std::size_t snapshot_count = 0;
    plot::Series_view_kind view_kind = plot::Series_view_kind::MAIN;
    std::size_t source_first = 0;
    std::size_t source_count = 0;
    std::size_t synthetic_hold_count = 0;
    std::size_t gpu_count = 0;
    std::uint64_t sample_sequence = 0;
    std::uint64_t snapshot_sequence = 0;
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
        ++m_snapshot_calls;
        if (lod_level != 0) {
            return {plot::data_snapshot_t{}, plot::snapshot_result_t::Snapshot_status::FAILED};
        }
        if (m_busy_snapshots_remaining > 0) {
            --m_busy_snapshots_remaining;
            return {plot::data_snapshot_t{}, plot::snapshot_result_t::Snapshot_status::BUSY};
        }
        if (m_advance_during_next_snapshot) {
            m_advance_during_next_snapshot = false;
            ++m_sequence;
        }
        plot::data_snapshot_t snapshot;
        snapshot.data = m_samples.data();
        snapshot.count = m_samples.size();
        snapshot.stride = sizeof(test_sample_t);
        snapshot.sequence = m_sequence;
        auto hold = std::make_shared<int>(17);
        m_last_hold = hold;
        snapshot.hold = hold;
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
    }

    std::size_t sample_count() const { return m_samples.size(); }

    void notify_changed()
    {
        ++m_sequence;
    }

    int snapshot_calls() const { return m_snapshot_calls; }
    std::weak_ptr<void> last_hold() const { return m_last_hold; }
    void return_busy_once() { m_busy_snapshots_remaining = 1; }
    void advance_during_next_snapshot() { m_advance_during_next_snapshot = true; }

private:
    std::vector<test_sample_t> m_samples;
    std::weak_ptr<void> m_last_hold;
    const void* m_identity = this;
    std::uint64_t m_sequence = 0;
    int m_snapshot_calls = 0;
    int m_busy_snapshots_remaining = 0;
    bool m_advance_during_next_snapshot = false;
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
        event.snapshot_valid = static_cast<bool>(ctx.window.snapshot);
        event.snapshot_count = ctx.window.snapshot.count;
        event.snapshot_sequence = ctx.window.snapshot.sequence;
        event.view_kind = ctx.window.view_kind;
        event.source_first = ctx.window.source_first;
        event.source_count = ctx.window.source_count;
        event.synthetic_hold_count = ctx.window.synthetic_hold_count;
        event.gpu_count = ctx.window.gpu_count;
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
        event.snapshot_valid = static_cast<bool>(ctx.window.snapshot);
        event.snapshot_count = ctx.window.snapshot.count;
        event.snapshot_sequence = ctx.window.snapshot.sequence;
        event.view_kind = ctx.window.view_kind;
        event.source_first = ctx.window.source_first;
        event.source_count = ctx.window.source_count;
        event.synthetic_hold_count = ctx.window.synthetic_hold_count;
        event.gpu_count = ctx.window.gpu_count;
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
    series->style = plot::Display_style::NONE;
    series->data_source = source;
    series->access = make_access_policy();
    series->qrhi_layers = std::move(layers);
    return series;
}

std::shared_ptr<plot::series_data_t> make_builtin_plus_layer_series(
    std::shared_ptr<Test_source> source,
    plot::Display_style style,
    std::vector<std::shared_ptr<const plot::Qrhi_series_layer>> layers)
{
    auto series = std::make_shared<plot::series_data_t>();
    series->style = style;
    series->data_source = source;
    series->access = make_access_policy();
    series->qrhi_layers = std::move(layers);
    return series;
}

std::shared_ptr<plot::series_data_t> make_line_plus_layer_series(
    std::shared_ptr<Test_source> source,
    std::vector<std::shared_ptr<const plot::Qrhi_series_layer>> layers)
{
    return make_builtin_plus_layer_series(
        std::move(source),
        plot::Display_style::LINE,
        std::move(layers));
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

bool assert_compact_upload_state(
    const plot::Series_renderer& renderer,
    int series_id,
    const layer_event_t& prepare,
    std::size_t expected_line_window_sample_count,
    std::string_view label)
{
    constexpr std::size_t k_gpu_sample_bytes = sizeof(float) * 4u;

    auto state_it = renderer.m_vbo_states.find(series_id);
    TEST_ASSERT(state_it != renderer.m_vbo_states.end(),
        std::string("expected renderer VBO state for ") + std::string(label));
    const auto& view_state = state_it->second.main_view;
    TEST_ASSERT(view_state.last_staged_sample_count == prepare.gpu_count,
        std::string(label) + " upload must stage the compact GPU window; got " +
        std::to_string(view_state.last_staged_sample_count) + ", expected " +
        std::to_string(prepare.gpu_count));
    TEST_ASSERT(view_state.last_sample_upload_bytes ==
            prepare.gpu_count * k_gpu_sample_bytes,
        std::string(label) + " upload bytes must match compact GPU sample count");
    TEST_ASSERT(view_state.last_sample_upload_bytes <
            prepare.snapshot_count * k_gpu_sample_bytes,
        std::string(label) + " upload bytes must not scale with full snapshot count");
    TEST_ASSERT(
        view_state.last_line_window_sample_count ==
            expected_line_window_sample_count,
        std::string(label) + " line-window sample count mismatch");

    return true;
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
    TEST_ASSERT(events[low_prepare].gpu_count > 0 && events[high_prepare].gpu_count > 0,
        "layer-only series with zero Display_style must not be skipped");
    TEST_ASSERT(events[low_prepare].snapshot_valid && events[high_prepare].snapshot_valid,
        "layer-only external prepares must receive a valid snapshot");

    return true;
}

bool test_builtin_upload_stages_visible_window_only()
{
    constexpr std::int64_t k_second_ns = 1'000'000'000LL;

    std::vector<layer_event_t> events;
    int create_count = 0;
    auto layer = std::make_shared<Recording_layer>(
        "visible-upload", 1, 20, events, create_count);
    auto source = std::make_shared<Test_source>();
    std::vector<test_sample_t> samples;
    samples.reserve(128);
    for (std::size_t i = 0; i < 128; ++i) {
        samples.push_back({
            static_cast<std::int64_t>(i) * k_second_ns,
            static_cast<float>(i)
        });
    }
    source->set_samples(std::move(samples));

    auto series = make_line_plus_layer_series(source, {layer});
    std::map<int, std::shared_ptr<const plot::series_data_t>> series_map;
    const int series_id = 31;
    series_map[series_id] = series;

    plot::Asset_loader asset_loader;
    plot::Series_renderer renderer;
    renderer.initialize(asset_loader);

    Offscreen_rhi_fixture rhi_fixture;
    std::string error_message;
    TEST_ASSERT(rhi_fixture.initialize(error_message), error_message);

    plot::Plot_config config;
    const plot::frame_layout_result_t layout = make_layout();
    plot::frame_context_t ctx = make_context(layout, config);
    ctx.t0 = 40LL * k_second_ns;
    ctx.t1 = 42LL * k_second_ns;
    ctx.t_available_min = ctx.t0;
    ctx.t_available_max = ctx.t1;

    TEST_ASSERT(
        rhi_fixture.render_layer_frame(renderer, ctx, series_map, events, error_message),
        error_message);
    const layer_event_t* prepare = find_prepare_event(events, "visible-upload");
    TEST_ASSERT(prepare, "expected visible-upload layer prepare event");
    TEST_ASSERT(prepare->snapshot_count == source->sample_count(),
        "external layer should still receive the full frame snapshot");
    TEST_ASSERT(prepare->source_count > 0,
        "visible source window should contain samples");
    TEST_ASSERT(prepare->source_count < prepare->snapshot_count,
        "visible source window should be smaller than the full source snapshot");
    TEST_ASSERT(prepare->synthetic_hold_count == 0,
        "ordinary visible upload should not add a synthetic hold sample");
    TEST_ASSERT(prepare->gpu_count == prepare->source_count,
        "linear visible upload should stage one GPU sample per source sample");

    if (!assert_compact_upload_state(
            renderer,
            series_id,
            *prepare,
            prepare->gpu_count,
            "linear line"))
    {
        return false;
    }

    return true;
}

bool test_builtin_upload_reuses_vbo_capacity_headroom()
{
    constexpr std::int64_t k_second_ns = 1'000'000'000LL;
    constexpr std::size_t k_gpu_sample_bytes = sizeof(float) * 4u;

    std::vector<layer_event_t> events;
    int create_count = 0;
    auto layer = std::make_shared<Recording_layer>(
        "capacity-upload", 1, 20, events, create_count);
    auto source = std::make_shared<Test_source>();
    std::vector<test_sample_t> samples;
    samples.reserve(128);
    for (std::size_t i = 0; i < 128; ++i) {
        samples.push_back({
            static_cast<std::int64_t>(i) * k_second_ns,
            static_cast<float>(i)
        });
    }
    source->set_samples(std::move(samples));

    auto series = make_line_plus_layer_series(source, {layer});
    std::map<int, std::shared_ptr<const plot::series_data_t>> series_map;
    const int series_id = 38;
    series_map[series_id] = series;

    plot::Asset_loader asset_loader;
    plot::Series_renderer renderer;
    renderer.initialize(asset_loader);

    Offscreen_rhi_fixture rhi_fixture;
    std::string error_message;
    TEST_ASSERT(rhi_fixture.initialize(error_message), error_message);

    plot::Plot_config config;
    const plot::frame_layout_result_t layout = make_layout();
    plot::frame_context_t ctx = make_context(layout, config);
    ctx.t0 = 40LL * k_second_ns;
    ctx.t1 = 41LL * k_second_ns;
    ctx.t_available_min = ctx.t0;
    ctx.t_available_max = ctx.t1;

    TEST_ASSERT(
        rhi_fixture.render_layer_frame(renderer, ctx, series_map, events, error_message),
        error_message);
    const layer_event_t* first_prepare =
        find_prepare_event(events, "capacity-upload");
    TEST_ASSERT(first_prepare && first_prepare->gpu_count == 5,
        "first capacity test frame should stage five GPU samples");

    auto state_it = renderer.m_vbo_states.find(series_id);
    TEST_ASSERT(state_it != renderer.m_vbo_states.end(),
        "expected renderer VBO state for capacity test");
    const auto& first_view_state = state_it->second.main_view;
    const std::size_t first_capacity =
        first_view_state.rhi_vbo_capacity_bytes;
    const std::size_t first_generation =
        first_view_state.last_vbo_generation;
    TEST_ASSERT(first_capacity >= first_prepare->gpu_count * k_gpu_sample_bytes,
        "first capacity test frame should allocate enough VBO bytes");

    events.clear();
    ctx.t1 = 42LL * k_second_ns;
    ctx.t_available_max = ctx.t1;
    TEST_ASSERT(
        rhi_fixture.render_layer_frame(renderer, ctx, series_map, events, error_message),
        error_message);
    const layer_event_t* second_prepare =
        find_prepare_event(events, "capacity-upload");
    TEST_ASSERT(second_prepare && second_prepare->gpu_count == 6,
        "second capacity test frame should grow to six GPU samples");

    const auto& second_view_state = state_it->second.main_view;
    TEST_ASSERT(
        second_prepare->gpu_count * k_gpu_sample_bytes <= first_capacity,
        "second capacity test frame must stay within first allocation headroom");
    TEST_ASSERT(second_view_state.rhi_vbo_capacity_bytes == first_capacity,
        "compact VBO capacity should stay stable inside existing headroom");
    TEST_ASSERT(second_view_state.last_vbo_generation == first_generation,
        "compact VBO must not be reallocated inside existing headroom");

    return true;
}

bool test_combined_builtin_uploads_samples_once_per_view()
{
    constexpr std::int64_t k_second_ns = 1'000'000'000LL;
    constexpr std::size_t k_gpu_sample_bytes = sizeof(float) * 4u;

    std::vector<layer_event_t> events;
    int create_count = 0;
    auto layer = std::make_shared<Recording_layer>(
        "combined-upload", 1, 20, events, create_count);
    auto source = std::make_shared<Test_source>();
    std::vector<test_sample_t> samples;
    samples.reserve(128);
    for (std::size_t i = 0; i < 128; ++i) {
        samples.push_back({
            static_cast<std::int64_t>(i) * k_second_ns,
            static_cast<float>(i)
        });
    }
    source->set_samples(std::move(samples));

    auto series = make_builtin_plus_layer_series(
        source,
        plot::Display_style::DOTS_LINE_AREA,
        {layer});
    std::map<int, std::shared_ptr<const plot::series_data_t>> series_map;
    const int series_id = 41;
    series_map[series_id] = series;

    plot::Asset_loader asset_loader;
    plot::Series_renderer renderer;
    renderer.initialize(asset_loader);

    Offscreen_rhi_fixture rhi_fixture;
    std::string error_message;
    TEST_ASSERT(rhi_fixture.initialize(error_message), error_message);

    plot::Plot_config config;
    const plot::frame_layout_result_t layout = make_layout();
    plot::frame_context_t ctx = make_context(layout, config);
    ctx.t0 = 40LL * k_second_ns;
    ctx.t1 = 42LL * k_second_ns;
    ctx.t_available_min = ctx.t0;
    ctx.t_available_max = ctx.t1;

    TEST_ASSERT(
        rhi_fixture.render_layer_frame(renderer, ctx, series_map, events, error_message),
        error_message);
    const layer_event_t* prepare = find_prepare_event(events, "combined-upload");
    TEST_ASSERT(prepare, "expected combined-upload layer prepare event");
    TEST_ASSERT(prepare->gpu_count > 0,
        "combined upload test should plan a drawable GPU window");

    auto state_it = renderer.m_vbo_states.find(series_id);
    TEST_ASSERT(state_it != renderer.m_vbo_states.end(),
        "expected renderer VBO state for combined upload test");
    const auto& view_state = state_it->second.main_view;
    TEST_ASSERT(view_state.last_primitive_prepare_count == 3,
        "combined DOTS_LINE_AREA style must prepare three built-in primitives");
    TEST_ASSERT(view_state.last_sample_upload_count == 1,
        "combined DOTS_LINE_AREA style must upload compact samples once");
    TEST_ASSERT(view_state.last_staged_sample_count == prepare->gpu_count,
        "combined style upload must stage the shared compact GPU window");
    TEST_ASSERT(view_state.last_sample_upload_bytes ==
            prepare->gpu_count * k_gpu_sample_bytes,
        "combined style upload bytes must match one compact GPU window");
    TEST_ASSERT(view_state.last_line_window_sample_count == prepare->gpu_count,
        "combined style LINE primitive should still prepare its padded window");

    events.clear();
    TEST_ASSERT(
        rhi_fixture.render_layer_frame(renderer, ctx, series_map, events, error_message),
        error_message);
    const layer_event_t* second_prepare =
        find_prepare_event(events, "combined-upload");
    TEST_ASSERT(second_prepare,
        "expected second combined-upload layer prepare event");
    TEST_ASSERT(view_state.last_primitive_prepare_count == 3,
        "combined primitive prepare count must describe the current frame");
    TEST_ASSERT(view_state.last_sample_upload_count == 1,
        "combined sample upload count must describe the current frame");
    TEST_ASSERT(view_state.last_staged_sample_count == second_prepare->gpu_count,
        "second combined frame must stage the shared compact GPU window");

    return true;
}

bool test_builtin_upload_stages_visible_windows_for_dots_and_area()
{
    constexpr std::int64_t k_second_ns = 1'000'000'000LL;

    struct upload_case_t
    {
        plot::Display_style style = plot::Display_style::DOTS;
        std::string_view layer_id;
        int series_id = 0;
    };

    const upload_case_t cases[] = {
        {plot::Display_style::DOTS, "visible-dots-upload", 33},
        {plot::Display_style::AREA, "visible-area-upload", 34}
    };

    for (const auto& test_case : cases) {
        std::vector<layer_event_t> events;
        int create_count = 0;
        auto layer = std::make_shared<Recording_layer>(
            std::string(test_case.layer_id), 1, 20, events, create_count);
        auto source = std::make_shared<Test_source>();
        std::vector<test_sample_t> samples;
        samples.reserve(128);
        for (std::size_t i = 0; i < 128; ++i) {
            samples.push_back({
                static_cast<std::int64_t>(i) * k_second_ns,
                static_cast<float>(i)
            });
        }
        source->set_samples(std::move(samples));

        auto series = make_builtin_plus_layer_series(
            source,
            test_case.style,
            {layer});
        std::map<int, std::shared_ptr<const plot::series_data_t>> series_map;
        series_map[test_case.series_id] = series;

        plot::Asset_loader asset_loader;
        plot::Series_renderer renderer;
        renderer.initialize(asset_loader);

        Offscreen_rhi_fixture rhi_fixture;
        std::string error_message;
        TEST_ASSERT(rhi_fixture.initialize(error_message), error_message);

        plot::Plot_config config;
        const plot::frame_layout_result_t layout = make_layout();
        plot::frame_context_t ctx = make_context(layout, config);
        ctx.t0 = 40LL * k_second_ns;
        ctx.t1 = 42LL * k_second_ns;
        ctx.t_available_min = ctx.t0;
        ctx.t_available_max = ctx.t1;

        TEST_ASSERT(
            rhi_fixture.render_layer_frame(
                renderer, ctx, series_map, events, error_message),
            error_message);
        const layer_event_t* prepare =
            find_prepare_event(events, test_case.layer_id);
        TEST_ASSERT(prepare, "expected visible dots/area layer prepare event");
        TEST_ASSERT(prepare->snapshot_count == source->sample_count(),
            "external layer should still receive the full frame snapshot");
        TEST_ASSERT(prepare->source_count > 0,
            "visible source window should contain samples");
        TEST_ASSERT(prepare->source_count < prepare->snapshot_count,
            "visible source window should be smaller than the full source snapshot");
        TEST_ASSERT(prepare->synthetic_hold_count == 0,
            "ordinary visible dots/area upload should not add a synthetic hold sample");
        TEST_ASSERT(prepare->gpu_count == prepare->source_count,
            "visible dots/area upload should stage one GPU sample per source sample");

        if (!assert_compact_upload_state(
                renderer,
                test_case.series_id,
                *prepare,
                0,
                test_case.layer_id))
        {
            return false;
        }
    }

    return true;
}

bool test_builtin_upload_stages_single_synthetic_hold_sample()
{
    constexpr std::int64_t k_second_ns = 1'000'000'000LL;

    std::vector<layer_event_t> events;
    int create_count = 0;
    auto layer = std::make_shared<Recording_layer>(
        "hold-upload", 1, 20, events, create_count);
    auto source = std::make_shared<Test_source>();
    source->set_samples({
        {0LL, 1.0f},
        {1LL * k_second_ns, 2.0f},
        {2LL * k_second_ns, 3.0f}
    });

    auto series = std::make_shared<plot::series_data_t>();
    series->style = plot::Display_style::LINE;
    series->interpolation = plot::Series_interpolation::STEP_AFTER;
    series->empty_window_behavior = plot::Empty_window_behavior::HOLD_LAST_FORWARD;
    series->data_source = source;
    series->access = make_access_policy();
    series->qrhi_layers = {layer};

    std::map<int, std::shared_ptr<const plot::series_data_t>> series_map;
    const int series_id = 32;
    series_map[series_id] = series;

    plot::Asset_loader asset_loader;
    plot::Series_renderer renderer;
    renderer.initialize(asset_loader);

    Offscreen_rhi_fixture rhi_fixture;
    std::string error_message;
    TEST_ASSERT(rhi_fixture.initialize(error_message), error_message);

    plot::Plot_config config;
    const plot::frame_layout_result_t layout = make_layout();
    plot::frame_context_t ctx = make_context(layout, config);
    ctx.t0 = 10LL * k_second_ns;
    ctx.t1 = 12LL * k_second_ns;
    ctx.t_available_min = ctx.t0;
    ctx.t_available_max = ctx.t1;

    TEST_ASSERT(
        rhi_fixture.render_layer_frame(renderer, ctx, series_map, events, error_message),
        error_message);
    const layer_event_t* prepare = find_prepare_event(events, "hold-upload");
    TEST_ASSERT(prepare, "expected hold-upload layer prepare event");
    TEST_ASSERT(prepare->source_first == 2,
        "hold-forward source window should start at the last real source sample");
    TEST_ASSERT(prepare->source_count == 1,
        "hold-forward source window should keep exactly one real source sample");
    TEST_ASSERT(prepare->synthetic_hold_count == 1,
        "hold-forward upload should add exactly one synthetic GPU sample");
    TEST_ASSERT(prepare->gpu_count == 2,
        "hold-forward upload should stage one real and one synthetic GPU sample");

    if (!assert_compact_upload_state(
            renderer,
            series_id,
            *prepare,
            3,
            "hold-forward line"))
    {
        return false;
    }

    return true;
}

bool test_builtin_upload_stages_hold_windows_for_dots_and_area()
{
    constexpr std::int64_t k_second_ns = 1'000'000'000LL;

    struct upload_case_t
    {
        plot::Display_style style = plot::Display_style::DOTS;
        std::string_view layer_id;
        int series_id = 0;
    };

    const upload_case_t cases[] = {
        {plot::Display_style::DOTS, "hold-dots-upload", 35},
        {plot::Display_style::AREA, "hold-area-upload", 36}
    };

    for (const auto& test_case : cases) {
        std::vector<layer_event_t> events;
        int create_count = 0;
        auto layer = std::make_shared<Recording_layer>(
            std::string(test_case.layer_id), 1, 20, events, create_count);
        auto source = std::make_shared<Test_source>();
        source->set_samples({
            {0LL, 1.0f},
            {1LL * k_second_ns, 2.0f},
            {2LL * k_second_ns, 3.0f}
        });

        auto series = std::make_shared<plot::series_data_t>();
        series->style = test_case.style;
        series->interpolation = plot::Series_interpolation::STEP_AFTER;
        series->empty_window_behavior =
            plot::Empty_window_behavior::HOLD_LAST_FORWARD;
        series->data_source = source;
        series->access = make_access_policy();
        series->qrhi_layers = {layer};

        std::map<int, std::shared_ptr<const plot::series_data_t>> series_map;
        series_map[test_case.series_id] = series;

        plot::Asset_loader asset_loader;
        plot::Series_renderer renderer;
        renderer.initialize(asset_loader);

        Offscreen_rhi_fixture rhi_fixture;
        std::string error_message;
        TEST_ASSERT(rhi_fixture.initialize(error_message), error_message);

        plot::Plot_config config;
        const plot::frame_layout_result_t layout = make_layout();
        plot::frame_context_t ctx = make_context(layout, config);
        ctx.t0 = 10LL * k_second_ns;
        ctx.t1 = 12LL * k_second_ns;
        ctx.t_available_min = ctx.t0;
        ctx.t_available_max = ctx.t1;

        TEST_ASSERT(
            rhi_fixture.render_layer_frame(
                renderer, ctx, series_map, events, error_message),
            error_message);
        const layer_event_t* prepare =
            find_prepare_event(events, test_case.layer_id);
        TEST_ASSERT(prepare, "expected hold dots/area layer prepare event");
        TEST_ASSERT(prepare->source_first == 2,
            "hold-forward dots/area source window should start at last source sample");
        TEST_ASSERT(prepare->source_count == 1,
            "hold-forward dots/area source window should keep one real source sample");
        TEST_ASSERT(prepare->synthetic_hold_count == 1,
            "hold-forward dots/area upload should add one synthetic GPU sample");
        TEST_ASSERT(prepare->gpu_count == 2,
            "hold-forward dots/area upload should stage real and synthetic samples");

        if (!assert_compact_upload_state(
                renderer,
                test_case.series_id,
                *prepare,
                0,
                test_case.layer_id))
        {
            return false;
        }
    }

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

bool test_resources_changed_tracks_hold_timestamp_changes()
{
    constexpr std::int64_t k_second_ns = 1'000'000'000LL;

    std::vector<layer_event_t> events;
    int create_count = 0;
    auto layer = std::make_shared<Recording_layer>(
        "hold-key", 1, 20, events, create_count);
    auto source = std::make_shared<Test_source>();
    source->set_samples({
        {0LL, 1.0f},
        {1LL * k_second_ns, 2.0f},
        {2LL * k_second_ns, 3.0f}
    });

    auto series = std::make_shared<plot::series_data_t>();
    series->style = plot::Display_style::LINE;
    series->interpolation = plot::Series_interpolation::STEP_AFTER;
    series->empty_window_behavior =
        plot::Empty_window_behavior::HOLD_LAST_FORWARD;
    series->data_source = source;
    series->access = make_access_policy();
    series->qrhi_layers = {layer};

    std::map<int, std::shared_ptr<const plot::series_data_t>> series_map;
    series_map[37] = series;

    plot::Asset_loader asset_loader;
    plot::Series_renderer renderer;
    renderer.initialize(asset_loader);

    Offscreen_rhi_fixture rhi_fixture;
    std::string error_message;
    TEST_ASSERT(rhi_fixture.initialize(error_message), error_message);

    plot::Plot_config config;
    const plot::frame_layout_result_t layout = make_layout();
    plot::frame_context_t ctx = make_context(layout, config);
    ctx.t0 = 10LL * k_second_ns;
    ctx.t1 = 12LL * k_second_ns;
    ctx.t_available_min = ctx.t0;
    ctx.t_available_max = ctx.t1;

    TEST_ASSERT(
        rhi_fixture.render_layer_frame(renderer, ctx, series_map, events, error_message),
        error_message);
    const layer_event_t* first_prepare = find_prepare_event(events, "hold-key");
    TEST_ASSERT(first_prepare && first_prepare->resources_changed,
        "first hold-forward prepare must report resources_changed");
    TEST_ASSERT(first_prepare->source_first == 2 &&
            first_prepare->source_count == 1 &&
            first_prepare->synthetic_hold_count == 1 &&
            first_prepare->gpu_count == 2,
        "first hold-forward prepare should draw one real and one synthetic sample");

    const layer_event_t first = *first_prepare;
    const std::int64_t first_origin = first.t_origin_ns;

    events.clear();
    ctx.t1 = 13LL * k_second_ns;
    ctx.t_available_max = ctx.t1;
    TEST_ASSERT(
        rhi_fixture.render_layer_frame(renderer, ctx, series_map, events, error_message),
        error_message);
    const layer_event_t* second_prepare = find_prepare_event(events, "hold-key");
    TEST_ASSERT(second_prepare,
        "second hold-forward prepare must run");
    TEST_ASSERT(second_prepare->t_origin_ns == first_origin,
        "hold timestamp test requires both frames to stay in the same origin bucket");
    TEST_ASSERT(second_prepare->source_first == first.source_first &&
            second_prepare->source_count == first.source_count &&
            second_prepare->synthetic_hold_count ==
                first.synthetic_hold_count &&
            second_prepare->gpu_count == first.gpu_count,
        "hold timestamp test requires unchanged source/gpu window metadata");
    TEST_ASSERT(second_prepare->resources_changed,
        "changed hold-forward timestamp must invalidate external layer data key");

    return true;
}

bool test_busy_hold_forward_does_not_prepare_stale_tmax()
{
    constexpr std::int64_t k_second_ns = 1'000'000'000LL;

    std::vector<layer_event_t> events;
    auto source = std::make_shared<Test_source>();
    source->set_samples({
        {0LL, 1.0f},
        {1LL * k_second_ns, 2.0f},
        {2LL * k_second_ns, 3.0f}
    });

    auto series = std::make_shared<plot::series_data_t>();
    series->style = plot::Display_style::LINE;
    series->interpolation = plot::Series_interpolation::STEP_AFTER;
    series->empty_window_behavior =
        plot::Empty_window_behavior::HOLD_LAST_FORWARD;
    series->data_source = source;
    series->access = make_access_policy();

    std::map<int, std::shared_ptr<const plot::series_data_t>> series_map;
    const int series_id = 39;
    series_map[series_id] = series;

    plot::Asset_loader asset_loader;
    plot::Series_renderer renderer;
    renderer.initialize(asset_loader);

    Offscreen_rhi_fixture rhi_fixture;
    std::string error_message;
    TEST_ASSERT(rhi_fixture.initialize(error_message), error_message);

    plot::Plot_config config;
    const plot::frame_layout_result_t layout = make_layout();
    plot::frame_context_t ctx = make_context(layout, config);
    ctx.t0 = 10LL * k_second_ns;
    ctx.t1 = 12LL * k_second_ns;
    ctx.t_available_min = ctx.t0;
    ctx.t_available_max = ctx.t1;

    TEST_ASSERT(
        rhi_fixture.render_layer_frame(renderer, ctx, series_map, events, error_message),
        error_message);
    auto state_it = renderer.m_vbo_states.find(series_id);
    TEST_ASSERT(state_it != renderer.m_vbo_states.end(),
        "expected renderer VBO state for busy hold-forward test");
    const auto& initial_view_state = state_it->second.main_view;
    TEST_ASSERT(initial_view_state.last_staged_sample_count == 2,
        "initial hold-forward frame should stage real and synthetic samples");
    TEST_ASSERT(initial_view_state.last_prepared_t_max_ns == ctx.t1,
        "initial hold-forward frame should prepare at its visible t_max");

    const std::int64_t prepared_t_max = initial_view_state.last_prepared_t_max_ns;
    const std::size_t vbo_generation = initial_view_state.last_vbo_generation;
    source->return_busy_once();
    ctx.t1 = 13LL * k_second_ns;
    ctx.t_available_max = ctx.t1;
    TEST_ASSERT(
        rhi_fixture.render_layer_frame(renderer, ctx, series_map, events, error_message),
        error_message);

    const auto& busy_view_state = state_it->second.main_view;
    TEST_ASSERT(busy_view_state.last_prepared_t_max_ns == prepared_t_max,
        "busy snapshot with changed hold timestamp must not prepare stale VBO data");
    TEST_ASSERT(busy_view_state.last_vbo_generation == vbo_generation,
        "busy snapshot with changed hold timestamp must not reupload stale VBO data");

    return true;
}

bool test_busy_hold_forward_does_not_reuse_non_hold_window()
{
    constexpr std::int64_t k_second_ns = 1'000'000'000LL;

    std::vector<layer_event_t> events;
    auto source = std::make_shared<Test_source>();
    source->set_samples({
        {0LL, 1.0f},
        {1LL * k_second_ns, 2.0f},
        {2LL * k_second_ns, 3.0f}
    });

    auto series = std::make_shared<plot::series_data_t>();
    series->style = plot::Display_style::LINE;
    series->interpolation = plot::Series_interpolation::STEP_AFTER;
    series->empty_window_behavior =
        plot::Empty_window_behavior::HOLD_LAST_FORWARD;
    series->data_source = source;
    series->access = make_access_policy();

    std::map<int, std::shared_ptr<const plot::series_data_t>> series_map;
    const int series_id = 40;
    series_map[series_id] = series;

    plot::Asset_loader asset_loader;
    plot::Series_renderer renderer;
    renderer.initialize(asset_loader);

    Offscreen_rhi_fixture rhi_fixture;
    std::string error_message;
    TEST_ASSERT(rhi_fixture.initialize(error_message), error_message);

    plot::Plot_config config;
    const plot::frame_layout_result_t layout = make_layout();
    plot::frame_context_t ctx = make_context(layout, config);
    ctx.t0 = 0;
    ctx.t1 = 2LL * k_second_ns;
    ctx.t_available_min = ctx.t0;
    ctx.t_available_max = ctx.t1;

    TEST_ASSERT(
        rhi_fixture.render_layer_frame(renderer, ctx, series_map, events, error_message),
        error_message);
    auto state_it = renderer.m_vbo_states.find(series_id);
    TEST_ASSERT(state_it != renderer.m_vbo_states.end(),
        "expected renderer VBO state for busy non-hold-to-hold test");
    const auto& initial_view_state = state_it->second.main_view;
    TEST_ASSERT(initial_view_state.last_staged_sample_count >= 2,
        "initial non-hold frame should stage drawable source samples");
    TEST_ASSERT(initial_view_state.last_prepared_t_max_ns == ctx.t1,
        "initial non-hold frame should prepare at its visible t_max");

    const std::int64_t prepared_t_max =
        initial_view_state.last_prepared_t_max_ns;
    const std::size_t vbo_generation =
        initial_view_state.last_vbo_generation;
    source->return_busy_once();
    ctx.t1 = 3LL * k_second_ns;
    ctx.t_available_max = ctx.t1;
    TEST_ASSERT(
        rhi_fixture.render_layer_frame(renderer, ctx, series_map, events, error_message),
        error_message);

    const auto& busy_view_state = state_it->second.main_view;
    TEST_ASSERT(busy_view_state.last_prepared_t_max_ns == prepared_t_max,
        "busy non-hold-to-hold transition must not prepare stale VBO data");
    TEST_ASSERT(busy_view_state.last_vbo_generation == vbo_generation,
        "busy non-hold-to-hold transition must not reupload stale VBO data");

    return true;
}

bool test_external_layer_gets_snapshot_on_builtin_cache_hit()
{
    std::vector<layer_event_t> events;
    int create_count = 0;
    auto layer = std::make_shared<Recording_layer>(
        "cached", 1, 0, events, create_count);
    auto source = std::make_shared<Test_source>();
    auto series = make_line_plus_layer_series(source, {layer});
    std::map<int, std::shared_ptr<const plot::series_data_t>> series_map;
    const int series_id = 21;
    series_map[series_id] = series;

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
    const layer_event_t* initial_prepare = find_prepare_event(events, "cached");
    TEST_ASSERT(initial_prepare && initial_prepare->snapshot_valid,
        "initial prepare must receive a valid snapshot");
    TEST_ASSERT(initial_prepare->snapshot_count > 0,
        "initial prepare snapshot must contain samples");

    const int snapshots_after_initial_frame = source->snapshot_calls();

    events.clear();
    TEST_ASSERT(
        rhi_fixture.render_layer_frame(renderer, ctx, series_map, events, error_message),
        error_message);
    const layer_event_t* stable_prepare = find_prepare_event(events, "cached");
    TEST_ASSERT(stable_prepare, "expected external prepare on stable built-in cache-hit frame");
    TEST_ASSERT(!stable_prepare->resources_changed,
        "unchanged data and window must keep resources_changed false");
    TEST_ASSERT(stable_prepare->snapshot_valid,
        "stable built-in cache-hit frame must still provide the external layer snapshot");
    TEST_ASSERT(stable_prepare->snapshot_count > 0,
        "stable built-in cache-hit snapshot must contain samples");
    TEST_ASSERT(source->snapshot_calls() > snapshots_after_initial_frame,
        "external layer view must acquire a frame snapshot instead of taking the no-snapshot fast path");

    std::weak_ptr<void> hold = source->last_hold();
    TEST_ASSERT(hold.expired(),
        "external layer cache-hit snapshot hold must release after render");

    return true;
}

bool test_external_layer_skips_busy_stale_fallback_and_recovers()
{
    std::vector<layer_event_t> events;
    int create_count = 0;
    auto layer = std::make_shared<Recording_layer>(
        "busy", 1, 0, events, create_count);
    auto source = std::make_shared<Test_source>();
    auto series = make_line_plus_layer_series(source, {layer});
    std::map<int, std::shared_ptr<const plot::series_data_t>> series_map;
    const int series_id = 22;
    series_map[series_id] = series;

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
    TEST_ASSERT(find_prepare_event(events, "busy"),
        "expected initial external prepare before BUSY simulation");
    TEST_ASSERT(create_count == 1,
        "expected one external layer state after initial prepare");

    source->return_busy_once();
    events.clear();
    TEST_ASSERT(
        rhi_fixture.render_layer_frame(renderer, ctx, series_map, events, error_message),
        error_message);
    TEST_ASSERT(!find_prepare_event(events, "busy"),
        "external layer must not prepare from stale fallback without a valid snapshot");
    TEST_ASSERT(create_count == 1,
        "transient BUSY snapshot must not recreate external layer state");

    events.clear();
    TEST_ASSERT(
        rhi_fixture.render_layer_frame(renderer, ctx, series_map, events, error_message),
        error_message);
    const layer_event_t* recovered_prepare = find_prepare_event(events, "busy");
    TEST_ASSERT(recovered_prepare && recovered_prepare->snapshot_valid,
        "external layer must prepare with a valid snapshot after BUSY recovery");
    TEST_ASSERT(recovered_prepare->snapshot_count > 0,
        "recovered external prepare snapshot must contain samples");
    TEST_ASSERT(create_count == 1,
        "external layer state must survive transient BUSY snapshot recovery");

    return true;
}

bool test_external_layer_replans_when_snapshot_advances_after_sequence_probe()
{
    std::vector<layer_event_t> events;
    int create_count = 0;
    auto layer = std::make_shared<Recording_layer>(
        "advance", 1, 0, events, create_count);
    auto source = std::make_shared<Test_source>();
    auto series = make_line_plus_layer_series(source, {layer});
    std::map<int, std::shared_ptr<const plot::series_data_t>> series_map;
    const int series_id = 23;
    series_map[series_id] = series;

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
    const layer_event_t* initial_prepare = find_prepare_event(events, "advance");
    TEST_ASSERT(initial_prepare && initial_prepare->snapshot_valid,
        "expected initial external prepare with a snapshot");

    source->advance_during_next_snapshot();
    events.clear();
    TEST_ASSERT(
        rhi_fixture.render_layer_frame(renderer, ctx, series_map, events, error_message),
        error_message);
    const layer_event_t* advanced_prepare = find_prepare_event(events, "advance");
    TEST_ASSERT(advanced_prepare && advanced_prepare->snapshot_valid,
        "external prepare must still receive a valid snapshot after source advancement");
    TEST_ASSERT(advanced_prepare->sample_sequence == advanced_prepare->snapshot_sequence,
        "cache-hit metadata must be replanned when the acquired snapshot sequence advances");
    TEST_ASSERT(advanced_prepare->resources_changed,
        "advanced snapshot sequence must invalidate the external layer data key");

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
    RUN_TEST(test_builtin_upload_stages_visible_window_only);
    RUN_TEST(test_builtin_upload_reuses_vbo_capacity_headroom);
    RUN_TEST(test_combined_builtin_uploads_samples_once_per_view);
    RUN_TEST(test_builtin_upload_stages_visible_windows_for_dots_and_area);
    RUN_TEST(test_builtin_upload_stages_single_synthetic_hold_sample);
    RUN_TEST(test_builtin_upload_stages_hold_windows_for_dots_and_area);
    RUN_TEST(test_resources_changed_tracks_data_and_window_changes);
    RUN_TEST(test_resources_changed_tracks_hold_timestamp_changes);
    RUN_TEST(test_busy_hold_forward_does_not_prepare_stale_tmax);
    RUN_TEST(test_busy_hold_forward_does_not_reuse_non_hold_window);
    RUN_TEST(test_external_layer_gets_snapshot_on_builtin_cache_hit);
    RUN_TEST(test_external_layer_skips_busy_stale_fallback_and_recovers);
    RUN_TEST(test_external_layer_replans_when_snapshot_advances_after_sequence_probe);
    RUN_TEST(test_layer_state_recreated_for_program_identity_changes);

    std::cout << "Results: " << passed << " passed, " << failed << " failed" << std::endl;
    return failed > 0 ? 1 : 0;
}
