#pragma once
// VNM Plot Library - Core Layout Calculator
// Computes axis labels, grid positions, and layout metrics.
// This is pure computational logic with no OpenGL dependencies.

#include "layout_types.h"

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace vnm::plot {
class Profiler;
}

namespace vnm::plot::core {

// -----------------------------------------------------------------------------
// Layout Calculator
// -----------------------------------------------------------------------------
// Computes axis labels and layout metrics based on data ranges and viewport.
// Stateless - all inputs provided via parameters struct.
class Layout_calculator
{
public:
    // All inputs for a layout calculation pass
    struct parameters_t
    {
        // Data ranges
        float  v_min;
        float  v_max;
        double t_min;
        double t_max;

        // Viewport dimensions
        double usable_width;
        double usable_height;
        double vbar_width;
        double label_visible_height;

        // Font metrics
        double    adjusted_font_size_in_pixels;
        float     h_label_vertical_nudge_factor = 0.0f;
        uint64_t  measure_text_cache_key        = 0;
        float     monospace_char_advance_px     = 0.f;
        bool      monospace_advance_is_reliable = false;

        // Callbacks for metrics and formatting
        std::function<int(double)>              get_required_fixed_digits_func;
        std::function<std::string(double, double)> format_timestamp_func;
        std::function<float(const char*)>       measure_text_func;

        // Optional profiler (from Plot_config)
        vnm::plot::Profiler* profiler = nullptr;

        // Seed hints for incremental computation
        bool   has_vertical_seed     = false;
        int    vertical_seed_index   = -1;
        double vertical_seed_step    = 0.0;

        bool   has_horizontal_seed   = false;
        int    horizontal_seed_index = -1;
        double horizontal_seed_step  = 0.0;
    };

    // Calculation result
    struct result_t
    {
        std::vector<v_label_t> v_labels;
        std::vector<h_label_t> h_labels;

        int    v_label_fixed_digits   = 1;
        bool   h_labels_subsecond     = false;
        float  max_v_label_text_width = 0.f;

        int    vertical_seed_index    = -1;
        double vertical_seed_step     = 0.0;
        double vertical_finest_step   = 0.0;
        int    horizontal_seed_index  = -1;
        double horizontal_seed_step   = 0.0;
    };

    Layout_calculator() = default;

    // Main entry point - calculate layout from parameters
    result_t calculate(const parameters_t& params) const;

private:
    // Check if intervals fit without overlap
    bool fits_with_gap(
        const std::vector<std::pair<float, float>>& level,
        const std::vector<std::pair<float, float>>& accepted,
        float min_gap) const;

    // Scratch buffers (reused to avoid allocations)
    mutable std::vector<std::pair<double, float>> m_scratch_vals;
    mutable std::vector<std::pair<float, float>>  m_scratch_level;
    mutable std::vector<std::pair<float, float>>  m_scratch_accepted_boxes;
    mutable std::vector<float>                    m_scratch_accepted_y;
    mutable std::vector<double>                   m_scratch_vals_d;
};

// Shutdown caches (call during application shutdown)
void shutdown_layout_caches();

} // namespace vnm::plot::core
