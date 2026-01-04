// VNM Plot Library - Font Renderer Stub
// No-op implementation for builds without MSDF text rendering.
// Provides reasonable layout metrics so the rest of the pipeline behaves correctly.

#include <vnm_plot/renderers/font_renderer.h>

#include <cstring>

namespace vnm::plot {

struct Font_renderer::impl_t
{
    int pixel_height = 12;
    bool initialized = false;
};

Font_renderer::Font_renderer()
    : m_impl(std::make_unique<impl_t>())
{
}

Font_renderer::~Font_renderer() = default;

void Font_renderer::initialize(int pixel_height, bool /*force_rebuild*/)
{
    m_impl->pixel_height = pixel_height > 0 ? pixel_height : 12;
    m_impl->initialized = true;
}

bool Font_renderer::is_initialized() const
{
    return m_impl && m_impl->initialized;
}

void Font_renderer::deinitialize()
{
    if (m_impl) {
        m_impl->initialized = false;
    }
}

void Font_renderer::set_log_callbacks(
    std::function<void(const std::string&)> /*log_error*/,
    std::function<void(const std::string&)> /*log_debug*/)
{
    // No-op in stub
}

float Font_renderer::measure_text_px(const char* text) const
{
    if (!text || !m_impl) {
        return 0.0f;
    }
    // Approximate: 0.6 * font_height * string_length
    const float char_width = static_cast<float>(m_impl->pixel_height) * 0.6f;
    return char_width * static_cast<float>(std::strlen(text));
}

std::uint64_t Font_renderer::text_measure_cache_key() const
{
    return m_impl ? static_cast<std::uint64_t>(m_impl->pixel_height) : 0;
}

float Font_renderer::monospace_advance_px() const
{
    return m_impl ? static_cast<float>(m_impl->pixel_height) * 0.6f : 7.2f;
}

bool Font_renderer::monospace_advance_is_reliable() const
{
    return true;  // Stub is consistent
}

float Font_renderer::compute_numeric_bottom() const
{
    return m_impl ? static_cast<float>(m_impl->pixel_height) * 0.2f : 2.4f;
}

float Font_renderer::baseline_offset_px() const
{
    return m_impl ? static_cast<float>(m_impl->pixel_height) * 0.8f : 9.6f;
}

void Font_renderer::batch_text(float /*x*/, float /*y*/, const char* /*text*/)
{
    // No-op in stub
}

void Font_renderer::draw_and_flush(const glm::mat4& /*pmv*/, const glm::vec4& /*color*/)
{
    // No-op in stub
}

void Font_renderer::cleanup_thread_resources()
{
    // No-op in stub
}

void Font_renderer::shutdown_all_thread_resources()
{
    // No-op in stub
}

} // namespace vnm::plot
