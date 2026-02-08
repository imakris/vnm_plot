// VNM Plot Library - Core Font Renderer Stub
// No-op implementation for builds without MSDF text rendering.
// Provides stable symbols for core library when VNM_PLOT_ENABLE_TEXT is OFF.

#include <vnm_plot/core/font_renderer.h>

#include <atomic>

namespace vnm::plot {

namespace {

std::atomic<bool> s_disk_cache_enabled{true};

} // namespace

// Provide an empty definition so std::unique_ptr<impl_t> is complete in this TU.
struct Font_renderer::impl_t {};

// -----------------------------------------------------------------------------
// Font Cache Configuration
// -----------------------------------------------------------------------------

void set_font_disk_cache_enabled(bool enabled)
{
    s_disk_cache_enabled.store(enabled, std::memory_order_relaxed);
}

bool font_disk_cache_enabled()
{
    return s_disk_cache_enabled.load(std::memory_order_relaxed);
}

// -----------------------------------------------------------------------------
// Font Renderer (stub)
// -----------------------------------------------------------------------------

Font_renderer::Font_renderer() = default;
Font_renderer::~Font_renderer() = default;

void Font_renderer::initialize(Asset_loader&, int, bool)
{
    // No-op in stub.
}

void Font_renderer::deinitialize()
{
    // No-op in stub.
}

void Font_renderer::set_log_callbacks(
    std::function<void(const std::string&)>,
    std::function<void(const std::string&)>)
{
    // No-op in stub.
}

float Font_renderer::measure_text_px(const char*) const
{
    return 0.0f;
}

std::uint64_t Font_renderer::text_measure_cache_key() const
{
    return 0;
}

float Font_renderer::monospace_advance_px() const
{
    return 0.0f;
}

bool Font_renderer::monospace_advance_is_reliable() const
{
    return false;
}

float Font_renderer::compute_numeric_bottom() const
{
    return 0.0f;
}

float Font_renderer::baseline_offset_px() const
{
    return 0.0f;
}

void Font_renderer::batch_text(float, float, const char*)
{
    // No-op in stub.
}

void Font_renderer::draw_and_flush(const glm::mat4&, const glm::vec4&)
{
    // No-op in stub.
}

void Font_renderer::clear_buffer()
{
    // No-op in stub.
}

void Font_renderer::cleanup_thread_resources()
{
    // No-op in stub.
}

void Font_renderer::shutdown_all_thread_resources()
{
    // No-op in stub.
}

} // namespace vnm::plot
