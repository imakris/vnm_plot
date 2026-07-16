#pragma once

#include <vnm_plot/core/lcd.h>

#include <functional>

class QQuickWindow;

namespace vnm::plot {

struct lcd_resolver_probes_t
{
    using probe_t = std::function<lcd_subpixel_order_t()>;

    probe_t qt_probe      = nullptr;
    probe_t windows_probe = nullptr;
};

lcd_subpixel_order_t resolve_lcd_subpixel_order_from_probes(
    lcd_request_t                requested,
    const lcd_resolver_probes_t& probes);

lcd_subpixel_order_t resolve_lcd_subpixel_order_for_window(
    lcd_request_t  requested,
    QQuickWindow*  window);

lcd_subpixel_order_t lcd_from_qt_subpixel_hint(int qt_subpixel_hint);

lcd_subpixel_order_t lcd_from_windows_font_smoothing_settings(
    bool         enabled,
    unsigned int smoothing_type,
    unsigned int smoothing_orientation);

} // namespace vnm::plot
