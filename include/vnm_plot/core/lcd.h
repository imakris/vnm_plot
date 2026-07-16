#pragma once

// VNM Plot Library - LCD Rendering Options

#include <vnm_msdf_text/lcd_contract.h>

namespace vnm::plot {

using lcd_subpixel_order_t = vnm::msdf_text::lcd::Resolved_lcd_subpixel_order;

struct lcd_request_t
{
    bool automatic = true;
    lcd_subpixel_order_t resolved_order = lcd_subpixel_order_t::NONE;
};

constexpr lcd_request_t lcd_auto_request()
{
    return {true, lcd_subpixel_order_t::NONE};
}

constexpr lcd_request_t lcd_none_request()
{
    return {false, lcd_subpixel_order_t::NONE};
}

constexpr lcd_request_t lcd_explicit_request(lcd_subpixel_order_t resolved_order)
{
    return {
        false,
        vnm::msdf_text::lcd::is_display_specific(resolved_order)
            ? resolved_order
            : lcd_subpixel_order_t::NONE
    };
}

} // namespace vnm::plot
