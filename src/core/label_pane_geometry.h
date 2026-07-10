#pragma once

#include <vnm_plot/rhi/font_renderer.h>
#include <vnm_plot/rhi/frame_context.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>

namespace vnm::plot::detail {

[[nodiscard]] inline bool horizontal_axis_label_pane_rect(
    const frame_context_t& ctx,
    glm::vec4&             rect)
{
    const double frame_right  = static_cast<double>(ctx.win_w);
    const double frame_bottom = static_cast<double>(ctx.win_h);
    if (!(frame_right > 0.0) || !(frame_bottom > 0.0)) {
        return false;
    }

    const double backing_top = std::clamp(
        ctx.layout.usable_height,
        0.0,
        frame_bottom);
    const double backing_bottom = std::clamp(
        ctx.layout.usable_height + ctx.base_label_height_px,
        0.0,
        frame_bottom);
    if (!(backing_bottom > backing_top)) {
        return false;
    }

    rect = glm::vec4(
        0.0f,
        static_cast<float>(backing_top),
        static_cast<float>(frame_right),
        static_cast<float>(backing_bottom));
    return true;
}

[[nodiscard]] inline bool vertical_axis_label_pane_rect(
    const frame_context_t& ctx,
    glm::vec4&             rect)
{
    const double frame_right  = static_cast<double>(ctx.win_w);
    const double frame_bottom = static_cast<double>(ctx.win_h);
    if (!(frame_right > 0.0) || !(frame_bottom > 0.0)) {
        return false;
    }

    const double backing_left = std::clamp(
        ctx.layout.usable_width,
        0.0,
        frame_right);
    const double backing_right = std::clamp(
        ctx.layout.usable_width + ctx.layout.v_bar_width,
        0.0,
        frame_right);
    const double backing_bottom = std::clamp(
        ctx.layout.usable_height,
        0.0,
        frame_bottom);
    if (!(backing_right > backing_left) || !(backing_bottom > 0.0)) {
        return false;
    }

    rect = glm::vec4(
        static_cast<float>(backing_left),
        0.0f,
        static_cast<float>(backing_right),
        static_cast<float>(backing_bottom));
    return true;
}

[[nodiscard]] inline bool framebuffer_scissor_from_top_left_rect(
    const glm::vec4&   rect,
    int                window_width,
    int                window_height,
    text_scissor_t&    scissor)
{
    if (window_width <= 0 || window_height <= 0 ||
        !std::isfinite(rect.x) ||
        !std::isfinite(rect.y) ||
        !std::isfinite(rect.z) ||
        !std::isfinite(rect.w))
    {
        scissor = {};
        return false;
    }

    const double clipped_left = std::clamp(
        static_cast<double>(rect.x),
        0.0,
        static_cast<double>(window_width));
    const double clipped_top = std::clamp(
        static_cast<double>(rect.y),
        0.0,
        static_cast<double>(window_height));
    const double clipped_right = std::clamp(
        static_cast<double>(rect.z),
        0.0,
        static_cast<double>(window_width));
    const double clipped_bottom = std::clamp(
        static_cast<double>(rect.w),
        0.0,
        static_cast<double>(window_height));
    if (!(clipped_right > clipped_left) || !(clipped_bottom > clipped_top)) {
        scissor = {};
        return false;
    }

    const int left = std::clamp(
        static_cast<int>(std::ceil(clipped_left)),
        0,
        window_width);
    const int top = std::clamp(
        static_cast<int>(std::ceil(clipped_top)),
        0,
        window_height);
    const int right = std::clamp(
        static_cast<int>(std::floor(clipped_right)),
        0,
        window_width);
    const int bottom = std::clamp(
        static_cast<int>(std::floor(clipped_bottom)),
        0,
        window_height);
    if (right <= left || bottom <= top) {
        scissor = {};
        return false;
    }

    scissor.enabled = true;
    scissor.x       = left;
    scissor.y       = window_height - bottom;
    scissor.width   = right - left;
    scissor.height  = bottom - top;

    const bool contained =
        scissor.x                  >= 0            &&
        scissor.y                  >= 0            &&
        scissor.width              >  0            &&
        scissor.height             >  0            &&
        scissor.x + scissor.width  <= window_width &&
        scissor.y + scissor.height <= window_height;
    if (!contained) {
        scissor = {};
        return false;
    }
    return true;
}

} // namespace vnm::plot::detail
