// vnm_plot Benchmark - Window
// Qt OpenGL window with vnm_plot rendering integration

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
#include <vnm_plot/vnm_plot.h>
#if defined(VNM_PLOT_ENABLE_TEXT)
#include <vnm_plot/core/font_renderer.h>
#include <vnm_plot/core/text_renderer.h>
#endif

#include <QOpenGLWidget>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <thread>

namespace vnm::benchmark {

/// Configuration for the benchmark window
struct Benchmark_config {
    double duration_seconds = 30.0;
    std::string session = "benchmark_run";
    std::string stream = "SIM";
    std::string data_type = "Bars";  // "Bars" or "Trades"
    std::string output_directory = ".";
    uint64_t seed = 0;
    double volatility = 0.02;
    double rate = 1000.0;  // samples per second
    std::size_t ring_capacity = 100000;
    bool extended_metadata = false;
    bool quiet = false;  // Suppress console output during benchmark
    bool show_text = true;  // Text/font rendering (default: on)
};

/// Main benchmark window with vnm_plot rendering
class Benchmark_window : public QOpenGLWidget {
    Q_OBJECT

public:
    explicit Benchmark_window(const Benchmark_config& config, QWidget* parent = nullptr);
    ~Benchmark_window() override;

    /// Get the profiler for report generation
    Benchmark_profiler& profiler() { return m_profiler; }
    const Benchmark_profiler& profiler() const { return m_profiler; }

    /// Get configuration
    const Benchmark_config& config() const { return m_config; }

    /// Get samples generated count
    std::size_t samples_generated() const { return m_samples_generated.load(); }

    /// Get start time
    std::chrono::system_clock::time_point started_at() const { return m_started_at; }

signals:
    void benchmark_finished();

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private slots:
    void on_render_timer();
    void on_benchmark_timeout();

private:
    void setup_series();
    void generator_thread_func();
    void stop_generator_thread();

    Benchmark_config m_config;
    Benchmark_profiler m_profiler;

    // Data pipeline
    std::unique_ptr<Ring_buffer<Bar_sample>> m_bar_buffer;
    std::unique_ptr<Ring_buffer<Trade_sample>> m_trade_buffer;
    std::unique_ptr<Benchmark_data_source<Bar_sample>> m_bar_source;
    std::unique_ptr<Benchmark_data_source<Trade_sample>> m_trade_source;
    Brownian_generator m_generator;

    // Generator thread
    std::thread m_generator_thread;
    std::atomic<bool> m_stop_generator{false};

    // Rendering
    std::unique_ptr<vnm::plot::Asset_loader> m_asset_loader;
    std::unique_ptr<vnm::plot::Primitive_renderer> m_primitives;
    std::unique_ptr<vnm::plot::Series_renderer> m_series_renderer;
    std::unique_ptr<vnm::plot::Chrome_renderer> m_chrome_renderer;
#if defined(VNM_PLOT_ENABLE_TEXT)
    vnm::plot::Font_renderer m_font_renderer;
    std::unique_ptr<vnm::plot::Text_renderer> m_text_renderer;
#endif
    vnm::plot::Layout_calculator m_layout_calc;
    std::map<int, std::shared_ptr<const vnm::plot::series_data_t>> m_series_map;
    vnm::plot::Plot_config m_render_config;
    vnm::plot::Layout_cache m_layout_cache;

    // View state
    double m_t_min = 0.0;
    double m_t_max = 10.0;
    float m_v_min = 90.0f;
    float m_v_max = 110.0f;
    bool m_gl_initialized = false;

    // Layout configuration (from shared benchmark_constants.h)
    double m_adjusted_font_px = k_adjusted_font_px;
    double m_base_label_height_px = k_base_label_height_px;
    double m_adjusted_preview_height = k_adjusted_preview_height;
    double m_vbar_width_pixels = k_vbar_width_pixels;
    double m_t_available_min = 0.0;  // First sample timestamp

    // Timing
    QTimer m_render_timer;
    QTimer m_benchmark_timer;
    std::chrono::system_clock::time_point m_started_at;
    std::atomic<std::size_t> m_samples_generated{0};
};

}  // namespace vnm::benchmark

#endif  // VNM_PLOT_BENCHMARK_WINDOW_H
