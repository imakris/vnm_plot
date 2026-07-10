// vnm_plot Benchmark - Window Implementation

#include "benchmark_window.h"
#include "allocation_tracker.h"

#include <vnm_plot/core/plot_config.h>

#include <glm/gtc/matrix_transform.hpp>

#include <QByteArray>
#include <QCoreApplication>
#include <QMatrix4x4>
#include <QOffscreenSurface>
#include <QQuickItem>
#include <QSGRendererInterface>
#include <QSurfaceFormat>
#if QT_CONFIG(vulkan) && __has_include(<vulkan/vulkan.h>)
#include <QVulkanInstance>
#endif

#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>

#if defined(Q_OS_WIN)
#include <windows.h>
#include <psapi.h>
#elif defined(Q_OS_UNIX)
#include <sys/resource.h>
#endif

namespace vnm::benchmark {

namespace {

std::string device_type_name(QRhiDriverInfo::DeviceType type)
{
    switch (type) {
    case QRhiDriverInfo::IntegratedDevice: return "integrated";
    case QRhiDriverInfo::DiscreteDevice: return "discrete";
    case QRhiDriverInfo::ExternalDevice: return "external";
    case QRhiDriverInfo::VirtualDevice: return "virtual";
    case QRhiDriverInfo::CpuDevice: return "cpu";
    case QRhiDriverInfo::UnknownDevice: return "unknown";
    }
    return "unknown";
}

std::string frame_op_result_name(QRhi::FrameOpResult result)
{
    switch (result) {
    case QRhi::FrameOpSuccess: return "success";
    case QRhi::FrameOpError: return "error";
    case QRhi::FrameOpSwapChainOutOfDate: return "swap-chain-out-of-date";
    case QRhi::FrameOpDeviceLost: return "device-lost";
    }
    return "unknown";
}

Graphics_device_info graphics_device_info_from_rhi(QRhi& rhi)
{
    const QRhiDriverInfo driver = rhi.driverInfo();
    Graphics_device_info info;
    info.backend = rhi.backendName();
    info.device_name = std::string(driver.deviceName.constData(), driver.deviceName.size());
    info.device_type = device_type_name(driver.deviceType);
    info.device_id = driver.deviceId;
    info.vendor_id = driver.vendorId;
    return info;
}

std::uint64_t process_memory_high_water_bytes()
{
#if defined(Q_OS_WIN)
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(
            GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
            sizeof(counters)))
    {
        return static_cast<std::uint64_t>(counters.PeakWorkingSetSize);
    }
#elif defined(Q_OS_UNIX)
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
#if defined(Q_OS_MACOS)
        return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
        return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024u;
#endif
    }
#endif
    return 0;
}

double steady_clock_resolution_ns()
{
    double resolution = std::numeric_limits<double>::max();
    auto previous = std::chrono::steady_clock::now();
    for (int sample = 0; sample < 10'000; ++sample) {
        const auto current = std::chrono::steady_clock::now();
        const double delta = std::chrono::duration<double, std::nano>(
            current - previous).count();
        if (delta > 0.0) {
            resolution = std::min(resolution, delta);
        }
        previous = current;
    }
    return std::isfinite(resolution) ? resolution : 0.0;
}

void ensure_interval_observations(Benchmark_profiler& profiler)
{
    profiler.ensure_observation("benchmark.producer.lock_wait_ns");
    profiler.ensure_observation("renderer.series_window.monotonicity_scan_count");
    profiler.ensure_observation("renderer.series_window.monotonicity_scan_samples");
    profiler.ensure_observation("renderer.frame.gpu_buffer_allocation_bytes");
    profiler.ensure_observation("renderer.frame.gpu_buffer_allocation_count");
    profiler.ensure_observation("benchmark.frame.cpu_allocation_bytes");
    profiler.ensure_observation("benchmark.frame.cpu_allocation_count");
}

template<typename T>
void record_ring_statistics(
    Benchmark_profiler& profiler,
    const std::vector<std::unique_ptr<Ring_buffer<T>>>& buffers,
    double measured_seconds)
{
    if (buffers.empty()) {
        return;
    }
    typename Ring_buffer<T>::Statistics total;
    std::uint64_t producer_wait_min_ns = std::numeric_limits<std::uint64_t>::max();
    for (const auto& buffer : buffers) {
        const auto stats = buffer->statistics();
        total.occupancy += stats.occupancy;
        total.high_water_occupancy += stats.high_water_occupancy;
        total.published_samples += stats.published_samples;
        total.overwritten_samples += stats.overwritten_samples;
        total.producer_wait_count += stats.producer_wait_count;
        total.producer_wait_total_ns += stats.producer_wait_total_ns;
        if (stats.producer_wait_count > 0) {
            producer_wait_min_ns = std::min(producer_wait_min_ns, stats.producer_wait_min_ns);
            total.producer_wait_max_ns = std::max(
                total.producer_wait_max_ns,
                stats.producer_wait_max_ns);
        }
    }
    profiler.record_observation("benchmark.ring.occupancy", static_cast<double>(total.occupancy));
    profiler.record_observation(
        "benchmark.ring.high_water_occupancy",
        static_cast<double>(total.high_water_occupancy));
    profiler.record_observation(
        "benchmark.ring.published_samples",
        static_cast<double>(total.published_samples));
    profiler.record_observation(
        "benchmark.ring.overwritten_samples",
        static_cast<double>(total.overwritten_samples));
    if (measured_seconds > 0.0) {
        profiler.record_observation(
            "benchmark.ring.published_samples_per_second",
            static_cast<double>(total.published_samples) / measured_seconds);
    }
    if (total.producer_wait_count > 0) {
        profiler.record_observation_summary(
            "benchmark.producer.lock_wait_ns",
            total.producer_wait_count,
            static_cast<double>(total.producer_wait_total_ns),
            static_cast<double>(producer_wait_min_ns),
            static_cast<double>(total.producer_wait_max_ns));
    }
}

template<typename T>
void reset_ring_measurement_statistics(
    std::vector<std::unique_ptr<Ring_buffer<T>>>& buffers)
{
    for (auto& buffer : buffers) {
        buffer->reset_measurement_statistics();
    }
}

glm::mat4 to_glm_mat4(const QMatrix4x4& matrix)
{
    const float* data = matrix.constData();
    return glm::mat4(
        data[0],  data[1],  data[2],  data[3],
        data[4],  data[5],  data[6],  data[7],
        data[8],  data[9],  data[10], data[11],
        data[12], data[13], data[14], data[15]);
}

std::string format_benchmark_timestamp(std::int64_t ts_ns, std::int64_t /*step_ns*/)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(ts_ns) / 1.0e9);
    return buf;
}

void update_view_range_from_source(
    vnm::plot::Data_source* source,
    const std::string& data_type,
    std::int64_t& t_min,
    std::int64_t& t_max,
    std::int64_t& t_available_min,
    float& v_min,
    float& v_max)
{
    if (!source) {
        return;
    }

    auto result = source->try_snapshot();
    if (result.status != vnm::plot::snapshot_result_t::Snapshot_status::READY) {
        return;
    }

    const auto& snapshot = result.snapshot;
    if (snapshot.count == 0) {
        return;
    }

    const void* first_sample = snapshot.at(0);
    const void* last_sample = snapshot.at(snapshot.count - 1);
    if (!first_sample || !last_sample) {
        return;
    }

    std::int64_t t_first = 0;
    std::int64_t t_last = 0;
    if (data_type == "Trades") {
        t_first = reinterpret_cast<const Trade_sample*>(first_sample)->timestamp;
        t_last = reinterpret_cast<const Trade_sample*>(last_sample)->timestamp;
    }
    else {
        t_first = reinterpret_cast<const Bar_sample*>(first_sample)->timestamp;
        t_last = reinterpret_cast<const Bar_sample*>(last_sample)->timestamp;
    }

    t_available_min = t_first;

    constexpr std::int64_t k_window_ns = std::int64_t{10} * 1'000'000'000;
    t_max = t_last;
    t_min = std::max(t_first, t_last - k_window_ns);

    // Release the direct-view shared lock before query_v_range() takes its own
    // snapshot. On writer-priority shared_mutex implementations, retaining the
    // first read lock while a producer queues can deadlock this thread's nested
    // read behind the waiting writer.
    result = {};

    vnm::plot::Data_access_policy access = data_type == "Trades"
        ? make_trade_access_policy()
        : make_bar_access_policy();
    vnm::plot::data_query_context_t query;
    query.access = &access;
    query.time_window = {
        std::numeric_limits<std::int64_t>::min(),
        std::numeric_limits<std::int64_t>::max()
    };
    const auto range_result = source->query_v_range(0, query);
    if (range_result.status == vnm::plot::Data_query_status::READY &&
        std::isfinite(range_result.value.min) &&
        std::isfinite(range_result.value.max) &&
        range_result.value.min <= range_result.value.max)
    {
        const float lo = range_result.value.min;
        const float hi = range_result.value.max;
        float padding = (hi - lo) * 0.1f;
        if (padding < 0.01f) {
            padding = 1.0f;
        }
        v_min = lo - padding;
        v_max = hi + padding;
    }
}

} // anonymous namespace


Benchmark_rhi_window::Benchmark_rhi_window(const Benchmark_config& config)
:
    QQuickWindow(),
    m_config(config),
    m_generator([&config]() {
        Brownian_generator::Config gen_config;
        gen_config.seed = config.seed;
        gen_config.volatility = config.volatility;
        gen_config.initial_price = 100.0;
        gen_config.time_step = 1.0 / config.rate;
        return gen_config;
    }())
{
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setSwapInterval(0);
    setFormat(format);

    setTitle("vnm_plot Benchmark QRhi");
    resize(m_config.framebuffer_width, m_config.framebuffer_height);

    if (m_config.data_type == "Trades") {
        m_trade_buffers.reserve(m_config.series_count);
        m_trade_sources.reserve(m_config.series_count);
        for (std::size_t index = 0; index < m_config.series_count; ++index) {
            auto buffer = std::make_unique<Ring_buffer<Trade_sample>>(m_config.ring_capacity);
            buffer->set_profiler(&m_profiler);
            auto source = std::make_unique<Benchmark_data_source<Trade_sample>>(*buffer);
            source->set_profiler(&m_profiler);
            m_trade_buffers.push_back(std::move(buffer));
            m_trade_sources.push_back(std::move(source));
        }
    }
    else {
        m_bar_buffers.reserve(m_config.series_count);
        m_bar_sources.reserve(m_config.series_count);
        for (std::size_t index = 0; index < m_config.series_count; ++index) {
            auto buffer = std::make_unique<Ring_buffer<Bar_sample>>(m_config.ring_capacity);
            buffer->set_profiler(&m_profiler);
            auto source = std::make_unique<Benchmark_data_source<Bar_sample>>(*buffer);
            source->set_profiler(&m_profiler);
            m_bar_buffers.push_back(std::move(buffer));
            m_bar_sources.push_back(std::move(source));
        }
    }

    m_render_config.dark_mode = true;
    m_render_config.show_text = m_config.show_text;
    m_render_config.snap_lines_to_pixels = false;
    m_render_config.line_width_px = m_config.line_width_px;
    m_render_config.point_diameter_px = m_config.point_diameter_px;
    m_render_config.font_size_px = k_adjusted_font_px;
    m_render_config.base_label_height_px = k_base_label_height_px;
    m_render_config.preview_height_px = k_adjusted_preview_height;
    m_render_config.format_timestamp = format_benchmark_timestamp;
    m_render_config.profiler =
        std::shared_ptr<vnm::plot::Profiler>(&m_profiler, [](vnm::plot::Profiler*) {});

    m_plot = new vnm::plot::Plot_widget();
    m_plot->setParentItem(contentItem());
    m_plot->setWidth(width());
    m_plot->setHeight(height());
    m_plot->set_config(m_render_config);
    m_plot->set_grid_visibility(1.0);
    m_plot->set_preview_visibility(1.0);
    m_plot->set_line_width_px(m_config.line_width_px);
    m_plot->set_preview_height(k_adjusted_preview_height);
    m_plot->set_vbar_width(k_vbar_width_pixels);
    m_plot->set_visible_info(vnm::plot::k_visible_info_none);
    m_plot->set_v_auto(false);

    connect(this, &QQuickWindow::widthChanged, this, [this](int w) {
        m_plot->setWidth(w);
    });
    connect(this, &QQuickWindow::heightChanged, this, [this](int h) {
        m_plot->setHeight(h);
    });
    connect(&m_render_timer, &QTimer::timeout, this, &Benchmark_rhi_window::on_render_timer);
    connect(&m_benchmark_timer, &QTimer::timeout, this, &Benchmark_rhi_window::on_benchmark_timeout);
    connect(
        this,
        &QQuickWindow::sceneGraphInitialized,
        this,
        [this]() {
            auto* rhi = static_cast<QRhi*>(rendererInterface()->getResource(
                this,
                QSGRendererInterface::RhiResource));
            if (!rhi) {
                return;
            }
            std::lock_guard<std::mutex> lock(m_graphics_info_mutex);
            m_graphics_info = graphics_device_info_from_rhi(*rhi);
        },
        Qt::DirectConnection);

    setup_series();

    if (m_config.static_data) {
        fill_static_data();
    }
    else {
        m_stop_generator.store(false);
        m_generator_thread = std::thread(&Benchmark_rhi_window::generator_thread_func, this);
    }

    m_warmup_frames_remaining = m_config.warmup_frames;
    if (m_warmup_frames_remaining == 0) {
        m_started_at = std::chrono::system_clock::now();
        if (m_config.measured_frames == 0) {
            m_benchmark_timer.setSingleShot(true);
            m_benchmark_timer.start(static_cast<int>(m_config.duration_seconds * 1000));
        }
    }
    m_render_timer.start(0);
}

Benchmark_rhi_window::~Benchmark_rhi_window()
{
    stop_generator_thread();
    delete m_plot;
    m_plot = nullptr;
}

Graphics_device_info Benchmark_rhi_window::graphics_device_info() const
{
    std::lock_guard<std::mutex> lock(m_graphics_info_mutex);
    return m_graphics_info;
}

void Benchmark_rhi_window::stop_generator_thread()
{
    if (m_generator_thread.joinable()) {
        m_stop_generator.store(true);
        m_generator_thread.join();
    }
}

void Benchmark_rhi_window::generator_thread_func()
{
    const double samples_per_second = m_config.rate;
    const double ns_per_sample = 1e9 / samples_per_second;

    auto start_time = std::chrono::steady_clock::now();

    while (!m_stop_generator.load()) {
        auto now = std::chrono::steady_clock::now();
        double elapsed_ns = std::chrono::duration<double, std::nano>(now - start_time).count();
        double target_samples = elapsed_ns / ns_per_sample;
        std::size_t current_count = m_samples_generated.load();

        while (current_count < static_cast<std::size_t>(target_samples) && !m_stop_generator.load()) {
            if (m_config.data_type == "Trades") {
                const Trade_sample sample = m_generator.next_trade();
                for (auto& buffer : m_trade_buffers) {
                    buffer->push(sample);
                }
            }
            else {
                const Bar_sample sample = m_generator.next_bar();
                for (auto& buffer : m_bar_buffers) {
                    buffer->push(sample);
                }
            }
            ++m_samples_generated;
            ++current_count;
        }

        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
}

void Benchmark_rhi_window::fill_static_data()
{
    if (m_config.data_type == "Trades") {
        std::vector<Trade_sample> samples(m_config.static_sample_count);
        m_generator.generate_trades(samples.data(), samples.size());
        for (auto& buffer : m_trade_buffers) {
            buffer->push_batch(samples.data(), samples.size());
        }
    }
    else {
        std::vector<Bar_sample> samples(m_config.static_sample_count);
        m_generator.generate_bars(samples.data(), samples.size());
        for (auto& buffer : m_bar_buffers) {
            buffer->push_batch(samples.data(), samples.size());
        }
    }
    m_samples_generated.store(m_config.static_sample_count);
}

void Benchmark_rhi_window::setup_series()
{
    const std::string& style = !m_config.style.empty()
        ? m_config.style
        : (m_config.data_type == "Trades" ? std::string("Dots") : std::string("Area"));
    m_series.reserve(m_config.series_count);
    for (std::size_t index = 0; index < m_config.series_count; ++index) {
        auto series = std::make_shared<vnm::plot::series_data_t>();
        series->enabled = true;
        const float blend = static_cast<float>(index % 8) / 8.0f;
        series->color = glm::vec4(0.2f + 0.5f * blend, 0.7f, 0.9f - 0.5f * blend, 1.0f);

        if (m_config.data_type == "Trades") {
            series->set_data_source_ref(*m_trade_sources[index]);
            series->access = make_trade_access_policy();
        }
        else {
            series->set_data_source_ref(*m_bar_sources[index]);
            series->access = make_bar_access_policy();
        }

        if (style == "Dots") {
            series->style = vnm::plot::Display_style::DOTS;
        }
        else
        if (style == "Line") {
            series->style = vnm::plot::Display_style::LINE;
        }
        else {
            series->style = vnm::plot::Display_style::AREA;
        }

        m_plot->add_series(static_cast<int>(index + 1), series);
        m_series.push_back(std::move(series));
    }
}

void Benchmark_rhi_window::update_plot_view()
{
    vnm::plot::Data_source* source = m_config.data_type == "Trades"
        ? static_cast<vnm::plot::Data_source*>(m_trade_sources.front().get())
        : static_cast<vnm::plot::Data_source*>(m_bar_sources.front().get());

    update_view_range_from_source(
        source,
        m_config.data_type,
        m_t_min,
        m_t_max,
        m_t_available_min,
        m_v_min,
        m_v_max);

    vnm::plot::Plot_view view;
    view.t_range = std::pair<qint64, qint64>{m_t_min, m_t_max};
    view.t_available_range = std::pair<qint64, qint64>{m_t_available_min, m_t_max};
    view.v_range = std::make_pair(m_v_min, m_v_max);
    view.v_auto = false;
    m_plot->set_view(view);
}

void Benchmark_rhi_window::on_render_timer()
{
    if (m_measurement_finished) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (m_last_timer_tick.time_since_epoch().count() != 0) {
        const double elapsed_ms =
            std::chrono::duration<double, std::milli>(now - m_last_timer_tick).count();
        m_timer_interval_count++;
        m_timer_interval_total_ms += elapsed_ms;
        m_timer_interval_min_ms = std::min(m_timer_interval_min_ms, elapsed_ms);
        m_timer_interval_max_ms = std::max(m_timer_interval_max_ms, elapsed_ms);
        if (m_warmup_frames_remaining == 0) {
            m_profiler.record_observation("benchmark.frame.total_ms", elapsed_ms);
        }
    }
    m_last_timer_tick = now;

    update_plot_view();
    m_plot->update();
    update();

    if (m_warmup_frames_remaining > 0) {
        --m_warmup_frames_remaining;
        if (m_warmup_frames_remaining == 0) {
            reset_ring_measurement_statistics(m_bar_buffers);
            reset_ring_measurement_statistics(m_trade_buffers);
            m_profiler.reset();
            m_started_at = std::chrono::system_clock::now();
            m_last_timer_tick = {};
            m_timer_interval_count = 0;
            m_timer_interval_total_ms = 0.0;
            m_timer_interval_min_ms = std::numeric_limits<double>::max();
            m_timer_interval_max_ms = 0.0;
            if (m_config.measured_frames == 0) {
                m_benchmark_timer.setSingleShot(true);
                m_benchmark_timer.start(static_cast<int>(m_config.duration_seconds * 1000));
            }
        }
        return;
    }

    ++m_measured_frame_count;
    m_profiler.record_counter("benchmark.frame.output_count");
    if (m_config.measured_frames > 0 &&
        m_measured_frame_count >= m_config.measured_frames)
    {
        on_benchmark_timeout();
    }
}

void Benchmark_rhi_window::on_benchmark_timeout()
{
    if (m_measurement_finished) {
        return;
    }
    m_measurement_finished = true;
    stop_generator_thread();
    m_render_timer.stop();
    m_benchmark_timer.stop();
    m_profiler.record_observation_summary(
        "qrhi.scheduler.timer_interval",
        m_timer_interval_count,
        m_timer_interval_total_ms,
        m_timer_interval_min_ms,
        m_timer_interval_max_ms);
    record_final_statistics();
    emit benchmark_finished();
}

void Benchmark_rhi_window::record_final_statistics()
{
    const double measured_seconds = m_timer_interval_total_ms > 0.0
        ? m_timer_interval_total_ms / 1000.0
        : m_config.duration_seconds;
    record_ring_statistics(m_profiler, m_bar_buffers, measured_seconds);
    record_ring_statistics(m_profiler, m_trade_buffers, measured_seconds);
    m_profiler.record_observation(
        "benchmark.memory.process_high_water_bytes",
        static_cast<double>(process_memory_high_water_bytes()));
    ensure_interval_observations(m_profiler);
}

Benchmark_rhi_offscreen_runner::Benchmark_rhi_offscreen_runner(const Benchmark_config& config)
:
    m_config(config),
    m_generator([&config]() {
        Brownian_generator::Config gen_config;
        gen_config.seed = config.seed;
        gen_config.volatility = config.volatility;
        gen_config.initial_price = 100.0;
        gen_config.time_step = 1.0 / config.rate;
        return gen_config;
    }())
{}

Benchmark_rhi_offscreen_runner::~Benchmark_rhi_offscreen_runner()
{
    stop_generator_thread();
#if defined(VNM_PLOT_ENABLE_TEXT)
    m_font_renderer.deinitialize();
#endif
}

void Benchmark_rhi_offscreen_runner::setup_data_source()
{
    if (m_config.data_type == "Trades") {
        m_trade_buffers.reserve(m_config.series_count);
        m_trade_sources.reserve(m_config.series_count);
        for (std::size_t index = 0; index < m_config.series_count; ++index) {
            auto buffer = std::make_unique<Ring_buffer<Trade_sample>>(m_config.ring_capacity);
            buffer->set_profiler(&m_profiler);
            auto source = std::make_unique<Benchmark_data_source<Trade_sample>>(*buffer);
            source->set_profiler(&m_profiler);
            m_trade_buffers.push_back(std::move(buffer));
            m_trade_sources.push_back(std::move(source));
        }
    }
    else {
        m_bar_buffers.reserve(m_config.series_count);
        m_bar_sources.reserve(m_config.series_count);
        for (std::size_t index = 0; index < m_config.series_count; ++index) {
            auto buffer = std::make_unique<Ring_buffer<Bar_sample>>(m_config.ring_capacity);
            buffer->set_profiler(&m_profiler);
            auto source = std::make_unique<Benchmark_data_source<Bar_sample>>(*buffer);
            source->set_profiler(&m_profiler);
            m_bar_buffers.push_back(std::move(buffer));
            m_bar_sources.push_back(std::move(source));
        }
    }
}

void Benchmark_rhi_offscreen_runner::setup_rendering()
{
    m_asset_loader.set_log_callback([](const std::string& msg) {
        std::cerr << "asset_loader: " << msg << "\n";
    });
    vnm::plot::init_embedded_assets(m_asset_loader);

    m_primitives.set_profiler(&m_profiler);
    m_series_renderer.initialize(m_asset_loader);

#if defined(VNM_PLOT_ENABLE_TEXT)
    const int font_px = static_cast<int>(std::round(k_adjusted_font_px));
    m_font_renderer.initialize_metrics(m_asset_loader, font_px, true);
    m_text_renderer = std::make_unique<vnm::plot::Text_renderer>(&m_font_renderer);
#endif

    m_render_config.dark_mode = true;
    m_render_config.show_text =
#if defined(VNM_PLOT_ENABLE_TEXT)
        m_config.show_text;
#else
        false;
#endif
    m_render_config.snap_lines_to_pixels = false;
    m_render_config.line_width_px = m_config.line_width_px;
    m_render_config.point_diameter_px = m_config.point_diameter_px;
    m_render_config.font_size_px = k_adjusted_font_px;
    m_render_config.base_label_height_px = k_base_label_height_px;
    m_render_config.preview_height_px = k_adjusted_preview_height;
    m_render_config.format_timestamp = format_benchmark_timestamp;
    m_render_config.profiler =
        std::shared_ptr<vnm::plot::Profiler>(&m_profiler, [](vnm::plot::Profiler*) {});
}

void Benchmark_rhi_offscreen_runner::setup_series()
{
    const std::string& style = !m_config.style.empty()
        ? m_config.style
        : (m_config.data_type == "Trades" ? std::string("Dots") : std::string("Area"));
    for (std::size_t index = 0; index < m_config.series_count; ++index) {
        auto series = std::make_shared<vnm::plot::series_data_t>();
        series->enabled = true;
        const float blend = static_cast<float>(index % 8) / 8.0f;
        series->color = glm::vec4(0.2f + 0.5f * blend, 0.7f, 0.9f - 0.5f * blend, 1.0f);

        if (m_config.data_type == "Trades") {
            series->set_data_source_ref(*m_trade_sources[index]);
            series->access = make_trade_access_policy();
        }
        else {
            series->set_data_source_ref(*m_bar_sources[index]);
            series->access = make_bar_access_policy();
        }

        if (style == "Dots") {
            series->style = vnm::plot::Display_style::DOTS;
        }
        else
        if (style == "Line") {
            series->style = vnm::plot::Display_style::LINE;
        }
        else {
            series->style = vnm::plot::Display_style::AREA;
        }

        m_series_map[static_cast<int>(index + 1)] = std::move(series);
    }
}

void Benchmark_rhi_offscreen_runner::generator_thread_func()
{
    Publication_rate_clock rate_clock;

    while (!m_stop_generator.load()) {
        Publication_rate_clock::duration paused_duration{};
        if (!wait_for_generator_permission(paused_duration)) {
            break;
        }
        rate_clock.exclude_pause(paused_duration);
        auto now = std::chrono::steady_clock::now();
        const std::size_t target_samples = rate_clock.target_samples(now, m_config.rate);
        std::size_t current_count = m_samples_generated.load();

        while (current_count < target_samples && !m_stop_generator.load()) {
            paused_duration = {};
            if (!wait_for_generator_permission(paused_duration)) {
                break;
            }
            rate_clock.exclude_pause(paused_duration);
            if (m_config.data_type == "Trades") {
                const Trade_sample sample = m_generator.next_trade();
                for (auto& buffer : m_trade_buffers) {
                    buffer->push(sample);
                }
            }
            else {
                const Bar_sample sample = m_generator.next_bar();
                for (auto& buffer : m_bar_buffers) {
                    buffer->push(sample);
                }
            }
            ++m_samples_generated;
            ++current_count;
        }

        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
}

void Benchmark_rhi_offscreen_runner::stop_generator_thread()
{
    if (m_generator_thread.joinable()) {
        m_stop_generator.store(true);
        m_generator_pause_requested.store(false);
        m_generator_control_cv.notify_all();
        m_generator_thread.join();
    }
}

void Benchmark_rhi_offscreen_runner::pause_generator_publication()
{
    if (!m_generator_thread.joinable()) {
        return;
    }
    m_generator_pause_requested.store(true, std::memory_order_release);
    m_generator_control_cv.notify_all();
    std::unique_lock lock(m_generator_control_mutex);
    m_generator_control_cv.wait(lock, [&]() {
        return m_generator_paused || m_stop_generator.load();
    });
}

void Benchmark_rhi_offscreen_runner::resume_generator_publication()
{
    if (!m_generator_thread.joinable()) {
        return;
    }
    m_generator_pause_requested.store(false, std::memory_order_release);
    m_generator_control_cv.notify_all();
    std::unique_lock lock(m_generator_control_mutex);
    m_generator_control_cv.wait(lock, [&]() {
        return !m_generator_paused || m_stop_generator.load();
    });
}

bool Benchmark_rhi_offscreen_runner::wait_for_generator_permission(
    Publication_rate_clock::duration& paused_duration)
{
    if (!m_generator_pause_requested.load(std::memory_order_acquire)) {
        return !m_stop_generator.load();
    }
    const auto pause_started = Publication_rate_clock::clock::now();
    std::unique_lock lock(m_generator_control_mutex);
    m_generator_paused = true;
    m_generator_control_cv.notify_all();
    m_generator_control_cv.wait(lock, [&]() {
        return !m_generator_pause_requested.load(std::memory_order_acquire) ||
            m_stop_generator.load();
    });
    m_generator_paused = false;
    m_generator_control_cv.notify_all();
    paused_duration += Publication_rate_clock::clock::now() - pause_started;
    return !m_stop_generator.load();
}

void Benchmark_rhi_offscreen_runner::fill_static_data()
{
    if (m_config.data_type == "Trades") {
        std::vector<Trade_sample> samples(m_config.static_sample_count);
        m_generator.generate_trades(samples.data(), samples.size());
        for (auto& buffer : m_trade_buffers) {
            buffer->push_batch(samples.data(), samples.size());
        }
    }
    else {
        std::vector<Bar_sample> samples(m_config.static_sample_count);
        m_generator.generate_bars(samples.data(), samples.size());
        for (auto& buffer : m_bar_buffers) {
            buffer->push_batch(samples.data(), samples.size());
        }
    }
    m_samples_generated.store(m_config.static_sample_count);
}

bool Benchmark_rhi_offscreen_runner::initialize_rhi(std::string& error_message)
{
    const auto create_null_rhi = [&]() {
        QRhiNullInitParams params;
        m_rhi.reset(QRhi::create(QRhi::Null, &params));
        return m_rhi != nullptr;
    };

    const auto create_opengl_rhi = [&]() {
#if QT_CONFIG(opengl)
        m_fallback_surface.reset(QRhiGles2InitParams::newFallbackSurface());
        QRhiGles2InitParams params;
        params.fallbackSurface = m_fallback_surface.get();
        m_rhi.reset(QRhi::create(QRhi::OpenGLES2, &params));
        if (!m_rhi) {
            m_fallback_surface.reset();
        }
        return m_rhi != nullptr;
#else
        return false;
#endif
    };

    const auto create_d3d11_rhi = [&]() {
#ifdef Q_OS_WIN
        QRhiD3D11InitParams params;
        m_rhi.reset(QRhi::create(QRhi::D3D11, &params));
        return m_rhi != nullptr;
#else
        return false;
#endif
    };

    const auto create_metal_rhi = [&]() {
#if defined(Q_OS_MACOS) && QT_CONFIG(metal)
        QRhiMetalInitParams params;
        m_rhi.reset(QRhi::create(QRhi::Metal, &params));
        return m_rhi != nullptr;
#else
        return false;
#endif
    };

    const auto create_vulkan_rhi = [&]() {
#if QT_CONFIG(vulkan) && __has_include(<vulkan/vulkan.h>)
        m_vulkan_instance = std::make_unique<QVulkanInstance>();
        if (!m_vulkan_instance->create()) {
            m_vulkan_instance.reset();
            return false;
        }
        QRhiVulkanInitParams params;
        params.inst = m_vulkan_instance.get();
        m_rhi.reset(QRhi::create(QRhi::Vulkan, &params));
        if (!m_rhi) {
            m_vulkan_instance.reset();
        }
        return m_rhi != nullptr;
#else
        return false;
#endif
    };

    const std::string& requested = m_config.graphics_backend;
    bool created = false;
    if (requested == "null") {
        created = create_null_rhi();
    }
    else
    if (requested == "opengl") {
        created = create_opengl_rhi();
    }
    else
    if (requested == "d3d11") {
        created = create_d3d11_rhi();
    }
    else
    if (requested == "metal") {
        created = create_metal_rhi();
    }
    else
    if (requested == "vulkan") {
        created = create_vulkan_rhi();
    }
    else
    if (requested == "native") {
#if defined(Q_OS_WIN)
        created = create_d3d11_rhi();
#elif defined(Q_OS_MACOS)
        created = create_metal_rhi();
#else
        created = create_opengl_rhi();
#endif
    }

    if (!created || !m_rhi) {
        error_message = "cold.backend_create: failed to create requested offscreen QRhi backend '" +
            requested + "'";
        return false;
    }
    if (m_rhi->backend() == QRhi::Null && requested != "null") {
        error_message = "cold.backend_validate: requested '" + requested +
            "' but QRhi created the Null backend";
        return false;
    }

    m_graphics_info = graphics_device_info_from_rhi(*m_rhi);

    const QSize size(m_config.framebuffer_width, m_config.framebuffer_height);
    const int sample_count = static_cast<int>(m_config.sample_count);
    m_color_buffer.reset(m_rhi->newRenderBuffer(
        QRhiRenderBuffer::Color,
        size,
        sample_count,
        {},
        QRhiTexture::RGBA8));
    if (!m_color_buffer->create()) {
        error_message = "Failed to create QRhi offscreen color buffer";
        return false;
    }

    if (m_config.capture_pixel_checksum) {
        m_resolve_texture.reset(m_rhi->newTexture(
            QRhiTexture::RGBA8,
            size,
            1,
            QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
        if (!m_resolve_texture || !m_resolve_texture->create()) {
            error_message = "cold.render_target: failed to create pixel readback resolve texture";
            return false;
        }
    }

    QRhiColorAttachment color_attachment;
    color_attachment.setRenderBuffer(m_color_buffer.get());
    if (m_resolve_texture) {
        color_attachment.setResolveTexture(m_resolve_texture.get());
    }
    QRhiTextureRenderTargetDescription rt_desc(color_attachment);
    m_render_target.reset(m_rhi->newTextureRenderTarget(rt_desc));
    m_render_pass_descriptor.reset(m_render_target->newCompatibleRenderPassDescriptor());
    m_render_target->setRenderPassDescriptor(m_render_pass_descriptor.get());
    if (!m_render_target->create()) {
        error_message = "Failed to create QRhi offscreen render target";
        return false;
    }

    return true;
}

bool Benchmark_rhi_offscreen_runner::render_frame(
    std::string& error_message,
    bool measured)
{
    Thread_allocation_scope allocation_scope(measured);
    const auto frame_started = std::chrono::steady_clock::now();
    const auto submission_started = frame_started;
    QRhiCommandBuffer* cb = nullptr;
    if (m_rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess || !cb) {
        error_message = "QRhi beginOffscreenFrame failed";
        return false;
    }

    VNM_PLOT_PROFILE_SCOPE(&m_profiler, "renderer");
    VNM_PLOT_PROFILE_SCOPE(&m_profiler, "renderer.frame");

    const int fb_w = m_config.framebuffer_width;
    const int fb_h = m_config.framebuffer_height;

    vnm::plot::Data_source* source = m_config.data_type == "Trades"
        ? static_cast<vnm::plot::Data_source*>(m_trade_sources.front().get())
        : static_cast<vnm::plot::Data_source*>(m_bar_sources.front().get());
    {
        VNM_PLOT_PROFILE_SCOPE(&m_profiler, "renderer.frame.update_view_range");
        update_view_range_from_source(
            source,
            m_config.data_type,
            m_t_min,
            m_t_max,
            m_t_available_min,
            m_v_min,
            m_v_max);
    }

    const double adjusted_reserved_height = k_base_label_height_px + k_adjusted_preview_height;
    const double usable_width = double(fb_w) - k_vbar_width_pixels;
    const double usable_height = double(fb_h) - adjusted_reserved_height;

    vnm::plot::Layout_calculator::parameters_t layout_params;
    layout_params.v_min = m_v_min;
    layout_params.v_max = m_v_max;
    layout_params.t_min = m_t_min;
    layout_params.t_max = m_t_max;
    layout_params.usable_width = usable_width;
    layout_params.usable_height = usable_height;
    layout_params.vbar_width = k_vbar_width_pixels;
    layout_params.label_visible_height = usable_height + k_adjusted_preview_height;
    layout_params.adjusted_font_size_in_pixels = k_adjusted_font_px;
#if defined(VNM_PLOT_ENABLE_TEXT)
    layout_params.monospace_char_advance_px = m_font_renderer.monospace_advance_px();
    layout_params.monospace_advance_is_reliable = m_font_renderer.monospace_advance_is_reliable();
    layout_params.measure_text_cache_key = m_font_renderer.text_measure_cache_key();
    layout_params.measure_text_func = [this](const char* text) {
        return m_font_renderer.measure_text_px(text);
    };
#else
    layout_params.measure_text_func = [](const char* text) {
        return static_cast<float>(std::strlen(text));
    };
#endif
    layout_params.h_label_vertical_nudge_factor = vnm::plot::detail::k_h_label_vertical_nudge_px;
    layout_params.format_timestamp_func = format_benchmark_timestamp;
    layout_params.get_required_fixed_digits_func = [](double) { return 2; };
    layout_params.profiler = &m_profiler;

    vnm::plot::layout_cache_key_t cache_key;
    cache_key.v0 = m_v_min;
    cache_key.v1 = m_v_max;
    cache_key.t0 = m_t_min;
    cache_key.t1 = m_t_max;
    cache_key.viewport_size = vnm::plot::Size_2i{fb_w, fb_h};
    cache_key.adjusted_reserved_height = adjusted_reserved_height;
    cache_key.adjusted_preview_height = k_adjusted_preview_height;
    cache_key.adjusted_font_size_in_pixels = k_adjusted_font_px;
    cache_key.vbar_width_pixels = k_vbar_width_pixels;
#if defined(VNM_PLOT_ENABLE_TEXT)
    cache_key.font_metrics_key = m_font_renderer.text_measure_cache_key();
#endif

    const vnm::plot::frame_layout_result_t* layout_ptr = m_layout_cache.try_get(cache_key);
    if (!layout_ptr) {
        VNM_PLOT_PROFILE_SCOPE(&m_profiler, "renderer.frame.layout_cache_miss");
        auto layout_result = m_layout_calc.calculate(layout_params);

        vnm::plot::frame_layout_result_t layout;
        layout.usable_width = usable_width;
        layout.usable_height = usable_height;
        layout.v_bar_width = k_vbar_width_pixels;
        layout.h_bar_height = k_base_label_height_px + 1.0;
        layout.max_v_label_text_width = layout_result.max_v_label_text_width;
        layout.v_labels = std::move(layout_result.v_labels);
        layout.h_labels = std::move(layout_result.h_labels);
        layout.v_label_fixed_digits = layout_result.v_label_fixed_digits;
        layout.h_labels_subsecond = layout_result.h_labels_subsecond;
        layout_ptr = &m_layout_cache.store(cache_key, std::move(layout));
    }

    vnm::plot::frame_context_t frame_ctx{*layout_ptr};
    frame_ctx.v0 = m_v_min;
    frame_ctx.v1 = m_v_max;
    frame_ctx.preview_v0 = m_v_min;
    frame_ctx.preview_v1 = m_v_max;
    frame_ctx.t0 = m_t_min;
    frame_ctx.t1 = m_t_max;
    frame_ctx.t_available_min = m_t_available_min;
    frame_ctx.t_available_max = m_t_max;
    frame_ctx.win_w = fb_w;
    frame_ctx.win_h = fb_h;

    const glm::mat4 pixel_ortho = glm::ortho(
        0.0f,
        float(fb_w),
        float(fb_h),
        0.0f,
        -1.0f,
        1.0f);
    frame_ctx.pmv = to_glm_mat4(m_rhi->clipSpaceCorrMatrix()) * pixel_ortho;
    frame_ctx.adjusted_font_px = k_adjusted_font_px;
    frame_ctx.base_label_height_px = k_base_label_height_px;
    frame_ctx.adjusted_reserved_height = adjusted_reserved_height;
    frame_ctx.adjusted_preview_height = k_adjusted_preview_height;
    frame_ctx.visible_info_flags = vnm::plot::k_visible_info_none;
    frame_ctx.dark_mode = m_render_config.dark_mode;
    frame_ctx.config = &m_render_config;
    const auto palette = vnm::plot::resolved_color_palette(
        &m_render_config,
        frame_ctx.dark_mode);
    frame_ctx.plot_body_background = palette.background;
    frame_ctx.rhi = m_rhi.get();
    frame_ctx.cb = cb;
    frame_ctx.render_target = m_render_target.get();

    QRhiResourceUpdateBatch* rhi_updates = m_rhi->nextResourceUpdateBatch();
    frame_ctx.rhi_updates = rhi_updates;

    {
        VNM_PLOT_PROFILE_SCOPE(&m_profiler, "renderer.frame.render_passes");
        const auto planning_started = std::chrono::steady_clock::now();
        m_series_renderer.prepare(frame_ctx, m_series_map);
        m_last_prepare_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - planning_started).count();
        if (measured) {
            m_profiler.record_observation("benchmark.planning.time_ms", m_last_prepare_ms);
        }
        m_chrome_renderer.render_grid_and_backgrounds(frame_ctx, m_primitives);
        const std::size_t back_layer_end = m_primitives.queued_op_count();
        m_chrome_renderer.render_zero_line(frame_ctx, m_primitives);
        m_chrome_renderer.render_preview_overlay(frame_ctx, m_primitives);
        const std::size_t front_layer_end = m_primitives.queued_op_count();

#if defined(VNM_PLOT_ENABLE_TEXT)
        if (m_text_renderer && m_render_config.show_text) {
            m_text_renderer->prepare(frame_ctx, false, false);
        }
#endif

        const QColor clear_color = QColor::fromRgbF(
            palette.background.r,
            palette.background.g,
            palette.background.b,
            palette.background.a);
        cb->beginPass(m_render_target.get(), clear_color, QRhiDepthStencilClearValue(1.0f, 0), rhi_updates);
        cb->setViewport(QRhiViewport(0, 0, fb_w, fb_h));
        m_primitives.record_draws(frame_ctx, back_layer_end);
        m_series_renderer.render(frame_ctx, m_series_map);
        m_primitives.record_draws(frame_ctx, front_layer_end);
#if defined(VNM_PLOT_ENABLE_TEXT)
        if (m_text_renderer && m_render_config.show_text) {
            m_text_renderer->record(frame_ctx);
        }
#endif
        cb->endPass();
        m_primitives.reset_frame();
    }

    QRhiReadbackResult readback;
    if (measured && m_config.capture_pixel_checksum && m_resolve_texture) {
        QRhiResourceUpdateBatch* readback_updates = m_rhi->nextResourceUpdateBatch();
        readback_updates->readBackTexture(
            QRhiReadbackDescription(m_resolve_texture.get()),
            &readback);
        cb->resourceUpdate(readback_updates);
    }

    const QRhi::FrameOpResult end_result = m_rhi->endOffscreenFrame();
    if (end_result != QRhi::FrameOpSuccess) {
        error_message = "measure.end_offscreen_frame: " + frame_op_result_name(end_result);
        return false;
    }
    if (measured) {
        const double submission_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - submission_started).count();
        m_profiler.record_observation("benchmark.frame.submission_ms", submission_ms);
    }
    if (m_config.finish || (measured && m_config.capture_pixel_checksum)) {
        VNM_PLOT_PROFILE_SCOPE(&m_profiler, "renderer.frame.finish");
        const auto finish_started = std::chrono::steady_clock::now();
        const QRhi::FrameOpResult finish_result = m_rhi->finish();
        if (finish_result != QRhi::FrameOpSuccess) {
            error_message = "measure.finish: " + frame_op_result_name(finish_result);
            return false;
        }
        if (measured) {
            const double finish_ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - finish_started).count();
            m_profiler.record_observation("benchmark.frame.gpu_finish_ms", finish_ms);
        }
    }
    if (measured && m_config.capture_pixel_checksum) {
        if (readback.data.isEmpty()) {
            error_message = "measure.pixel_readback: QRhi returned no pixel data";
            return false;
        }
        std::uint64_t checksum = 1'469'598'103'934'665'603ull;
        for (const unsigned char byte : readback.data) {
            checksum ^= byte;
            checksum *= 1'099'511'628'211ull;
        }
        m_pixel_checksum = checksum;
        m_pixel_nonuniform_count = 0;
        if (readback.data.size() >= 4) {
            const char* pixels = readback.data.constData();
            for (qsizetype offset = 4; offset + 4 <= readback.data.size(); offset += 4) {
                if (std::memcmp(pixels, pixels + offset, 4) != 0) {
                    ++m_pixel_nonuniform_count;
                }
            }
        }
        m_profiler.record_observation(
            "benchmark.frame.pixel_readback_bytes",
            static_cast<double>(readback.data.size()));
        m_profiler.record_observation(
            "benchmark.frame.pixel_checksum_low32",
            static_cast<double>(checksum & 0xffff'ffffull));
        m_profiler.record_observation(
            "benchmark.frame.pixel_nonuniform_count",
            static_cast<double>(m_pixel_nonuniform_count));
    }
    if (measured) {
        const Thread_allocation_measurement allocations = allocation_scope.finish();
        const double frame_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - frame_started).count();
        m_profiler.record_observation(
            "benchmark.frame.cpu_allocation_count",
            static_cast<double>(allocations.count));
        m_profiler.record_observation(
            "benchmark.frame.cpu_allocation_bytes",
            static_cast<double>(allocations.bytes));
        m_profiler.record_observation("benchmark.frame.total_ms", frame_ms);
        m_profiler.record_counter("benchmark.frame.output_count");
        ++m_measured_frame_count;
    }
    return true;
}

bool Benchmark_rhi_offscreen_runner::run(std::string& error_message)
{
    m_phase_trace_started = std::chrono::steady_clock::now();
    const auto trace_identity = std::chrono::system_clock::now().time_since_epoch().count();
    m_phase_trace_path = (
        std::filesystem::path(m_config.output_directory) /
        ("benchmark_phase_trace_" + std::to_string(trace_identity) + "_" +
            std::to_string(QCoreApplication::applicationPid()) + ".jsonl")).string();
    m_phase_trace.open(m_phase_trace_path, std::ios::out | std::ios::trunc);
    if (!m_phase_trace) {
        error_message = "phase_trace.open: failed to open " + m_phase_trace_path;
        return false;
    }
    record_phase("cold.setup.begin");
    const auto cold_started = std::chrono::steady_clock::now();
    const auto setup_started = cold_started;
    setup_data_source();
    setup_rendering();
    setup_series();
    record_phase("cold.setup.end");
    m_cold_setup_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - setup_started).count();

    const auto backend_started = std::chrono::steady_clock::now();
    record_phase("cold.backend_create.begin");
    if (!initialize_rhi(error_message)) {
        record_phase("cold.backend_create.failed");
        return false;
    }
    record_phase("cold.backend_create.end");
    m_cold_backend_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - backend_started).count();

    if (m_config.static_data) {
        fill_static_data();
    }
    else {
        m_stop_generator.store(false);
        m_generator_thread = std::thread(&Benchmark_rhi_offscreen_runner::generator_thread_func, this);
    }

    for (std::size_t frame = 0; frame < m_config.warmup_frames; ++frame) {
        record_phase("warmup.frame.begin", frame);
        const auto warmup_started = std::chrono::steady_clock::now();
        if (!render_frame(error_message, false)) {
            error_message = "warmup.frame: " + error_message;
            stop_generator_thread();
            return false;
        }
        record_phase("warmup.frame.end", frame);
        if (frame == 0) {
            m_cold_first_submission_ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - warmup_started).count();
            m_cold_shader_pipeline_prepare_ms = m_last_prepare_ms;
        }
    }

    m_cold_total_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - cold_started).count();
    const double clock_resolution_ns = steady_clock_resolution_ns();
    pause_generator_publication();
    reset_ring_measurement_statistics(m_bar_buffers);
    reset_ring_measurement_statistics(m_trade_buffers);
    m_profiler.reset();
    m_profiler.record_observation("benchmark.cold.setup_ms", m_cold_setup_ms);
    m_profiler.record_observation("benchmark.cold.backend_create_ms", m_cold_backend_ms);
    if (m_config.warmup_frames > 0) {
        m_profiler.record_observation(
            "benchmark.cold.first_submission_ms",
            m_cold_first_submission_ms);
        m_profiler.record_observation(
            "benchmark.cold.series_shader_pipeline_prepare_ms",
            m_cold_shader_pipeline_prepare_ms);
    }
    m_profiler.record_observation("benchmark.cold.total_ms", m_cold_total_ms);
    m_profiler.record_observation(
        "benchmark.clock.steady_resolution_ns",
        clock_resolution_ns);
    m_profiler.record_observation(
        "benchmark.warmup.frame_count",
        static_cast<double>(m_config.warmup_frames));

    m_started_at = std::chrono::system_clock::now();
    const auto measured_started = std::chrono::steady_clock::now();
    resume_generator_publication();
    const auto duration = std::chrono::duration<double>(m_config.duration_seconds);

    const auto render_measured_frame = [&](std::size_t frame) {
        record_phase("measure.frame.begin", frame);
        if (render_frame(error_message, true)) {
            record_phase("measure.frame.end", frame);
            return true;
        }
        record_phase("measure.frame.failed", frame);
        error_message = "measure.frame: " + error_message;
        return false;
    };

    if (m_config.measured_frames > 0) {
        for (std::size_t frame = 0; frame < m_config.measured_frames; ++frame) {
            if (!render_measured_frame(frame)) {
                stop_generator_thread();
                return false;
            }
        }
    }
    else {
        std::size_t frame = 0;
        while (std::chrono::steady_clock::now() - measured_started < duration) {
            if (!render_measured_frame(frame++)) {
                stop_generator_thread();
                return false;
            }
        }
    }

    pause_generator_publication();
    const auto measured_ended = std::chrono::steady_clock::now();
    const double measured_seconds = std::chrono::duration<double>(
        measured_ended - measured_started).count();
    record_final_statistics(measured_seconds);

    const auto shutdown_started = std::chrono::steady_clock::now();
    record_phase("shutdown.generator.begin");
    stop_generator_thread();
    record_phase("shutdown.generator.end");
    const double shutdown_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - shutdown_started).count();
    m_profiler.record_observation("benchmark.shutdown.generator_ms", shutdown_ms);
    record_phase("complete");
    return true;
}

void Benchmark_rhi_offscreen_runner::record_phase(
    const char* phase,
    std::size_t frame) const
{
    const double elapsed_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - m_phase_trace_started).count();
    if (m_phase_trace) {
        m_phase_trace << "{\"elapsed_ms\":" << std::setprecision(17) << elapsed_ms
                      << ",\"phase\":\"" << phase << "\",\"frame\":" << frame << "}\n";
        m_phase_trace.flush();
    }
}

void Benchmark_rhi_offscreen_runner::record_final_statistics(double measured_seconds)
{
    record_ring_statistics(m_profiler, m_bar_buffers, measured_seconds);
    record_ring_statistics(m_profiler, m_trade_buffers, measured_seconds);
    m_profiler.record_observation(
        "benchmark.memory.process_high_water_bytes",
        static_cast<double>(process_memory_high_water_bytes()));
    ensure_interval_observations(m_profiler);
}

}  // namespace vnm::benchmark
