// vnm_plot Font_renderer visual bounds tests

#include "test_macros.h"

#include <vnm_plot/rhi/asset_loader.h>
#include <vnm_plot/rhi/font_renderer.h>

#include <glm/glm.hpp>

#include <cmath>
#include <iostream>
#include <string>

namespace plot = vnm::plot;

namespace {

constexpr int   k_test_font_px   = 18;
constexpr float k_bounds_epsilon = 1.0e-4f;

bool finite_ordered(const glm::vec4& bounds)
{
    return
        std::isfinite(bounds.x) &&
        std::isfinite(bounds.y) &&
        std::isfinite(bounds.z) &&
        std::isfinite(bounds.w) &&
        bounds.z > bounds.x &&
        bounds.w > bounds.y;
}

bool nearly_equal(float lhs, float rhs)
{
    return std::abs(lhs - rhs) <= k_bounds_epsilon;
}

struct Scoped_font_disk_cache_setting
{
    bool previous = plot::font_disk_cache_enabled();

    explicit Scoped_font_disk_cache_setting(bool enabled)
    {
        plot::set_font_disk_cache_enabled(enabled);
    }

    ~Scoped_font_disk_cache_setting()
    {
        plot::set_font_disk_cache_enabled(previous);
    }
};

bool test_bounds_fail_closed_without_atlas()
{
    plot::Font_renderer renderer;
    glm::vec4 bounds(1.0f);

    TEST_ASSERT(!renderer.text_visual_bounds_px(nullptr, 0.0f, 0.0f, bounds),
        "null text must fail closed before font initialization");
    TEST_ASSERT(!renderer.text_visual_bounds_px("Axis", 0.0f, 0.0f, bounds),
        "visible text must fail closed before font initialization");
    return true;
}

bool test_bounds_fail_closed_for_no_visible_glyphs()
{
    Scoped_font_disk_cache_setting cache_setting(false);

    plot::Asset_loader loader;
    plot::init_embedded_assets(loader);

    plot::Font_renderer renderer;
    renderer.initialize_metrics(loader, k_test_font_px, true);

    glm::vec4 bounds(1.0f);
    TEST_ASSERT(!renderer.text_visual_bounds_px(nullptr, 10.0f, 20.0f, bounds),
        "null text must fail closed after font initialization");
    TEST_ASSERT(!renderer.text_visual_bounds_px("", 10.0f, 20.0f, bounds),
        "empty text must fail closed as no visible glyphs");
    TEST_ASSERT(!renderer.text_visual_bounds_px("   ", 10.0f, 20.0f, bounds),
        "space-only text must fail closed as no visible glyphs");
    return true;
}

bool test_visible_bounds_are_finite_ordered_and_translation_invariant()
{
    Scoped_font_disk_cache_setting cache_setting(false);

    plot::Asset_loader loader;
    plot::init_embedded_assets(loader);

    plot::Font_renderer renderer;
    renderer.initialize_metrics(loader, k_test_font_px, true);

    constexpr const char* k_text = "Axis 123";
    constexpr float       x      = 12.25f;
    constexpr float       y      = 34.5f;
    constexpr float       dx     = 7.75f;
    constexpr float       dy     = -3.5f;

    glm::vec4 first;
    glm::vec4 translated;
    TEST_ASSERT(renderer.text_visual_bounds_px(k_text, x, y, first),
        "visible text must produce visual bounds after font initialization");
    TEST_ASSERT(finite_ordered(first),
        "visible text bounds must be finite and ordered");

    TEST_ASSERT(renderer.text_visual_bounds_px(k_text, x + dx, y + dy, translated),
        "translated visible text must produce visual bounds");
    TEST_ASSERT(finite_ordered(translated),
        "translated visible text bounds must be finite and ordered");

    TEST_ASSERT(nearly_equal(translated.x - first.x, dx),
        "translated left bound must shift by dx");
    TEST_ASSERT(nearly_equal(translated.z - first.z, dx),
        "translated right bound must shift by dx");
    TEST_ASSERT(nearly_equal(translated.y - first.y, dy),
        "translated top bound must shift by dy");
    TEST_ASSERT(nearly_equal(translated.w - first.w, dy),
        "translated bottom bound must shift by dy");
    TEST_ASSERT(nearly_equal(translated.z - translated.x, first.z - first.x),
        "translated width must remain unchanged");
    TEST_ASSERT(nearly_equal(translated.w - translated.y, first.w - first.y),
        "translated height must remain unchanged");

    const float first_width  = first.z - first.x;
    const float first_height = first.w - first.y;
    const float advance      = renderer.measure_text_px(k_text);
    TEST_ASSERT(first_width > advance * 0.45f,
        "visible text bounds width must be plausible relative to text advance");
    TEST_ASSERT(first_width < advance + static_cast<float>(k_test_font_px),
        "visible text bounds width must not greatly exceed text advance");
    TEST_ASSERT(first_height > static_cast<float>(k_test_font_px) * 0.25f,
        "visible text bounds height must be plausible relative to font size");
    TEST_ASSERT(first_height < static_cast<float>(k_test_font_px) * 1.5f,
        "visible text bounds height must not greatly exceed font size");

    constexpr const char* k_single_glyph   = "0";
    constexpr const char* k_repeated_glyph = "0000";
    glm::vec4 single_bounds;
    glm::vec4 repeated_bounds;
    TEST_ASSERT(renderer.text_visual_bounds_px(k_single_glyph, x, y, single_bounds),
        "single visible glyph must produce visual bounds");
    TEST_ASSERT(renderer.text_visual_bounds_px(k_repeated_glyph, x, y, repeated_bounds),
        "repeated visible glyphs must produce visual bounds");

    const float single_width     = single_bounds.z - single_bounds.x;
    const float repeated_width   = repeated_bounds.z - repeated_bounds.x;
    const float single_advance   = renderer.measure_text_px(k_single_glyph);
    const float repeated_advance = renderer.measure_text_px(k_repeated_glyph);
    TEST_ASSERT(repeated_advance > single_advance * 3.5f,
        "repeated glyph advance must grow monotonically");
    TEST_ASSERT(repeated_width > single_width * 2.5f,
        "repeated glyph bounds must be wider than one glyph");
    TEST_ASSERT(repeated_width < repeated_advance + static_cast<float>(k_test_font_px),
        "repeated glyph bounds width must stay plausible relative to advance");
    return true;
}

} // namespace

int main()
{
    std::cout << "Font renderer bounds tests" << std::endl;

    int passed = 0;
    int failed = 0;

    RUN_TEST(test_bounds_fail_closed_without_atlas);
    RUN_TEST(test_bounds_fail_closed_for_no_visible_glyphs);
    RUN_TEST(test_visible_bounds_are_finite_ordered_and_translation_invariant);

    std::cout << "Results: " << passed << " passed, " << failed << " failed" << std::endl;
    return failed > 0 ? 1 : 0;
}
