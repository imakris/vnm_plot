#include "text_lcd_resolver.h"
#include "../core/text_lcd_policy.h"

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

text_lcd_resolved_subpixel_order_t text_lcd_subpixel_order_from_qt(QQuickWindow* window)
{
    if (window == nullptr) {
        return text_lcd_resolved_subpixel_order_t::NONE;
    }

    QPlatformScreen* const platform_screen =
        QPlatformScreen::platformScreenForWindow(window);
    if (platform_screen == nullptr) {
        return text_lcd_resolved_subpixel_order_t::NONE;
    }

    return text_lcd_from_qt_subpixel_hint(
        static_cast<int>(platform_screen->subpixelAntialiasingTypeHint()));
}

text_lcd_resolved_subpixel_order_t text_lcd_subpixel_order_from_windows()
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
        return text_lcd_resolved_subpixel_order_t::NONE;
    }

    unsigned int font_smoothing_type = 0U;
    if (SystemParametersInfoW(
            k_win_spi_get_font_smoothing_type,
            0U,
            &font_smoothing_type,
            0U) == 0)
    {
        return text_lcd_resolved_subpixel_order_t::NONE;
    }

    unsigned int font_smoothing_orientation = 0U;
    if (SystemParametersInfoW(
            k_win_spi_get_font_smoothing_orientation,
            0U,
            &font_smoothing_orientation,
            0U) == 0)
    {
        return text_lcd_resolved_subpixel_order_t::NONE;
    }

    return text_lcd_from_windows_font_smoothing_settings(
        true,
        font_smoothing_type,
        font_smoothing_orientation);
#else
    return text_lcd_resolved_subpixel_order_t::NONE;
#endif
}

} // anonymous namespace

text_lcd_resolved_subpixel_order_t resolve_text_lcd_subpixel_order_from_probes(
    text_lcd_request_t                 requested,
    const text_lcd_resolver_probes_t&  probes)
{
    if (!requested.automatic) {
        return detail::text_lcd_effective_order(
            requested,
            text_lcd_resolved_subpixel_order_t::NONE);
    }

    const text_lcd_resolved_subpixel_order_t qt_order = probes.qt_probe
        ? detail::text_lcd_sanitize_resolved_order(probes.qt_probe())
        : text_lcd_resolved_subpixel_order_t::NONE;
    if (vnm::msdf_text::lcd::is_display_specific(qt_order)) {
        return qt_order;
    }

    const text_lcd_resolved_subpixel_order_t windows_order = probes.windows_probe
        ? detail::text_lcd_sanitize_resolved_order(probes.windows_probe())
        : text_lcd_resolved_subpixel_order_t::NONE;
    return detail::text_lcd_auto_order_from_detections(qt_order, windows_order);
}

text_lcd_resolved_subpixel_order_t resolve_text_lcd_subpixel_order_for_window(
    text_lcd_request_t requested,
    QQuickWindow*      window)
{
    text_lcd_resolver_probes_t probes;
    probes.qt_probe = [window] {
        return text_lcd_subpixel_order_from_qt(window);
    };
    probes.windows_probe = [] {
        return text_lcd_subpixel_order_from_windows();
    };
    return resolve_text_lcd_subpixel_order_from_probes(requested, probes);
}

text_lcd_resolved_subpixel_order_t text_lcd_from_qt_subpixel_hint(int qt_subpixel_hint)
{
    switch (qt_subpixel_hint) {
        case static_cast<int>(QPlatformScreen::Subpixel_RGB):  return text_lcd_resolved_subpixel_order_t::RGB;
        case static_cast<int>(QPlatformScreen::Subpixel_BGR):  return text_lcd_resolved_subpixel_order_t::BGR;
        case static_cast<int>(QPlatformScreen::Subpixel_VRGB): return text_lcd_resolved_subpixel_order_t::VRGB;
        case static_cast<int>(QPlatformScreen::Subpixel_VBGR): return text_lcd_resolved_subpixel_order_t::VBGR;
        case static_cast<int>(QPlatformScreen::Subpixel_None):
        default:                                               return text_lcd_resolved_subpixel_order_t::NONE;
    }
}

text_lcd_resolved_subpixel_order_t text_lcd_from_windows_font_smoothing_settings(
    bool           enabled,
    unsigned int   smoothing_type,
    unsigned int   smoothing_orientation)
{
    if (!enabled) {
        return text_lcd_resolved_subpixel_order_t::NONE;
    }

    if (smoothing_type != k_win_font_smoothing_cleartype) {
        return text_lcd_resolved_subpixel_order_t::NONE;
    }

    switch (smoothing_orientation) {
        case k_win_font_smoothing_orientation_rgb: return text_lcd_resolved_subpixel_order_t::RGB;
        case k_win_font_smoothing_orientation_bgr: return text_lcd_resolved_subpixel_order_t::BGR;
        default:                                   return text_lcd_resolved_subpixel_order_t::NONE;
    }
}

} // namespace vnm::plot
