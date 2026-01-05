// VNM Plot Library - Core Text Renderer Stub
// No-op implementation for builds without MSDF text rendering.
// Maintains stable layout behavior without actual text rendering.

#include <vnm_plot/core/text_renderer.h>
#include <vnm_plot/core/font_renderer.h>

namespace vnm::plot {

Text_renderer::Text_renderer(Font_renderer* fr)
    : m_fonts(fr)
{
}

bool Text_renderer::render(const frame_context_t& /*ctx*/, bool /*fade_v_labels*/, bool /*fade_h_labels*/)
{
    // No-op in stub - return false to indicate no animations in progress
    return false;
}

bool Text_renderer::render_axis_labels(const frame_context_t& /*ctx*/, bool /*fade_labels*/)
{
    // No-op in stub
    return false;
}

bool Text_renderer::render_info_overlay(const frame_context_t& /*ctx*/, bool /*fade_labels*/)
{
    // No-op in stub
    return false;
}

} // namespace vnm::plot
