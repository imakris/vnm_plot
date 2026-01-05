#pragma once

// VNM Plot Library - Core Chrome Renderer
// Qt-free renderer for grid lines, background panes, and preview overlay.

#include "types.h"

namespace vnm::plot {

class Primitive_renderer;

// -----------------------------------------------------------------------------
// Chrome Renderer
// -----------------------------------------------------------------------------
// Renders the plot "chrome": backgrounds, separators, grid lines, and preview.
class Chrome_renderer
{
public:
    Chrome_renderer() = default;

    // Batches background rects, calculates grid parameters, and draws grid.
    void render_grid_and_backgrounds(const frame_context_t& ctx, Primitive_renderer& prims);

    // Draws preview handles and dimming overlay.
    void render_preview_overlay(const frame_context_t& ctx, Primitive_renderer& prims);

private:
    // Helper to compute grid layer parameters for vertical axis
    grid_layer_params_t calculate_grid_params(double min, double max, double pixel_span, double font_px);
};

} // namespace vnm::plot
