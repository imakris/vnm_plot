#pragma once

// VNM Plot Library - Color Palette
// Theme colors for light and dark modes.

#include "plot_types.h"

#include <glm/vec4.hpp>

namespace vnm::plot {

// -----------------------------------------------------------------------------
// Color Palette
// -----------------------------------------------------------------------------
// All colors used by the plot renderer, organized by theme.
struct Color_palette
{
    // Main background color
    glm::vec4 background;

    // Horizontal label background (time axis)
    glm::vec4 h_label_background;

    // Vertical label background (value axis)
    glm::vec4 v_label_background;

    // Preview bar background
    glm::vec4 preview_background;

    // Separator lines
    glm::vec4 separator;

    // Grid lines
    glm::vec4 grid_line;

    // Preview dimming overlays
    glm::vec4 preview_cover;
    glm::vec4 preview_cover_secondary;

    // Default series color
    glm::vec4 default_series;

    // Text colors
    glm::vec4 text_primary;
    glm::vec4 text_secondary;

    // Basic colors
    glm::vec4 black;
    glm::vec4 white;

    // --- Factory methods ---

    static Color_palette light()
    {
        Color_palette p;
        p.background              = hex_to_vec4("ffffffff");
        p.h_label_background      = hex_to_vec4("ffbadaef");
        p.v_label_background      = hex_to_vec4("cc959595");
        p.preview_background      = hex_to_vec4("ffeeeeee");
        p.separator               = hex_to_vec4("ff999999");
        p.grid_line               = hex_to_vec4("ff000000");  // Black for light mode (matches Lumis)
        p.preview_cover           = hex_to_vec4("99707070");
        p.preview_cover_secondary = hex_to_vec4("85707070");
        p.default_series          = hex_to_vec4("ff2972a3");
        p.text_primary            = hex_to_vec4("ff000000");
        p.text_secondary          = hex_to_vec4("ff666666");
        p.black                   = hex_to_vec4("ff000000");
        p.white                   = hex_to_vec4("ffffffff");
        return p;
    }

    static Color_palette dark()
    {
        Color_palette p;
        p.background              = hex_to_vec4("ff1c1e22");
        p.h_label_background      = hex_to_vec4("ff3f4f60");
        p.v_label_background      = hex_to_vec4("ff2c2f34");
        p.preview_background      = hex_to_vec4("ff1f1f1f");
        p.separator               = hex_to_vec4("ff999999");
        p.grid_line               = hex_to_vec4("ffd2d4d7");
        p.preview_cover           = hex_to_vec4("26555555");
        p.preview_cover_secondary = hex_to_vec4("10505050");
        p.default_series          = hex_to_vec4("ff4da0e0");
        p.text_primary            = hex_to_vec4("ffffffff");
        p.text_secondary          = hex_to_vec4("ff999999");
        p.black                   = hex_to_vec4("ff000000");
        p.white                   = hex_to_vec4("ffffffff");
        return p;
    }

    // Get palette based on dark mode flag
    static Color_palette for_theme(bool dark_mode)
    {
        return dark_mode ? dark() : light();
    }
};

// -----------------------------------------------------------------------------
// Layout Constants
// -----------------------------------------------------------------------------
// These are rendering constants that don't vary by theme.
namespace constants {

// Layout metrics
constexpr float k_v_label_horizontal_padding_px = 6.0f;

// Grid & preview metrics
constexpr float  k_grid_line_half_px     = 0.7f;
constexpr float  k_cell_span_min_factor  = 0.1f;
constexpr float  k_cell_span_max_factor  = 6.0f;

// Label nudge factors
constexpr float  k_v_label_vertical_nudge_px = 0.1f;
constexpr float  k_h_label_vertical_nudge_px = 1.05f;

// Hit testing
constexpr float  k_hit_test_px = 1.0f;

// Grid appearance
constexpr float  k_grid_line_alpha_base = 0.75f;
constexpr int    k_max_grid_levels      = 32;

// Value formatting
constexpr int    k_value_decimals = 3;

// Text margins
constexpr float  k_text_margin_px   = 3.0f;
constexpr float  k_overlay_left_px  = 10.0f;
constexpr float  k_line_spacing     = 1.5f;

// Scissor padding
constexpr float  k_scissor_pad_px = 1.0f;

// Preview bar
constexpr double k_preview_band_max_px   = 5.0;
constexpr int    k_preview_min_window_px = 60;

// Font defaults
constexpr double k_default_font_px             = 10.0;
constexpr double k_default_base_label_height_px = 14.0;

// Internal constants (not user-configurable)
constexpr int    k_drawn_x_reserve               = 512;
constexpr int    k_index_growth_step             = 2;
constexpr double k_vbar_width_change_threshold_d = 0.5;
constexpr double k_vbar_min_width_px_d           = 1.0;
constexpr int    k_msaa_samples                  = 8;
constexpr int    k_rect_initial_quads            = 256;

// Precision constants
constexpr float  k_pixel_snap = 0.5f;
constexpr double k_eps        = 1e-6;

} // namespace constants

} // namespace vnm::plot
