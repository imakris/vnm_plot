// vnm_plot Benchmark - Window Implementation

#include "benchmark_window.h"

#include <vnm_plot/core/plot_config.h>

#include <glm/gtc/matrix_transform.hpp>

#include <QByteArray>
#include <QMatrix4x4>
#include <QOffscreenSurface>
#include <QQuickItem>
#include <QSurfaceFormat>

#include <rhi/qrhi.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <sstream>

namespace vnm::benchmark {

namespace {

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
    resize(1200, 720);

    if (m_config.data_type == "Trades") {
        m_trade_buffer = std::make_unique<Ring_buffer<Trade_sample>>(m_config.ring_capacity);
        m_trade_buffer->set_profiler(&m_profiler);
        m_trade_source = std::make_unique<Benchmark_data_source<Trade_sample>>(*m_trade_buffer);
        m_trade_source->set_profiler(&m_profiler);
    }
    else {
        m_bar_buffer = std::make_unique<Ring_buffer<Bar_sample>>(m_config.ring_capacity);
        m_bar_buffer->set_profiler(&m_profiler);
        m_bar_source = std::make_unique<Benchmark_data_source<Bar_sample>>(*m_bar_buffer);
        m_bar_source->set_profiler(&m_profiler);
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

    setup_series();

    m_started_at = std::chrono::system_clock::now();

    if (m_config.static_data) {
        fill_static_data();
    }
    else {
        m_stop_generator.store(false);
        m_generator_thread = std::thread(&Benchmark_rhi_window::generator_thread_func, this);
    }

    m_render_timer.start(0);
    m_benchmark_timer.setSingleShot(true);
    m_benchmark_timer.start(static_cast<int>(m_config.duration_seconds * 1000));
}

Benchmark_rhi_window::~Benchmark_rhi_window()
{
    stop_generator_thread();
    delete m_plot;
    m_plot = nullptr;
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
                m_trade_buffer->push(m_generator.next_trade());
            }
            else {
                m_bar_buffer->push(m_generator.next_bar());
            }
            ++m_samples_generated;
            ++current_count;
        }

        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
}

void Benchmark_rhi_window::fill_static_data()
{
    constexpr std::array<int64_t, 5> ts_ns = {
        int64_t{   230'000'000},
        int64_t{ 1'230'000'000},
        int64_t{ 2'230'000'000},
        int64_t{ 3'230'000'000},
        int64_t{ 4'230'000'000}
    };
    const std::array<float, 5> vs = {-0.43f, 0.43f, -0.43f, 0.43f, -0.43f};

    for (std::size_t i = 0; i < ts_ns.size(); ++i) {
        if (m_config.data_type == "Trades") {
            Trade_sample s{};
            s.timestamp = ts_ns[i];
            s.price = vs[i];
            s.size = 1.0f;
            m_trade_buffer->push(s);
        }
        else {
            Bar_sample b{};
            b.timestamp = ts_ns[i];
            b.open = vs[i];
            b.high = vs[i];
            b.low  = vs[i];
            b.close = vs[i];
            b.volume = 1.0f;
            m_bar_buffer->push(b);
        }
    }
}

void Benchmark_rhi_window::setup_series()
{
    m_series = std::make_shared<vnm::plot::series_data_t>();
    m_series->enabled = true;
    m_series->color = glm::vec4(0.2f, 0.7f, 0.9f, 1.0f);

    if (m_config.data_type == "Trades") {
        m_series->set_data_source_ref(*m_trade_source);
        m_series->access = make_trade_access_policy();
    }
    else {
        m_series->set_data_source_ref(*m_bar_source);
        m_series->access = make_bar_access_policy();
    }

    const std::string& style = !m_config.style.empty()
        ? m_config.style
        : (m_config.data_type == "Trades" ? std::string("Dots") : std::string("Area"));

    if (style == "Dots") {
        m_series->style = vnm::plot::Display_style::DOTS;
    }
    else
    if (style == "Line") {
        m_series->style = vnm::plot::Display_style::LINE;
    }
    else {
        m_series->style = vnm::plot::Display_style::AREA;
    }

    m_plot->add_series(1, m_series);
}

void Benchmark_rhi_window::update_plot_view()
{
    vnm::plot::Data_source* source = m_config.data_type == "Trades"
        ? static_cast<vnm::plot::Data_source*>(m_trade_source.get())
        : static_cast<vnm::plot::Data_source*>(m_bar_source.get());

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
    const auto now = std::chrono::steady_clock::now();
    if (m_last_timer_tick.time_since_epoch().count() != 0) {
        const double elapsed_ms =
            std::chrono::duration<double, std::milli>(now - m_last_timer_tick).count();
        m_timer_interval_count++;
        m_timer_interval_total_ms += elapsed_ms;
        m_timer_interval_min_ms = std::min(m_timer_interval_min_ms, elapsed_ms);
        m_timer_interval_max_ms = std::max(m_timer_interval_max_ms, elapsed_ms);
    }
    m_last_timer_tick = now;

    update_plot_view();
    m_plot->update();
    update();
}

void Benchmark_rhi_window::on_benchmark_timeout()
{
    stop_generator_thread();
    m_render_timer.stop();
    m_profiler.record_observation_summary(
        "qrhi.scheduler.timer_interval",
        m_timer_interval_count,
        m_timer_interval_total_ms,
        m_timer_interval_min_ms,
        m_timer_interval_max_ms);
    emit benchmark_finished();
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
        m_trade_buffer = std::make_unique<Ring_buffer<Trade_sample>>(m_config.ring_capacity);
        m_trade_buffer->set_profiler(&m_profiler);
        m_trade_source = std::make_unique<Benchmark_data_source<Trade_sample>>(*m_trade_buffer);
        m_trade_source->set_profiler(&m_profiler);
    }
    else {
        m_bar_buffer = std::make_unique<Ring_buffer<Bar_sample>>(m_config.ring_capacity);
        m_bar_buffer->set_profiler(&m_profiler);
        m_bar_source = std::make_unique<Benchmark_data_source<Bar_sample>>(*m_bar_buffer);
        m_bar_source->set_profiler(&m_profiler);
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
    const int series_id = 1;
    auto series = std::make_shared<vnm::plot::series_data_t>();
    series->enabled = true;
    series->color = glm::vec4(0.2f, 0.7f, 0.9f, 1.0f);

    if (m_config.data_type == "Trades") {
        series->set_data_source_ref(*m_trade_source);
        series->access = make_trade_access_policy();
    }
    else {
        series->set_data_source_ref(*m_bar_source);
        series->access = make_bar_access_policy();
    }

    const std::string& style = !m_config.style.empty()
        ? m_config.style
        : (m_config.data_type == "Trades" ? std::string("Dots") : std::string("Area"));

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

    m_series_map[series_id] = series;
}

void Benchmark_rhi_offscreen_runner::generator_thread_func()
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
                m_trade_buffer->push(m_generator.next_trade());
            }
            else {
                m_bar_buffer->push(m_generator.next_bar());
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
        m_generator_thread.join();
    }
}

void Benchmark_rhi_offscreen_runner::fill_static_data()
{
    constexpr std::array<int64_t, 5> ts_ns = {
        int64_t{   230'000'000},
        int64_t{ 1'230'000'000},
        int64_t{ 2'230'000'000},
        int64_t{ 3'230'000'000},
        int64_t{ 4'230'000'000}
    };
    const std::array<float, 5> vs = {-0.43f, 0.43f, -0.43f, 0.43f, -0.43f};

    for (std::size_t i = 0; i < ts_ns.size(); ++i) {
        if (m_config.data_type == "Trades") {
            Trade_sample s{};
            s.timestamp = ts_ns[i];
            s.price = vs[i];
            s.size = 1.0f;
            m_trade_buffer->push(s);
        }
        else {
            Bar_sample b{};
            b.timestamp = ts_ns[i];
            b.open = vs[i];
            b.high = vs[i];
            b.low  = vs[i];
            b.close = vs[i];
            b.volume = 1.0f;
            m_bar_buffer->push(b);
        }
    }
}

bool Benchmark_rhi_offscreen_runner::initialize_rhi(std::string& error_message)
{
    auto create_null_rhi = [&]() {
        QRhiNullInitParams params;
        m_rhi.reset(QRhi::create(QRhi::Null, &params));
    };

    auto create_opengl_rhi = [&]() {
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

    const QByteArray forced_backend = qgetenv("QSG_RHI_BACKEND").trimmed().toLower();
    if (!forced_backend.isEmpty()) {
        if (forced_backend == "opengl" || forced_backend == "gles" ||
            forced_backend == "gles2")
        {
            if (!create_opengl_rhi()) {
                error_message = "QSG_RHI_BACKEND requests OpenGL, but offscreen OpenGL QRhi creation failed";
                return false;
            }
        }
        else
        if (forced_backend == "null") {
            create_null_rhi();
        }
        else
        if (forced_backend == "d3d11") {
#ifdef Q_OS_WIN
            QRhiD3D11InitParams params;
            m_rhi.reset(QRhi::create(QRhi::D3D11, &params));
            if (!m_rhi) {
                error_message = "QSG_RHI_BACKEND=d3d11 was requested, but D3D11 QRhi creation failed";
                return false;
            }
#else
            error_message = "QSG_RHI_BACKEND=d3d11 is only supported on Windows";
            return false;
#endif
        }
        else {
            error_message =
                "Unsupported QSG_RHI_BACKEND for offscreen benchmark: " +
                std::string(forced_backend.constData(), forced_backend.size());
            return false;
        }
    }
    else {
#ifdef Q_OS_WIN
        QRhiD3D11InitParams params;
        m_rhi.reset(QRhi::create(QRhi::D3D11, &params));
#else
        create_opengl_rhi();
#endif
    }

    if (!m_rhi) {
        create_null_rhi();
    }
    if (!m_rhi) {
        error_message = "Failed to create offscreen QRhi";
        return false;
    }

    const QSize size(1200, 720);
    const int sample_count = 4;
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

    QRhiColorAttachment color_attachment;
    color_attachment.setRenderBuffer(m_color_buffer.get());
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

bool Benchmark_rhi_offscreen_runner::render_frame(std::string& error_message)
{
    QRhiCommandBuffer* cb = nullptr;
    if (m_rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess || !cb) {
        error_message = "QRhi beginOffscreenFrame failed";
        return false;
    }

    VNM_PLOT_PROFILE_SCOPE(&m_profiler, "renderer");
    VNM_PLOT_PROFILE_SCOPE(&m_profiler, "renderer.frame");

    const int fb_w = 1200;
    const int fb_h = 720;

    vnm::plot::Data_source* source = m_config.data_type == "Trades"
        ? static_cast<vnm::plot::Data_source*>(m_trade_source.get())
        : static_cast<vnm::plot::Data_source*>(m_bar_source.get());
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
        m_series_renderer.prepare(frame_ctx, m_series_map);
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

    m_rhi->endOffscreenFrame();
    if (m_config.finish) {
        VNM_PLOT_PROFILE_SCOPE(&m_profiler, "renderer.frame.finish");
        m_rhi->finish();
    }
    return true;
}

bool Benchmark_rhi_offscreen_runner::run(std::string& error_message)
{
    setup_data_source();
    setup_rendering();
    setup_series();

    if (!initialize_rhi(error_message)) {
        return false;
    }

    m_started_at = std::chrono::system_clock::now();
    const auto steady_start = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration<double>(m_config.duration_seconds);

    if (m_config.static_data) {
        fill_static_data();
    }
    else {
        m_stop_generator.store(false);
        m_generator_thread = std::thread(&Benchmark_rhi_offscreen_runner::generator_thread_func, this);
    }

    while (std::chrono::steady_clock::now() - steady_start < duration) {
        if (!render_frame(error_message)) {
            stop_generator_thread();
            return false;
        }
    }

    stop_generator_thread();
    return true;
}

}  // namespace vnm::benchmark
