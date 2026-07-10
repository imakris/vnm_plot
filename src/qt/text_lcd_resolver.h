#pragma once

#include <vnm_plot/core/text_lcd.h>

#include <functional>

class QQuickWindow;

namespace vnm::plot {

struct text_lcd_resolver_probes_t
{
    using probe_t = std::function<text_lcd_resolved_subpixel_order_t()>;

    probe_t qt_probe      = nullptr;
    probe_t windows_probe = nullptr;
};

text_lcd_resolved_subpixel_order_t resolve_text_lcd_subpixel_order_from_probes(
    text_lcd_request_t                 requested,
    const text_lcd_resolver_probes_t&  probes);

text_lcd_resolved_subpixel_order_t resolve_text_lcd_subpixel_order_for_window(
    text_lcd_request_t requested,
    QQuickWindow*      window);

text_lcd_resolved_subpixel_order_t text_lcd_from_qt_subpixel_hint(
    int                qt_subpixel_hint);

text_lcd_resolved_subpixel_order_t text_lcd_from_windows_font_smoothing_settings(
    bool               enabled,
    unsigned int       smoothing_type,
    unsigned int       smoothing_orientation);

} // namespace vnm::plot
