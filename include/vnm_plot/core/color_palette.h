#pragma once

// VNM Plot Library - Core Color Palette
// Theme colors for light and dark modes.

#include <glm/vec4.hpp>

#include <cstdint>

namespace vnm::plot {

// -----------------------------------------------------------------------------
// Color Utilities
// -----------------------------------------------------------------------------

constexpr int hex_char_to_int(char c)
{
    if (c >= '0' && c <= '9') { return c - '0';      }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
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

inline glm::vec4 rgba_u8(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255)
{
    constexpr float k_inv_255 = 1.0f / 255.0f;
    return glm::vec4(
        static_cast<float>(r) * k_inv_255,
        static_cast<float>(g) * k_inv_255,
        static_cast<float>(b) * k_inv_255,
        static_cast<float>(a) * k_inv_255);
}

inline glm::vec4 rgb_hex(std::uint32_t rgb)
{
    const std::uint8_t r = static_cast<std::uint8_t>((rgb >> 16) & 0xFFu);
    const std::uint8_t g = static_cast<std::uint8_t>((rgb >> 8) & 0xFFu);
    const std::uint8_t b = static_cast<std::uint8_t>(rgb & 0xFFu);
    return rgba_u8(r, g, b, 255);
}

inline glm::vec4 rgba_hex(std::uint32_t rgba)
{
    const std::uint8_t r = static_cast<std::uint8_t>((rgba >> 24) & 0xFFu);
    const std::uint8_t g = static_cast<std::uint8_t>((rgba >> 16) & 0xFFu);
    const std::uint8_t b = static_cast<std::uint8_t>((rgba >> 8) & 0xFFu);
    const std::uint8_t a = static_cast<std::uint8_t>(rgba & 0xFFu);
    return rgba_u8(r, g, b, a);
}

// -----------------------------------------------------------------------------
// Color Palette
// -----------------------------------------------------------------------------
// All colors used by the plot renderer, organized by theme.
struct Color_palette
{
    glm::vec4 background              = hex_to_vec4("ff1c1e22");
    glm::vec4 h_label_background      = hex_to_vec4("ff3f4f60");
    glm::vec4 v_label_background      = hex_to_vec4("ff2c2f34");
    glm::vec4 preview_background      = hex_to_vec4("ff1f1f1f");
    glm::vec4 separator               = hex_to_vec4("ff999999");
    glm::vec4 grid_line               = hex_to_vec4("ffd2d4d7");
    glm::vec4 preview_cover           = hex_to_vec4("26555555");
    glm::vec4 preview_cover_secondary = hex_to_vec4("10505050");

    // --- Factory methods ---

    static Color_palette dark()
    {
        return Color_palette();
    }

    static Color_palette light()
    {
        Color_palette p;
        p.background                  = hex_to_vec4("ffffffff");
        p.h_label_background          = hex_to_vec4("ffbadaef");
        p.v_label_background          = hex_to_vec4("cc959595");
        p.preview_background          = hex_to_vec4("ffeeeeee");
        p.separator                   = hex_to_vec4("ff999999");
        p.grid_line                   = hex_to_vec4("ff000000");
        p.preview_cover               = hex_to_vec4("99707070");
        p.preview_cover_secondary     = hex_to_vec4("85707070");
        return p;
    }


    // Get palette based on dark mode flag
    static Color_palette for_theme(bool dark_mode)
    {
        return dark_mode ? dark() : light();
    }
};

} // namespace vnm::plot
