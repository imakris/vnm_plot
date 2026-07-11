#pragma once

#include <vnm_plot/core/text_lcd.h>

namespace vnm::plot::detail {

enum class text_lcd_draw_surface_t
{
    VERTICAL_AXIS_LABEL,
    HORIZONTAL_AXIS_LABEL,
};

constexpr float k_text_lcd_opaque_alpha_cutoff = 0.999f;

constexpr text_lcd_resolved_subpixel_order_t text_lcd_sanitize_resolved_order(
    text_lcd_resolved_subpixel_order_t order)
{
    return vnm::msdf_text::lcd::is_display_specific(order)
        ? order
        : text_lcd_resolved_subpixel_order_t::NONE;
}

constexpr text_lcd_resolved_subpixel_order_t text_lcd_auto_order_from_detections(
    text_lcd_resolved_subpixel_order_t qt_order,
    text_lcd_resolved_subpixel_order_t os_order)
{
    if (vnm::msdf_text::lcd::is_display_specific(qt_order)) { return qt_order; }
    if (vnm::msdf_text::lcd::is_display_specific(os_order)) { return os_order; }
    return text_lcd_resolved_subpixel_order_t::NONE;
}

constexpr text_lcd_resolved_subpixel_order_t text_lcd_effective_order(
    text_lcd_request_t                 requested,
    text_lcd_resolved_subpixel_order_t auto_resolved)
{
    if (requested.automatic) {
        return text_lcd_sanitize_resolved_order(auto_resolved);
    }

    return text_lcd_sanitize_resolved_order(requested.resolved_order);
}

constexpr text_lcd_resolved_subpixel_order_t text_lcd_effective_order_for_frame(
    text_lcd_request_t                 requested,
    text_lcd_resolved_subpixel_order_t auto_resolved_order)
{
    return text_lcd_effective_order(requested, auto_resolved_order);
}

constexpr text_lcd_resolved_subpixel_order_t text_lcd_effective_order_for_frame(
    const text_lcd_request_t*          requested,
    text_lcd_resolved_subpixel_order_t auto_resolved_order)
{
    return requested != nullptr
        ? text_lcd_effective_order(*requested, auto_resolved_order)
        : text_lcd_sanitize_resolved_order(auto_resolved_order);
}

constexpr bool text_lcd_draw_is_eligible(
    text_lcd_draw_surface_t            surface,
    text_lcd_resolved_subpixel_order_t frame_order,
    float                              background_alpha,
    bool                               has_opaque_backing)
{
    if (!vnm::msdf_text::lcd::is_display_specific(frame_order) ||
        !(background_alpha >= k_text_lcd_opaque_alpha_cutoff))
    {
        return false;
    }

    switch (surface) {
        case text_lcd_draw_surface_t::VERTICAL_AXIS_LABEL:
        case text_lcd_draw_surface_t::HORIZONTAL_AXIS_LABEL: return has_opaque_backing;
        default:                                             return false;
    }
}

} // namespace vnm::plot::detail
