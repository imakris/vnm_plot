#ifndef VNM_PLOT_BENCHMARK_CONSTANTS_H
#define VNM_PLOT_BENCHMARK_CONSTANTS_H

// Shared layout constants for vnm_plot benchmarks
// Used by both Qt and headless GLFW benchmarks to ensure identical render paths

namespace vnm::benchmark {

// Layout configuration matching the standard vnm_plot rendering
constexpr double k_adjusted_font_px = 12.0;
constexpr double k_base_label_height_px = 14.0;
constexpr double k_adjusted_preview_height = 40.0;  // Preview bar height
constexpr double k_vbar_width_pixels = 60.0;        // Value bar width

}  // namespace vnm::benchmark

#endif  // VNM_PLOT_BENCHMARK_CONSTANTS_H
