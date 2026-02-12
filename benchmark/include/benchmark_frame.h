// vnm_plot Benchmark - Shared Frame Rendering Helper
// Extracts common frame rendering logic between Qt and headless benchmarks

#ifndef VNM_PLOT_BENCHMARK_FRAME_H
#define VNM_PLOT_BENCHMARK_FRAME_H

#include "benchmark_constants.h"
#include "benchmark_profiler.h"
#include "sample_types.h"

#include <vnm_plot/core/chrome_renderer.h>
#include <vnm_plot/core/layout_calculator.h>
#include <vnm_plot/core/primitive_renderer.h>
#include <vnm_plot/core/series_renderer.h>
#include <vnm_plot/vnm_plot.h>
#if defined(VNM_PLOT_ENABLE_TEXT)
#include <vnm_plot/core/font_renderer.h>
#include <vnm_plot/core/text_renderer.h>
#endif

#include <functional>
#include <map>
#include <memory>
#include <string>

namespace vnm::benchmark {

/// Input parameters for a benchmark frame render
struct Benchmark_frame_params {
    // View range state (in/out - will be updated by update_view_range)
    double& t_min;
    double& t_max;
    double& t_available_min;
    float& v_min;
    float& v_max;

    // Data type for accessing samples
    const std::string& data_type;

    // Layout constants
    double adjusted_font_px = k_adjusted_font_px;
    double base_label_height_px = k_base_label_height_px;
    double adjusted_preview_height = k_adjusted_preview_height;
    double vbar_width_pixels = k_vbar_width_pixels;

    // GL control
    bool skip_gl_calls = false;
    bool clear_depth = false;  // If true, also clear depth buffer (headless uses this)

    // Callback to get framebuffer size (called inside renderer.frame.fb_size scope)
    // Returns {width, height}
    std::function<std::pair<int, int>()> get_fb_size;

    // Optional callback to bind framebuffer (called inside renderer.frame.gl_setup scope)
    // Used by headless benchmark to bind FBO before GL setup
    std::function<void()> bind_framebuffer;
};

/// Renderers and caches used by the benchmark frame
struct Benchmark_frame_context {
    vnm::benchmark::Benchmark_profiler& profiler;
    vnm::plot::Plot_config& render_config;
    vnm::plot::Layout_calculator& layout_calc;
    vnm::plot::Layout_cache& layout_cache;
    vnm::plot::Primitive_renderer& primitives;
    vnm::plot::Series_renderer& series_renderer;
    vnm::plot::Chrome_renderer& chrome_renderer;
    std::map<int, std::shared_ptr<const vnm::plot::series_data_t>>& series_map;
    vnm::plot::Data_source* data_source;
#if defined(VNM_PLOT_ENABLE_TEXT)
    vnm::plot::Font_renderer* font_renderer = nullptr;
    vnm::plot::Text_renderer* text_renderer = nullptr;
#endif
};

/// Shared format_benchmark_timestamp function
std::string format_benchmark_timestamp(double ts, double step);

/// Update view range from data source (t_min, t_max, v_min, v_max)
/// Uses a 10-second sliding window
void update_view_range_from_source(
    vnm::plot::Data_source* source,
    const std::string& data_type,
    double& t_min,
    double& t_max,
    double& t_available_min,
    float& v_min,
    float& v_max);

/// Render a complete benchmark frame with all profiling scopes
/// This is the main entry point that combines all frame rendering logic
void render_benchmark_frame(
    Benchmark_frame_params& params,
    Benchmark_frame_context& ctx);

}  // namespace vnm::benchmark

#endif  // VNM_PLOT_BENCHMARK_FRAME_H
