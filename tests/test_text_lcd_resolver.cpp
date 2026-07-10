#include "test_macros.h"
#include "text_lcd_resolver.h"

#include <qpa/qplatformscreen.h>

#include <iostream>

namespace plot = vnm::plot;

namespace {

using request_t = plot::text_lcd_request_t;
using resolved_t = plot::text_lcd_resolved_subpixel_order_t;

struct explicit_request_case_t
{
    request_t  request;
    resolved_t expected;
};

constexpr unsigned int k_win_font_smoothing_standard          = 0x0001U;
constexpr unsigned int k_win_font_smoothing_cleartype         = 0x0002U;
constexpr unsigned int k_win_font_smoothing_orientation_bgr   = 0x0000U;
constexpr unsigned int k_win_font_smoothing_orientation_rgb   = 0x0001U;
constexpr unsigned int k_win_font_smoothing_orientation_other = 0x002AU;

bool test_default_null_probes_return_none()
{
    const plot::text_lcd_resolver_probes_t probes;
    TEST_ASSERT(
        plot::resolve_text_lcd_subpixel_order_from_probes(
            plot::text_lcd_auto_request(),
            probes) == resolved_t::NONE,
        "AUTO with default probes should fail closed to NONE");
    return true;
}

bool test_auto_prefers_qt_probe_and_skips_windows()
{
    bool windows_called = false;

    plot::text_lcd_resolver_probes_t probes;
    probes.qt_probe = [] {
        return resolved_t::RGB;
    };
    probes.windows_probe = [&windows_called] {
        windows_called = true;
        return resolved_t::BGR;
    };

    TEST_ASSERT(
        plot::resolve_text_lcd_subpixel_order_from_probes(
            plot::text_lcd_auto_request(),
            probes) == resolved_t::RGB,
        "Qt display-specific probe should win AUTO resolution");
    TEST_ASSERT(!windows_called, "Windows fallback should not run after Qt RGB");
    return true;
}

bool test_auto_falls_back_to_windows()
{
    plot::text_lcd_resolver_probes_t probes;
    probes.qt_probe = [] {
        return resolved_t::NONE;
    };
    probes.windows_probe = [] {
        return resolved_t::BGR;
    };

    TEST_ASSERT(
        plot::resolve_text_lcd_subpixel_order_from_probes(
            plot::text_lcd_auto_request(),
            probes) == resolved_t::BGR,
        "Windows display-specific probe should be used when Qt reports NONE");

    probes.windows_probe = [] {
        return resolved_t::NONE;
    };
    TEST_ASSERT(
        plot::resolve_text_lcd_subpixel_order_from_probes(
            plot::text_lcd_auto_request(),
            probes) == resolved_t::NONE,
        "AUTO should fail closed when both probes report NONE");
    return true;
}

bool test_auto_invalid_qt_result_falls_back_to_windows()
{
    plot::text_lcd_resolver_probes_t probes;
    probes.qt_probe = [] {
        return static_cast<resolved_t>(255);
    };
    probes.windows_probe = [] {
        return resolved_t::BGR;
    };

    TEST_ASSERT(
        plot::resolve_text_lcd_subpixel_order_from_probes(
            plot::text_lcd_auto_request(),
            probes) == resolved_t::BGR,
        "invalid Qt probe result should fall back to valid Windows result");
    return true;
}

bool test_explicit_orders_skip_probes()
{
    int probe_calls = 0;
    plot::text_lcd_resolver_probes_t probes;
    probes.qt_probe = [&probe_calls] {
        ++probe_calls;
        return resolved_t::RGB;
    };
    probes.windows_probe = [&probe_calls] {
        ++probe_calls;
        return resolved_t::BGR;
    };

    const explicit_request_case_t explicit_orders[] = {
        { plot::text_lcd_none_request(), resolved_t::NONE },
        { plot::text_lcd_explicit_request(resolved_t::RGB), resolved_t::RGB },
        { plot::text_lcd_explicit_request(resolved_t::BGR), resolved_t::BGR },
        { plot::text_lcd_explicit_request(resolved_t::VRGB), resolved_t::VRGB },
        { plot::text_lcd_explicit_request(resolved_t::VBGR), resolved_t::VBGR },
    };

    for (const explicit_request_case_t& item : explicit_orders) {
        TEST_ASSERT(
            plot::resolve_text_lcd_subpixel_order_from_probes(item.request, probes) ==
                item.expected,
            "explicit LCD order should resolve without platform probes");
    }

    TEST_ASSERT(probe_calls == 0, "explicit LCD requests should not call any probe");
    return true;
}

bool test_explicit_orders_for_null_window_are_deterministic()
{
    const explicit_request_case_t explicit_orders[] = {
        { plot::text_lcd_none_request(), resolved_t::NONE },
        { plot::text_lcd_explicit_request(resolved_t::RGB), resolved_t::RGB },
        { plot::text_lcd_explicit_request(resolved_t::BGR), resolved_t::BGR },
        { plot::text_lcd_explicit_request(resolved_t::VRGB), resolved_t::VRGB },
        { plot::text_lcd_explicit_request(resolved_t::VBGR), resolved_t::VBGR },
    };

    for (const explicit_request_case_t& item : explicit_orders) {
        TEST_ASSERT(
            plot::resolve_text_lcd_subpixel_order_for_window(item.request, nullptr) ==
                item.expected,
            "explicit LCD order should resolve for nullptr window without platform settings");
    }

    return true;
}

bool test_probe_auto_or_invalid_results_fail_closed()
{
    plot::text_lcd_resolver_probes_t probes;
    probes.qt_probe = [] {
        return resolved_t::NONE;
    };
    probes.windows_probe = [] {
        return static_cast<resolved_t>(255);
    };
    TEST_ASSERT(
        plot::resolve_text_lcd_subpixel_order_from_probes(
            plot::text_lcd_auto_request(),
            probes) == resolved_t::NONE,
        "Windows invalid probe result should fail closed");

    const request_t invalid_request{false, static_cast<resolved_t>(255)};
    int invalid_request_probe_calls = 0;
    probes.qt_probe = [&invalid_request_probe_calls] {
        ++invalid_request_probe_calls;
        return resolved_t::RGB;
    };
    probes.windows_probe = [&invalid_request_probe_calls] {
        ++invalid_request_probe_calls;
        return resolved_t::BGR;
    };
    TEST_ASSERT(
        plot::resolve_text_lcd_subpixel_order_from_probes(
            invalid_request,
            probes) == resolved_t::NONE,
        "invalid requested order should fail closed without probing");
    TEST_ASSERT(
        invalid_request_probe_calls == 0,
        "invalid requested order should not call platform probes");
    return true;
}

bool test_qt_subpixel_hint_mapping()
{
    TEST_ASSERT(
        plot::text_lcd_from_qt_subpixel_hint(
            static_cast<int>(QPlatformScreen::Subpixel_RGB)) ==
            resolved_t::RGB,
        "Qt RGB hint should map to RGB");
    TEST_ASSERT(
        plot::text_lcd_from_qt_subpixel_hint(
            static_cast<int>(QPlatformScreen::Subpixel_BGR)) ==
            resolved_t::BGR,
        "Qt BGR hint should map to BGR");
    TEST_ASSERT(
        plot::text_lcd_from_qt_subpixel_hint(
            static_cast<int>(QPlatformScreen::Subpixel_VRGB)) ==
            resolved_t::VRGB,
        "Qt VRGB hint should map to VRGB");
    TEST_ASSERT(
        plot::text_lcd_from_qt_subpixel_hint(
            static_cast<int>(QPlatformScreen::Subpixel_VBGR)) ==
            resolved_t::VBGR,
        "Qt VBGR hint should map to VBGR");
    TEST_ASSERT(
        plot::text_lcd_from_qt_subpixel_hint(
            static_cast<int>(QPlatformScreen::Subpixel_None)) ==
            resolved_t::NONE,
        "Qt None hint should map to NONE");
    TEST_ASSERT(
        plot::text_lcd_from_qt_subpixel_hint(255) ==
            resolved_t::NONE,
        "unknown Qt hint should fail closed");
    return true;
}

bool test_windows_font_smoothing_mapping()
{
    TEST_ASSERT(
        plot::text_lcd_from_windows_font_smoothing_settings(
            false,
            k_win_font_smoothing_cleartype,
            k_win_font_smoothing_orientation_rgb) ==
            resolved_t::NONE,
        "disabled Windows font smoothing should map to NONE");
    TEST_ASSERT(
        plot::text_lcd_from_windows_font_smoothing_settings(
            true,
            k_win_font_smoothing_standard,
            k_win_font_smoothing_orientation_rgb) ==
            resolved_t::NONE,
        "non-ClearType smoothing should map to NONE");
    TEST_ASSERT(
        plot::text_lcd_from_windows_font_smoothing_settings(
            true,
            k_win_font_smoothing_cleartype,
            k_win_font_smoothing_orientation_rgb) ==
            resolved_t::RGB,
        "ClearType RGB orientation should map to RGB");
    TEST_ASSERT(
        plot::text_lcd_from_windows_font_smoothing_settings(
            true,
            k_win_font_smoothing_cleartype,
            k_win_font_smoothing_orientation_bgr) ==
            resolved_t::BGR,
        "ClearType BGR orientation should map to BGR");
    TEST_ASSERT(
        plot::text_lcd_from_windows_font_smoothing_settings(
            true,
            k_win_font_smoothing_cleartype,
            k_win_font_smoothing_orientation_other) ==
            resolved_t::NONE,
        "unknown ClearType orientation should fail closed");
    return true;
}

} // namespace

int main()
{
    std::cout << "Text LCD resolver tests" << std::endl;

    int passed = 0;
    int failed = 0;

    RUN_TEST(test_default_null_probes_return_none);
    RUN_TEST(test_auto_prefers_qt_probe_and_skips_windows);
    RUN_TEST(test_auto_falls_back_to_windows);
    RUN_TEST(test_auto_invalid_qt_result_falls_back_to_windows);
    RUN_TEST(test_explicit_orders_skip_probes);
    RUN_TEST(test_explicit_orders_for_null_window_are_deterministic);
    RUN_TEST(test_probe_auto_or_invalid_results_fail_closed);
    RUN_TEST(test_qt_subpixel_hint_mapping);
    RUN_TEST(test_windows_font_smoothing_mapping);

    std::cout << "Results: " << passed << " passed, " << failed << " failed" << std::endl;
    return failed > 0 ? 1 : 0;
}
