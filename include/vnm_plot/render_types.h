#pragma once

// VNM Plot Library - Render Types
// Structures for frame context and render state.

#include "data_source.h"
#include "layout/layout_calculator.h"
#include "plot_config.h"
#include "plot_types.h"

#include <glm/glm.hpp>

#include <map>
#include <memory>

namespace vnm::plot {

// -----------------------------------------------------------------------------
// Render Snapshot
// -----------------------------------------------------------------------------
// Long-lived state on the render thread.
// Populated inside synchronize() by copying from the UI thread.
struct render_snapshot_t
{
    // Configuration copies
    data_config_t cfg;
    bool visible = false;
    bool show_info = false;
    bool v_auto = true;

    // Plot configuration snapshot
    Plot_config config;

    // Data pointers (shared ownership to prevent deletion mid-frame)
    std::map<int, std::shared_ptr<Data_source>> data;

    // Font metrics
    double adjusted_font_px = 12.0;
    double base_label_height_px = 14.0;
    double vbar_width_pixels = 0.0;
    double adjusted_reserved_height = 0.0;
    double adjusted_preview_height = 0.0;
};

// -----------------------------------------------------------------------------
// Frame Context
// -----------------------------------------------------------------------------
// Strictly transient. Exists only on the stack during render.
struct frame_context_t
{
    // The data snapshot
    const render_snapshot_t& snapshot;

    // The layout result (labels, bar widths)
    const frame_layout_result_t& layout;

    // Validated ranges for this frame
    float v0, v1;
    float preview_v0, preview_v1;
    double t0, t1;

    // Viewport & Matrix
    int win_w;
    int win_h;
    glm::mat4 pmv;

    // Configuration reference
    const Plot_config* config = nullptr;
};

} // namespace vnm::plot
