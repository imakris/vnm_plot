#pragma once

#include <vnm_plot/core/types.h>

namespace vnm::plot {

/**
 * Build layered time-grid parameters for a visible seconds-domain range.
 */
grid_layer_params_t build_time_grid_layers(
    double t_min_seconds,
    double t_max_seconds,
    double width_px,
    double font_px,
    bool* out_dropped_non_multiple_step_or_null = nullptr);

} // namespace vnm::plot
