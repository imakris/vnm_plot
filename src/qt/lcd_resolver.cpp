#include "lcd_resolver.h"
#include "../core/lcd_policy.h"

#include <QQuickWindow>
#include <qpa/qplatformscreen.h>

#if defined(_WIN32)
extern "C" __declspec(dllimport) int __stdcall SystemParametersInfoW(
    unsigned int action,
    unsigned int parameter,
    void*        value,
    unsigned int update_flags);
#endif

namespace vnm::plot {

namespace {

#if defined(_WIN32)
constexpr unsigned int k_win_spi_get_font_smoothing             = 0x004AU;
constexpr unsigned int k_win_spi_get_font_smoothing_type        = 0x200AU;
constexpr unsigned int k_win_spi_get_font_smoothing_orientation = 0x2012U;
#endif

constexpr unsigned int k_win_font_smoothing_cleartype       = 0x0002U;
constexpr unsigned int k_win_font_smoothing_orientation_bgr = 0x0000U;
constexpr unsigned int k_win_font_smoothing_orientation_rgb = 0x0001U;

lcd_subpixel_order_t lcd_subpixel_order_from_qt(QQuickWindow* window)
{
    if (window == nullptr) {
        return lcd_subpixel_order_t::NONE;
    }

    QPlatformScreen* const platform_screen =
        QPlatformScreen::platformScreenForWindow(window);
    if (platform_screen == nullptr) {
        return lcd_subpixel_order_t::NONE;
    }

    return lcd_from_qt_subpixel_hint(
        static_cast<int>(platform_screen->subpixelAntialiasingTypeHint()));
}

lcd_subpixel_order_t lcd_subpixel_order_from_windows()
{
#if defined(_WIN32)
    int font_smoothing_enabled = 0;
    if (SystemParametersInfoW(
            k_win_spi_get_font_smoothing,
            0U,
            &font_smoothing_enabled,
            0U)                == 0 ||
        font_smoothing_enabled == 0)
    {
        return lcd_subpixel_order_t::NONE;
    }

    unsigned int font_smoothing_type = 0U;
    if (SystemParametersInfoW(
            k_win_spi_get_font_smoothing_type,
            0U,
            &font_smoothing_type,
            0U) == 0)
    {
        return lcd_subpixel_order_t::NONE;
    }

    unsigned int font_smoothing_orientation = 0U;
    if (SystemParametersInfoW(
            k_win_spi_get_font_smoothing_orientation,
            0U,
            &font_smoothing_orientation,
            0U) == 0)
    {
        return lcd_subpixel_order_t::NONE;
    }

    return lcd_from_windows_font_smoothing_settings(
        true,
        font_smoothing_type,
        font_smoothing_orientation);
#else
    return lcd_subpixel_order_t::NONE;
#endif
}

} // anonymous namespace

lcd_subpixel_order_t resolve_lcd_subpixel_order_from_probes(
    lcd_request_t                requested,
    const lcd_resolver_probes_t& probes)
{
    if (!requested.automatic) {
        return detail::lcd_effective_order(
            requested,
            lcd_subpixel_order_t::NONE);
    }

    const lcd_subpixel_order_t qt_order = probes.qt_probe
        ? detail::lcd_sanitize_resolved_order(probes.qt_probe())
        : lcd_subpixel_order_t::NONE;
    if (vnm::msdf_text::lcd::is_display_specific(qt_order)) {
        return qt_order;
    }

    const lcd_subpixel_order_t windows_order = probes.windows_probe
        ? detail::lcd_sanitize_resolved_order(probes.windows_probe())
        : lcd_subpixel_order_t::NONE;
    return detail::lcd_auto_order_from_detections(qt_order, windows_order);
}

lcd_subpixel_order_t resolve_lcd_subpixel_order_for_window(
    lcd_request_t requested,
    QQuickWindow* window)
{
    lcd_resolver_probes_t probes;
    probes.qt_probe = [window] {
        return lcd_subpixel_order_from_qt(window);
    };
    probes.windows_probe = [] {
        return lcd_subpixel_order_from_windows();
    };
    return resolve_lcd_subpixel_order_from_probes(requested, probes);
}

lcd_subpixel_order_t lcd_from_qt_subpixel_hint(int qt_subpixel_hint)
{
    switch (qt_subpixel_hint) {
        case static_cast<int>(QPlatformScreen::Subpixel_RGB):  return lcd_subpixel_order_t::RGB;
        case static_cast<int>(QPlatformScreen::Subpixel_BGR):  return lcd_subpixel_order_t::BGR;
        case static_cast<int>(QPlatformScreen::Subpixel_VRGB): return lcd_subpixel_order_t::VRGB;
        case static_cast<int>(QPlatformScreen::Subpixel_VBGR): return lcd_subpixel_order_t::VBGR;
        case static_cast<int>(QPlatformScreen::Subpixel_None):
        default:                                               return lcd_subpixel_order_t::NONE;
    }
}

lcd_subpixel_order_t lcd_from_windows_font_smoothing_settings(
    bool           enabled,
    unsigned int   smoothing_type,
    unsigned int   smoothing_orientation)
{
    if (!enabled) {
        return lcd_subpixel_order_t::NONE;
    }

    if (smoothing_type != k_win_font_smoothing_cleartype) {
        return lcd_subpixel_order_t::NONE;
    }

    switch (smoothing_orientation) {
        case k_win_font_smoothing_orientation_rgb: return lcd_subpixel_order_t::RGB;
        case k_win_font_smoothing_orientation_bgr: return lcd_subpixel_order_t::BGR;
        default:                                   return lcd_subpixel_order_t::NONE;
    }
}

} // namespace vnm::plot
