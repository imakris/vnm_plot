// vnm_plot Benchmark - Window
// QRhi benchmark windows and offscreen runner.

#ifndef VNM_PLOT_BENCHMARK_WINDOW_H
#define VNM_PLOT_BENCHMARK_WINDOW_H

#include "benchmark_constants.h"
#include "benchmark_data_source.h"
#include "benchmark_profiler.h"
#include "brownian_generator.h"
#include "ring_buffer.h"
#include "sample_types.h"

#include <vnm_plot/core/asset_loader.h>
#include <vnm_plot/core/chrome_renderer.h>
#include <vnm_plot/core/layout_calculator.h>
#include <vnm_plot/core/primitive_renderer.h>
#include <vnm_plot/core/series_renderer.h>
#include <vnm_plot/qt/plot_widget.h>
#include <vnm_plot/vnm_plot.h>
#if defined(VNM_PLOT_ENABLE_TEXT)
#include <vnm_plot/core/font_renderer.h>
#include <vnm_plot/core/text_renderer.h>
#endif

#include <QQuickWindow>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <thread>

class QRhi;
class QRhiRenderBuffer;
class QRhiRenderPassDescriptor;
class QRhiTextureRenderTarget;

namespace vnm::benchmark {

/// Configuration for the benchmark window
struct Benchmark_config {
    double duration_seconds = 30.0;
    std::string session = "benchmark_run";
    std::string stream = "SIM";
    std::string data_type = "Bars";  // "Bars" or "Trades"
    // "" => default per data type (Trades=Dots, Bars=Area).
    // Otherwise one of: "Dots", "Line", "Area".
    std::string style = "";
    std::string output_directory = ".";
    uint64_t seed = 0;
    double volatility = 0.02;
    double rate = 1000.0;  // samples per second
    std::size_t ring_capacity = 100000;
    bool extended_metadata = false;
    bool quiet = false;  // Suppress console output during benchmark
    bool show_text = true;  // Text/font rendering (default: on)
    std::string backend = "qrhi-offscreen";  // "qrhi" or "qrhi-offscreen"
    bool finish = false;  // Wait for GPU completion after offscreen frames
    // Visual-diff mode: pre-fill 5 fixed samples (4-segment zig-zag) and
    // skip the generator. Useful for side-by-side rendering comparisons.
    bool static_data = false;
    // Line thickness in pixels (default 1.5 for animated runs; bumped via
    // --line-px when comparing visuals so segments are easy to inspect).
    double line_width_px = 1.5;
    // Dot diameter in pixels for DOTS rendering (default 1.0 matches the
    // animated benchmark; bumped via --point-px so static-mode dots are
    // visible in side-by-side visual comparisons).
    double point_diameter_px = 1.0;
};

/// QRhi benchmark window using the public QQuickRhiItem Plot_widget path.
class Benchmark_rhi_window : public QQuickWindow {
    Q_OBJECT

public:
    explicit Benchmark_rhi_window(const Benchmark_config& config);
    ~Benchmark_rhi_window() override;

    Benchmark_profiler& profiler() { return m_profiler; }
    const Benchmark_profiler& profiler() const { return m_profiler; }
    const Benchmark_config& config() const { return m_config; }
    std::size_t samples_generated() const { return m_samples_generated.load(); }
    std::chrono::system_clock::time_point started_at() const { return m_started_at; }

signals:
    void benchmark_finished();

private slots:
    void on_render_timer();
    void on_benchmark_timeout();

private:
    void setup_series();
    void generator_thread_func();
    void stop_generator_thread();
    void fill_static_data();
    void update_plot_view();

    Benchmark_config m_config;
    Benchmark_profiler m_profiler;

    std::unique_ptr<Ring_buffer<Bar_sample>> m_bar_buffer;
    std::unique_ptr<Ring_buffer<Trade_sample>> m_trade_buffer;
    std::unique_ptr<Benchmark_data_source<Bar_sample>> m_bar_source;
    std::unique_ptr<Benchmark_data_source<Trade_sample>> m_trade_source;
    Brownian_generator m_generator;

    std::thread m_generator_thread;
    std::atomic<bool> m_stop_generator{false};

    vnm::plot::Plot_widget* m_plot = nullptr;
    std::shared_ptr<vnm::plot::series_data_t> m_series;

    vnm::plot::Plot_config m_render_config;

    std::int64_t m_t_min = 0;
    std::int64_t m_t_max = std::int64_t{10} * 1'000'000'000;
    std::int64_t m_t_available_min = 0;
    float m_v_min = 90.0f;
    float m_v_max = 110.0f;

    QTimer m_render_timer;
    QTimer m_benchmark_timer;
    std::chrono::steady_clock::time_point m_last_timer_tick;
    std::uint64_t m_timer_interval_count = 0;
    double m_timer_interval_total_ms = 0.0;
    double m_timer_interval_min_ms = std::numeric_limits<double>::max();
    double m_timer_interval_max_ms = 0.0;
    std::chrono::system_clock::time_point m_started_at;
    std::atomic<std::size_t> m_samples_generated{0};
};

/// Direct QRhi benchmark that renders into an offscreen target without Qt Quick.
class Benchmark_rhi_offscreen_runner
{
public:
    explicit Benchmark_rhi_offscreen_runner(const Benchmark_config& config);
    ~Benchmark_rhi_offscreen_runner();

    Benchmark_profiler& profiler() { return m_profiler; }
    const Benchmark_profiler& profiler() const { return m_profiler; }
    const Benchmark_config& config() const { return m_config; }
    std::size_t samples_generated() const { return m_samples_generated.load(); }
    std::chrono::system_clock::time_point started_at() const { return m_started_at; }

    bool run(std::string& error_message);

private:
    void setup_data_source();
    void setup_rendering();
    void setup_series();
    void generator_thread_func();
    void stop_generator_thread();
    void fill_static_data();
    bool initialize_rhi(std::string& error_message);
    bool render_frame(std::string& error_message);

    Benchmark_config m_config;
    Benchmark_profiler m_profiler;

    std::unique_ptr<Ring_buffer<Bar_sample>> m_bar_buffer;
    std::unique_ptr<Ring_buffer<Trade_sample>> m_trade_buffer;
    std::unique_ptr<Benchmark_data_source<Bar_sample>> m_bar_source;
    std::unique_ptr<Benchmark_data_source<Trade_sample>> m_trade_source;
    Brownian_generator m_generator;

    std::thread m_generator_thread;
    std::atomic<bool> m_stop_generator{false};

    std::unique_ptr<QRhi> m_rhi;
    std::unique_ptr<QRhiRenderBuffer> m_color_buffer;
    std::unique_ptr<QRhiTextureRenderTarget> m_render_target;
    std::unique_ptr<QRhiRenderPassDescriptor> m_render_pass_descriptor;

    vnm::plot::Asset_loader m_asset_loader;
    vnm::plot::Primitive_renderer m_primitives;
    vnm::plot::Series_renderer m_series_renderer;
    vnm::plot::Chrome_renderer m_chrome_renderer;
#if defined(VNM_PLOT_ENABLE_TEXT)
    vnm::plot::Font_renderer m_font_renderer;
    std::unique_ptr<vnm::plot::Text_renderer> m_text_renderer;
#endif
    vnm::plot::Layout_calculator m_layout_calc;
    vnm::plot::Layout_cache m_layout_cache;
    std::map<int, std::shared_ptr<const vnm::plot::series_data_t>> m_series_map;
    vnm::plot::Plot_config m_render_config;

    std::int64_t m_t_min = 0;
    std::int64_t m_t_max = std::int64_t{10} * 1'000'000'000;
    std::int64_t m_t_available_min = 0;
    float m_v_min = 90.0f;
    float m_v_max = 110.0f;
    std::chrono::system_clock::time_point m_started_at;
    std::atomic<std::size_t> m_samples_generated{0};
};

}  // namespace vnm::benchmark

#endif  // VNM_PLOT_BENCHMARK_WINDOW_H
