#pragma once

// VNM Plot Library - LCD Text Rendering Options

#include <vnm_msdf_text/lcd_contract.h>

namespace vnm::plot {

using text_lcd_resolved_subpixel_order_t = vnm::msdf_text::lcd::Resolved_lcd_subpixel_order;

struct text_lcd_request_t
{
    bool automatic = true;
    text_lcd_resolved_subpixel_order_t resolved_order =
        text_lcd_resolved_subpixel_order_t::NONE;
};

constexpr text_lcd_request_t text_lcd_auto_request()
{
    return {true, text_lcd_resolved_subpixel_order_t::NONE};
}

constexpr text_lcd_request_t text_lcd_none_request()
{
    return {false, text_lcd_resolved_subpixel_order_t::NONE};
}

constexpr text_lcd_request_t text_lcd_explicit_request(
    text_lcd_resolved_subpixel_order_t resolved_order)
{
    return {
        false,
        vnm::msdf_text::lcd::is_display_specific(resolved_order)
            ? resolved_order
            : text_lcd_resolved_subpixel_order_t::NONE
    };
}

} // namespace vnm::plot
