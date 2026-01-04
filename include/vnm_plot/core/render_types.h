#pragma once

// VNM Plot Library - Core Render Types
// Qt-free frame context and render state structures.

#include "core_types.h"
#include "layout_types.h"

#include <glm/mat4x4.hpp>

#include <functional>
#include <string>

namespace vnm::plot::core {

// -----------------------------------------------------------------------------
// Render Configuration (minimal subset for core renderers)
// -----------------------------------------------------------------------------

/// Minimal configuration needed by core renderers
struct Render_config
{
    bool dark_mode = false;
    bool show_text = true;

    // Line rendering
    bool   snap_lines_to_pixels = true;
    double line_width_px        = 1.0;
    double area_fill_alpha      = 0.3;

    // Timestamp formatting (optional)
    // Parameters: timestamp (unix seconds), visible_range (seconds shown)
    // Returns: formatted string for display
    std::function<std::string(double timestamp, double visible_range)> format_timestamp;

    // Logging (optional)
    std::function<void(const std::string&)> log_error;
};

// -----------------------------------------------------------------------------
// Frame Context
// -----------------------------------------------------------------------------

/// Transient context for a single frame render.
/// Exists only on the stack during render calls.
struct frame_context_t
{
    // The layout result (labels, bar widths)
    const frame_layout_result_t& layout;

    // Validated value ranges for this frame
    float v0 = 0.0f;
    float v1 = 1.0f;
    float preview_v0 = 0.0f;
    float preview_v1 = 1.0f;

    // Time range (visible)
    double t0 = 0.0;
    double t1 = 1.0;

    // Time range (available/full extent)
    double t_available_min = 0.0;
    double t_available_max = 1.0;

    // Viewport dimensions
    int win_w = 0;
    int win_h = 0;

    // Projection-Model-View matrix
    glm::mat4 pmv{1.0f};

    // Font & layout metrics
    double adjusted_font_px       = 10.0;
    double base_label_height_px   = 14.0;
    double adjusted_reserved_height = 0.0;
    double adjusted_preview_height  = 0.0;

    // Display flags
    bool show_info = false;

    // Configuration reference (may be null for minimal rendering)
    const Render_config* config = nullptr;
};

} // namespace vnm::plot::core
