#pragma once

// VNM Plot Library - Plot Types
// Re-exports core types and provides wrapper-specific configuration structures.

#include <vnm_plot/core/data_types.h>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace vnm::plot {

// -----------------------------------------------------------------------------
// Re-exported Core Types
// -----------------------------------------------------------------------------
// These types are defined in core and re-exported here for wrapper-level code.
using Display_style    = core::Display_style;
using shader_set_t     = core::shader_set_t;
using colormap_config_t = core::colormap_config_t;

// -----------------------------------------------------------------------------
// Time Follow Mode
// -----------------------------------------------------------------------------
enum class Time_follow_mode : int
{
    FOLLOWING             = 0,  // Follow live data (right edge = latest)
    INSPECTING            = 1,  // User is panning/zooming, don't auto-scroll
    FOLLOWING_AND_GROWING = 2   // Follow while history is loading
};

// -----------------------------------------------------------------------------
// Data Configuration
// -----------------------------------------------------------------------------
struct data_config_t
{
    // Value (vertical) range
    float v_min        = -1.f;
    float v_max        = 1.f;
    float v_manual_min = 0.f;
    float v_manual_max = 5.f;

    // Time (horizontal) range
    double t_min           = 5000.;
    double t_max           = 10000.;
    double t_available_min = 0.;
    double t_available_max = 10000.;

    // Bar width for OHLC-style charts
    double vbar_width = 150.;
};

// -----------------------------------------------------------------------------
// Axis Label Structures
// -----------------------------------------------------------------------------
struct vertical_label_t
{
    double      value;
    float       y;     // Y coordinate in plot space
    std::string text;
};

struct horizontal_label_t
{
    double      value;
    glm::vec2   position;
    std::string text;
};

// -----------------------------------------------------------------------------
// Layout Result
// -----------------------------------------------------------------------------
struct layout_result_t
{
    double usable_width            = 0.0;
    double usable_height           = 0.0;
    double v_bar_width             = 0.0;
    float  max_v_label_text_width  = 0.f;

    std::vector<horizontal_label_t> h_labels;
    std::vector<vertical_label_t>   v_labels;

    int    v_label_fixed_digits   = 0;
    bool   h_labels_subsecond     = false;

    // Seed values for incremental layout computation
    int    vertical_seed_index    = -1;
    double vertical_seed_step     = 0.0;
    double vertical_finest_step   = 0.0;
    int    horizontal_seed_index  = -1;
    double horizontal_seed_step   = 0.0;
};

// -----------------------------------------------------------------------------
// Grid Layer Parameters
// -----------------------------------------------------------------------------
struct grid_layer_params_t
{
    static constexpr int k_max_levels = 32;

    int   count = 0;
    float spacing_px[k_max_levels]   = {};
    float start_px[k_max_levels]     = {};
    float alpha[k_max_levels]        = {};
    float thickness_px[k_max_levels] = {};
};

// -----------------------------------------------------------------------------
// Color Utilities
// -----------------------------------------------------------------------------
constexpr int hex_char_to_int(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return 0;
}

constexpr float hex2f(const char* str)
{
    return static_cast<float>(hex_char_to_int(str[0]) * 16 + hex_char_to_int(str[1])) / 255.0f;
}

// Converts "aarrggbb" hex string to glm::vec4(r, g, b, a)
constexpr glm::vec4 hex_to_vec4(const char* str)
{
    return glm::vec4(hex2f(str + 2), hex2f(str + 4), hex2f(str + 6), hex2f(str));
}

} // namespace vnm::plot
