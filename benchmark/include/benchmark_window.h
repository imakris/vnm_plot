// vnm_plot Benchmark - Window
// Qt OpenGL window with vnm_plot rendering integration

#ifndef VNM_PLOT_BENCHMARK_WINDOW_H
#define VNM_PLOT_BENCHMARK_WINDOW_H

#include "benchmark_data_source.h"
#include "benchmark_profiler.h"
#include "brownian_generator.h"
#include "ring_buffer.h"
#include "sample_types.h"

#include <vnm_plot/vnm_plot.h>

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
    std::string symbol = "SIM";
    std::string data_type = "Bars";  // "Bars" or "Trades"
    std::string output_directory = ".";
    uint64_t seed = 0;
    double volatility = 0.02;
    double rate = 1000.0;  // samples per second
    std::size_t ring_capacity = 100000;
    bool extended_metadata = false;
    bool quiet = false;  // Suppress console output during benchmark
};

/// Main benchmark window with vnm_plot rendering
class Benchmark_window : public QOpenGLWidget {
    Q_OBJECT

public:
    explicit Benchmark_window(const Benchmark_config& config, QWidget* parent = nullptr);
    ~Benchmark_window() override;

    /// Get the profiler for report generation
    Benchmark_profiler& profiler() { return profiler_; }
    const Benchmark_profiler& profiler() const { return profiler_; }

    /// Get configuration
    const Benchmark_config& config() const { return config_; }

    /// Get samples generated count
    std::size_t samples_generated() const { return samples_generated_.load(); }

    /// Get start time
    std::chrono::system_clock::time_point started_at() const { return started_at_; }

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
    void update_view_range();
    void generator_thread_func();
    void stop_generator_thread();

    Benchmark_config config_;
    Benchmark_profiler profiler_;

    // Data pipeline
    std::unique_ptr<Ring_buffer<Bar_sample>> bar_buffer_;
    std::unique_ptr<Ring_buffer<Trade_sample>> trade_buffer_;
    std::unique_ptr<Benchmark_data_source<Bar_sample>> bar_source_;
    std::unique_ptr<Benchmark_data_source<Trade_sample>> trade_source_;
    Brownian_generator generator_;

    // Generator thread
    std::thread generator_thread_;
    std::atomic<bool> stop_generator_{false};

    // Rendering
    std::unique_ptr<vnm::plot::Asset_loader> asset_loader_;
    std::unique_ptr<vnm::plot::Primitive_renderer> primitives_;
    std::unique_ptr<vnm::plot::Series_renderer> series_renderer_;
    std::unique_ptr<vnm::plot::Chrome_renderer> chrome_renderer_;
    std::map<int, std::shared_ptr<vnm::plot::series_data_t>> series_map_;
    vnm::plot::Render_config render_config_;

    // View state
    double t_min_ = 0.0;
    double t_max_ = 10.0;
    float v_min_ = 90.0f;
    float v_max_ = 110.0f;
    bool gl_initialized_ = false;

    // Timing
    QTimer render_timer_;
    QTimer benchmark_timer_;
    std::chrono::system_clock::time_point started_at_;
    std::atomic<std::size_t> samples_generated_{0};
};

}  // namespace vnm::benchmark

#endif  // VNM_PLOT_BENCHMARK_WINDOW_H
