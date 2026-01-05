#pragma once

// VNM Plot Library - Core Color Palette
// Theme colors for light and dark modes.

#include <glm/vec4.hpp>

namespace vnm::plot {

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
        p.grid_line               = hex_to_vec4("ff000000");  // Black for light mode
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

} // namespace vnm::plot
