#pragma once

// VNM Plot Library - LCD Text Rendering Options

#include <cstdint>

namespace vnm::plot {

enum class text_lcd_subpixel_order_t : std::uint8_t
{
    NONE = 0,
    RGB  = 1,
    BGR  = 2,
    VRGB = 3,
    VBGR = 4,
    AUTO = 5,
};

enum class text_lcd_draw_surface_t
{
    VERTICAL_AXIS_LABEL,
    HORIZONTAL_AXIS_LABEL,
    INFO_OVERLAY,
    PLOT_BODY_TEXT,
    SHADOWED_TEXT,
};

constexpr bool text_lcd_subpixel_order_is_display_specific(text_lcd_subpixel_order_t order)
{
    switch (order) {
        case text_lcd_subpixel_order_t::RGB:
        case text_lcd_subpixel_order_t::BGR:
        case text_lcd_subpixel_order_t::VRGB:
        case text_lcd_subpixel_order_t::VBGR:
            return true;
        case text_lcd_subpixel_order_t::NONE:
        case text_lcd_subpixel_order_t::AUTO:
        default:
            return false;
    }
}

constexpr text_lcd_subpixel_order_t text_lcd_auto_order_from_detections(
    text_lcd_subpixel_order_t qt_order,
    text_lcd_subpixel_order_t os_order)
{
    if (text_lcd_subpixel_order_is_display_specific(qt_order)) {
        return qt_order;
    }
    if (text_lcd_subpixel_order_is_display_specific(os_order)) {
        return os_order;
    }
    return text_lcd_subpixel_order_t::NONE;
}

constexpr text_lcd_subpixel_order_t text_lcd_effective_order(
    text_lcd_subpixel_order_t requested,
    text_lcd_subpixel_order_t auto_resolved)
{
    switch (requested) {
        case text_lcd_subpixel_order_t::AUTO:
            return text_lcd_subpixel_order_is_display_specific(auto_resolved)
                ? auto_resolved
                : text_lcd_subpixel_order_t::NONE;
        case text_lcd_subpixel_order_t::RGB:
        case text_lcd_subpixel_order_t::BGR:
        case text_lcd_subpixel_order_t::VRGB:
        case text_lcd_subpixel_order_t::VBGR:
        case text_lcd_subpixel_order_t::NONE:
            return requested;
        default:
            return text_lcd_subpixel_order_t::NONE;
    }
}

constexpr float text_lcd_shader_uniform_value(text_lcd_subpixel_order_t order)
{
    switch (order) {
        case text_lcd_subpixel_order_t::RGB:  return 1.0f;
        case text_lcd_subpixel_order_t::BGR:  return 2.0f;
        case text_lcd_subpixel_order_t::VRGB: return 3.0f;
        case text_lcd_subpixel_order_t::VBGR: return 4.0f;
        case text_lcd_subpixel_order_t::NONE:
        case text_lcd_subpixel_order_t::AUTO:
        default:                              return 0.0f;
    }
}

constexpr text_lcd_subpixel_order_t text_lcd_effective_order_for_frame(
    text_lcd_subpixel_order_t requested,
    text_lcd_subpixel_order_t platform_order,
    int render_target_sample_count)
{
    // MSAA sample count is accepted for caller compatibility, but ordinary
    // fragment-frequency LCD text is gated by draw-surface safety instead.
    (void)render_target_sample_count;
    return text_lcd_effective_order(requested, platform_order);
}

constexpr bool text_lcd_draw_is_eligible(
    text_lcd_draw_surface_t surface,
    text_lcd_subpixel_order_t frame_order,
    float background_alpha,
    bool has_opaque_backing)
{
    if (!text_lcd_subpixel_order_is_display_specific(frame_order) ||
        !(background_alpha >= 0.999f))
    {
        return false;
    }

    switch (surface) {
        case text_lcd_draw_surface_t::VERTICAL_AXIS_LABEL:
        case text_lcd_draw_surface_t::HORIZONTAL_AXIS_LABEL:
            return has_opaque_backing;
        case text_lcd_draw_surface_t::INFO_OVERLAY:
        case text_lcd_draw_surface_t::PLOT_BODY_TEXT:
        case text_lcd_draw_surface_t::SHADOWED_TEXT:
        default:
            return false;
    }
}

} // namespace vnm::plot
