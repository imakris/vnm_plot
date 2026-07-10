// vnm_plot label pane geometry tests

#include "test_macros.h"

#include "../src/core/label_pane_geometry.h"

#include <glm/glm.hpp>

#include <cmath>
#include <iostream>
#include <limits>

namespace plot = vnm::plot;

namespace {

constexpr float k_rect_epsilon = 1.0e-4f;

bool nearly_equal(float lhs, float rhs)
{
    return std::abs(lhs - rhs) <= k_rect_epsilon;
}

bool rect_equal(const glm::vec4& lhs, const glm::vec4& rhs)
{
    return
        nearly_equal(lhs.x, rhs.x) &&
        nearly_equal(lhs.y, rhs.y) &&
        nearly_equal(lhs.z, rhs.z) &&
        nearly_equal(lhs.w, rhs.w);
}

bool scissor_inside_frame(const plot::text_scissor_t& scissor, int width, int height)
{
    return
        scissor.enabled                    &&
        scissor.x >= 0                     &&
        scissor.y >= 0                     &&
        scissor.width > 0                  &&
        scissor.height > 0                 &&
        scissor.x + scissor.width <= width &&
        scissor.y + scissor.height <= height;
}

bool test_standard_pane_rects_match_chrome_lanes()
{
    plot::frame_layout_result_t layout;
    layout.usable_width  = 640.0;
    layout.usable_height = 360.0;
    layout.v_bar_width   = 64.0;

    plot::frame_context_t ctx{layout};
    ctx.win_w                = 800;
    ctx.win_h                = 480;
    ctx.base_label_height_px = 48.0;

    glm::vec4 horizontal;
    glm::vec4 vertical;
    TEST_ASSERT(plot::detail::horizontal_axis_label_pane_rect(ctx, horizontal),
        "standard horizontal axis label pane must be present");
    TEST_ASSERT(plot::detail::vertical_axis_label_pane_rect(ctx, vertical),
        "standard vertical axis label pane must be present");

    TEST_ASSERT(rect_equal(horizontal, glm::vec4(0.0f, 360.0f, 800.0f, 408.0f)),
        "horizontal label pane must match the chrome lane");
    TEST_ASSERT(rect_equal(vertical, glm::vec4(640.0f, 0.0f, 704.0f, 360.0f)),
        "vertical label pane must match the chrome lane");
    return true;
}

bool test_no_preview_horizontal_pane_scissor_reaches_frame_bottom()
{
    plot::frame_layout_result_t layout;
    layout.usable_width  = 840.0;
    layout.usable_height = 466.0;
    layout.v_bar_width   = 60.0;

    plot::frame_context_t ctx{layout};
    ctx.win_w                = 900;
    ctx.win_h                = 500;
    ctx.base_label_height_px = 34.0;

    glm::vec4 horizontal;
    TEST_ASSERT(plot::detail::horizontal_axis_label_pane_rect(ctx, horizontal),
        "horizontal axis label pane ending at framebuffer bottom must be present");
    TEST_ASSERT(rect_equal(horizontal, glm::vec4(0.0f, 466.0f, 900.0f, 500.0f)),
        "horizontal axis label pane must clamp to the framebuffer bottom");

    plot::text_scissor_t scissor;
    TEST_ASSERT(plot::detail::framebuffer_scissor_from_top_left_rect(
        horizontal, ctx.win_w, ctx.win_h, scissor),
        "horizontal axis label pane must convert to an in-frame scissor");
    TEST_ASSERT(scissor.x == 0 && scissor.y == 0 && scissor.width == 900 && scissor.height == 34,
        "frame-bottom horizontal pane must map to QRhi y=0 scissor");
    TEST_ASSERT(scissor_inside_frame(scissor, ctx.win_w, ctx.win_h),
        "frame-bottom horizontal pane scissor must stay inside the framebuffer");
    return true;
}

bool test_partial_overflow_horizontal_pane_clamps_to_frame_bottom()
{
    plot::frame_layout_result_t layout;
    layout.usable_width  = 840.0;
    layout.usable_height = 486.0;
    layout.v_bar_width   = 60.0;

    plot::frame_context_t ctx{layout};
    ctx.win_w                = 900;
    ctx.win_h                = 500;
    ctx.base_label_height_px = 34.0;

    glm::vec4 horizontal;
    TEST_ASSERT(plot::detail::horizontal_axis_label_pane_rect(ctx, horizontal),
        "partial-overflow horizontal axis label pane must be present");
    TEST_ASSERT(nearly_equal(horizontal.y, static_cast<float>(layout.usable_height)),
        "partial-overflow horizontal pane top must keep the usable height");
    TEST_ASSERT(nearly_equal(horizontal.w, static_cast<float>(ctx.win_h)),
        "partial-overflow horizontal pane bottom must clamp to the framebuffer bottom");
    TEST_ASSERT(horizontal.w > horizontal.y && horizontal.w - horizontal.y < ctx.base_label_height_px,
        "partial-overflow horizontal pane must remain valid but smaller than the full label lane");

    plot::text_scissor_t scissor;
    TEST_ASSERT(plot::detail::framebuffer_scissor_from_top_left_rect(
        horizontal, ctx.win_w, ctx.win_h, scissor),
        "partial-overflow horizontal pane must convert to an in-frame scissor");
    TEST_ASSERT(scissor.x == 0 && scissor.y == 0 && scissor.width == ctx.win_w,
        "partial-overflow horizontal pane scissor must span the frame bottom edge");
    TEST_ASSERT(scissor.height > 0 && scissor.height < ctx.base_label_height_px,
        "partial-overflow horizontal pane scissor must be smaller than the full label lane");
    TEST_ASSERT(scissor_inside_frame(scissor, ctx.win_w, ctx.win_h),
        "partial-overflow horizontal pane scissor must stay inside the framebuffer");
    return true;
}

bool test_partial_overflow_vertical_pane_clamps_to_frame_right()
{
    plot::frame_layout_result_t layout;
    layout.usable_width  = 782.0;
    layout.usable_height = 360.0;
    layout.v_bar_width   = 64.0;

    plot::frame_context_t ctx{layout};
    ctx.win_w = 800;
    ctx.win_h = 480;

    glm::vec4 vertical;
    TEST_ASSERT(plot::detail::vertical_axis_label_pane_rect(ctx, vertical),
        "partial-overflow vertical axis label pane must be present");
    TEST_ASSERT(nearly_equal(vertical.x, static_cast<float>(layout.usable_width)),
        "partial-overflow vertical pane left must keep the usable width");
    TEST_ASSERT(nearly_equal(vertical.z, static_cast<float>(ctx.win_w)),
        "partial-overflow vertical pane right must clamp to the framebuffer right edge");
    TEST_ASSERT(vertical.z > vertical.x && vertical.z - vertical.x < layout.v_bar_width,
        "partial-overflow vertical pane must remain valid but smaller than the full label lane");

    plot::text_scissor_t scissor;
    TEST_ASSERT(plot::detail::framebuffer_scissor_from_top_left_rect(
        vertical, ctx.win_w, ctx.win_h, scissor),
        "partial-overflow vertical pane must convert to an in-frame scissor");
    TEST_ASSERT(scissor.x == static_cast<int>(layout.usable_width) &&
        scissor.y == ctx.win_h - static_cast<int>(layout.usable_height),
        "partial-overflow vertical pane scissor must keep the expected QRhi origin");
    TEST_ASSERT(scissor.width > 0 && scissor.width < layout.v_bar_width,
        "partial-overflow vertical pane scissor must be smaller than the full label lane");
    TEST_ASSERT(scissor.height == static_cast<int>(layout.usable_height),
        "partial-overflow vertical pane scissor height must match the usable plot height");
    TEST_ASSERT(scissor_inside_frame(scissor, ctx.win_w, ctx.win_h),
        "partial-overflow vertical pane scissor must stay inside the framebuffer");
    return true;
}

bool test_standard_vertical_pane_scissor_maps_to_qrhi_coordinates()
{
    plot::frame_layout_result_t layout;
    layout.usable_width  = 640.0;
    layout.usable_height = 360.0;
    layout.v_bar_width   = 64.0;

    plot::frame_context_t ctx{layout};
    ctx.win_w = 800;
    ctx.win_h = 480;

    glm::vec4 vertical;
    TEST_ASSERT(plot::detail::vertical_axis_label_pane_rect(ctx, vertical),
        "standard vertical axis label pane must be present");

    plot::text_scissor_t scissor;
    TEST_ASSERT(plot::detail::framebuffer_scissor_from_top_left_rect(
        vertical, ctx.win_w, ctx.win_h, scissor),
        "standard vertical axis label pane must convert to an in-frame scissor");
    TEST_ASSERT(scissor.y == 120 && scissor.height == 360,
        "standard vertical pane must map to the expected QRhi y and height");
    TEST_ASSERT(scissor_inside_frame(scissor, ctx.win_w, ctx.win_h),
        "standard vertical pane scissor must stay inside the framebuffer");
    return true;
}

bool test_fractional_rects_round_inward_without_exceeding_framebuffer()
{
    constexpr int width  = 100;
    constexpr int height = 80;
    const glm::vec4 rect(0.25f, 20.25f, 99.75f, 60.75f);

    plot::text_scissor_t scissor;
    TEST_ASSERT(plot::detail::framebuffer_scissor_from_top_left_rect(rect, width, height, scissor),
        "fractional rect with interior pixels must produce a scissor");
    TEST_ASSERT(scissor.x == 1,       "fractional left edge must ceil inward");
    TEST_ASSERT(scissor.y == 20,      "fractional bottom edge must floor before QRhi y conversion");
    TEST_ASSERT(scissor.width == 98,  "fractional width must be rounded inward");
    TEST_ASSERT(scissor.height == 39, "fractional height must be rounded inward");
    TEST_ASSERT(scissor_inside_frame(scissor, width, height),
        "fractional scissor must stay inside the framebuffer");
    return true;
}

bool test_invalid_inputs_fail_closed()
{
    const float nan      = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();

    plot::text_scissor_t scissor;
    scissor.enabled = true;
    TEST_ASSERT(!plot::detail::framebuffer_scissor_from_top_left_rect(
        glm::vec4(0.0f, nan, 40.0f, 60.0f), 100, 80, scissor),
        "NaN rect must fail closed");
    TEST_ASSERT(!scissor.enabled,
        "NaN rect failure must disable the scissor");

    scissor.enabled = true;
    TEST_ASSERT(!plot::detail::framebuffer_scissor_from_top_left_rect(
        glm::vec4(0.0f, 10.0f, infinity, 60.0f), 100, 80, scissor),
        "infinite rect must fail closed");
    TEST_ASSERT(!scissor.enabled,
        "infinite rect failure must disable the scissor");

    scissor.enabled = true;
    TEST_ASSERT(!plot::detail::framebuffer_scissor_from_top_left_rect(
        glm::vec4(0.0f, 10.0f, 40.0f, 60.0f), 0, 0, scissor),
        "zero-size window must fail closed");
    TEST_ASSERT(!scissor.enabled,
        "zero-size window failure must disable the scissor");

    plot::frame_layout_result_t zero_height_layout;
    zero_height_layout.usable_width  = 40.0;
    zero_height_layout.usable_height = 0.0;
    zero_height_layout.v_bar_width   = 20.0;

    plot::frame_context_t zero_height_ctx{zero_height_layout};
    zero_height_ctx.win_w = 100;
    zero_height_ctx.win_h = 80;

    glm::vec4 rect;
    TEST_ASSERT(!plot::detail::vertical_axis_label_pane_rect(zero_height_ctx, rect),
        "vertical pane with no usable height must fail closed");
    return true;
}

bool test_degenerate_and_offscreen_panes_fail_closed()
{
    plot::text_scissor_t scissor;
    TEST_ASSERT(!plot::detail::framebuffer_scissor_from_top_left_rect(
        glm::vec4(10.2f, 10.0f, 10.8f, 30.0f), 100, 80, scissor),
        "subpixel-width rect with no covered integer pixels must fail closed");
    TEST_ASSERT(!scissor.enabled,
        "failed degenerate scissor must be disabled");

    plot::frame_layout_result_t offscreen_vertical_layout;
    offscreen_vertical_layout.usable_width  = 120.0;
    offscreen_vertical_layout.usable_height = 50.0;
    offscreen_vertical_layout.v_bar_width   = 10.0;

    plot::frame_context_t offscreen_vertical_ctx{offscreen_vertical_layout};
    offscreen_vertical_ctx.win_w = 100;
    offscreen_vertical_ctx.win_h = 80;

    glm::vec4 rect;
    TEST_ASSERT(!plot::detail::vertical_axis_label_pane_rect(offscreen_vertical_ctx, rect),
        "fully offscreen vertical pane must fail closed");

    plot::frame_layout_result_t offscreen_horizontal_layout;
    offscreen_horizontal_layout.usable_width  = 80.0;
    offscreen_horizontal_layout.usable_height = 105.0;
    offscreen_horizontal_layout.v_bar_width   = 20.0;

    plot::frame_context_t offscreen_horizontal_ctx{offscreen_horizontal_layout};
    offscreen_horizontal_ctx.win_w                = 100;
    offscreen_horizontal_ctx.win_h                = 100;
    offscreen_horizontal_ctx.base_label_height_px = 10.0;

    TEST_ASSERT(!plot::detail::horizontal_axis_label_pane_rect(offscreen_horizontal_ctx, rect),
        "fully offscreen horizontal pane must fail closed");
    return true;
}

bool test_vertical_pane_rect_uses_usable_width_lane()
{
    plot::frame_layout_result_t layout;
    layout.usable_width  = 123.0;
    layout.usable_height = 456.0;
    layout.v_bar_width   = 78.0;

    plot::frame_context_t ctx{layout};
    ctx.win_w = 300;
    ctx.win_h = 500;

    glm::vec4 vertical;
    TEST_ASSERT(plot::detail::vertical_axis_label_pane_rect(ctx, vertical),
        "vertical axis label pane must be present");
    TEST_ASSERT(rect_equal(vertical, glm::vec4(123.0f, 0.0f, 201.0f, 456.0f)),
        "vertical pane must use [usable_width,0] to [usable_width+v_bar_width,usable_height]");
    return true;
}

} // namespace

int main()
{
    std::cout << "Label pane geometry tests" << std::endl;

    int passed = 0;
    int failed = 0;

    RUN_TEST(test_standard_pane_rects_match_chrome_lanes);
    RUN_TEST(test_no_preview_horizontal_pane_scissor_reaches_frame_bottom);
    RUN_TEST(test_partial_overflow_horizontal_pane_clamps_to_frame_bottom);
    RUN_TEST(test_partial_overflow_vertical_pane_clamps_to_frame_right);
    RUN_TEST(test_standard_vertical_pane_scissor_maps_to_qrhi_coordinates);
    RUN_TEST(test_fractional_rects_round_inward_without_exceeding_framebuffer);
    RUN_TEST(test_invalid_inputs_fail_closed);
    RUN_TEST(test_degenerate_and_offscreen_panes_fail_closed);
    RUN_TEST(test_vertical_pane_rect_uses_usable_width_lane);

    std::cout << "Results: " << passed << " passed, " << failed << " failed" << std::endl;
    return failed > 0 ? 1 : 0;
}
