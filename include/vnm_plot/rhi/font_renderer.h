#pragma once

// VNM Plot Library - RHI Font Renderer
// MSDF text rendering with font loading, measurement, and QRhi rendering.

#include <vnm_plot/core/text_lcd.h>
#include <vnm_plot/rhi/frame_context.h>

#include <glm/glm.hpp>

#if defined(VNM_PLOT_ENABLE_TEXT) && defined(VNM_PLOT_ENABLE_TEST_HOOKS)
#include <array>
#include <filesystem>
#endif

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace vnm::plot {

class Asset_loader;

struct text_scissor_t
{
    bool enabled = false;
    int  x       = 0;
    int  y       = 0;
    int  width   = 0;
    int  height  = 0;
};

struct text_shadow_t
{
    glm::vec4 color     = glm::vec4(0.f);
    float     radius_px = 0.0f;
};

struct text_lcd_t
{
    text_lcd_resolved_subpixel_order_t subpixel_order =
        text_lcd_resolved_subpixel_order_t::NONE;
    glm::vec4 background_color = glm::vec4(0.f);
};

// -----------------------------------------------------------------------------
// Font Cache Configuration
// -----------------------------------------------------------------------------
// Controls whether MSDF font data is persisted to disk for faster startup.

// Enable or disable disk caching of MSDF font data (default: enabled).
// Call before Font_renderer::initialize_metrics() to take effect.
void set_font_disk_cache_enabled(bool enabled);

// Returns true if disk caching is enabled.
[[nodiscard]] bool font_disk_cache_enabled();

#if defined(VNM_PLOT_ENABLE_TEXT) && defined(VNM_PLOT_ENABLE_TEST_HOOKS)
namespace detail {

using font_disk_cache_digest_t = std::array<std::uint8_t, 32>;

[[nodiscard]] bool validate_font_disk_cache_file(
    const std::filesystem::path&       path,
    const font_disk_cache_digest_t&    expected_digest,
    int                                pixel_height);

} // namespace detail
#endif

// -----------------------------------------------------------------------------
// Font Renderer
// -----------------------------------------------------------------------------
// Renders text using multi-channel signed distance field (MSDF) technique.
// Thread-local resources are managed internally for multi-threaded rendering.
class Font_renderer
{
public:
    Font_renderer();
    ~Font_renderer();

    // Non-copyable and non-movable due to renderer resource ownership.
    Font_renderer(const Font_renderer&) = delete;
    Font_renderer& operator=(const Font_renderer&) = delete;
    Font_renderer(Font_renderer&&) = delete;
    Font_renderer& operator=(Font_renderer&&) = delete;

    // Initializes the font system and ensures text metrics are ready.
    // asset_loader: Provider for font and shader assets
    // force_rebuild regenerates cached metrics even if the pixel height matches.
    void initialize(Asset_loader& asset_loader, int pixel_height, bool force_rebuild = false);

    // Initializes CPU font metrics/cache for layout calculation before the render pass.
    void initialize_metrics(
        Asset_loader&          asset_loader,
        int                    pixel_height,
        bool                   force_rebuild = false);

    // Releases this instance's weak reference to the shared resources.
    void deinitialize();

    void set_log_callbacks(
        std::function<void(const std::string&)>    log_error,
        std::function<void(const std::string&)>    log_debug);

    // --- Text Measurement ---

    // Measures the horizontal pixel width of a given string.
    float measure_text_px(const char* text) const;

    // Returns the visual glyph quad bounds for text at the given baseline.
    bool text_visual_bounds_px(const char* text, float x, float y, glm::vec4& bounds) const;

    // Returns a key that changes when font metrics change (for caching).
    std::uint64_t text_measure_cache_key() const;

    // Returns the advance width of '0' in pixels (for monospace layout).
    float monospace_advance_px() const;

    // Returns true if the monospace advance is reliably measured.
    bool monospace_advance_is_reliable() const;

    // Computes a font-specific metric for aligning numeric labels vertically.
    float compute_numeric_bottom() const;

    // Returns the baseline offset in pixels.
    float baseline_offset_px() const;

    // --- Text Rendering ---

    // Adds a string to an internal vertex buffer to be drawn in a batch.
    void batch_text(float x, float y, const char* text);

    // Starts a QRhi text frame. Subsequent batch_text() calls append to the
    // QRhi CPU batch until rhi_record_frame() resets the frame state.
    void rhi_begin_frame();

    // Uploads the current QRhi CPU batch into this frame's draw plan and clears it.
    void rhi_queue_draw(
        const frame_context_t& ctx,
        const glm::mat4&       pmv,
        const glm::vec4&       color,
        const text_scissor_t&  scissor = {},
        const text_shadow_t&   shadow = {});

    void rhi_queue_draw(
        const frame_context_t& ctx,
        const glm::mat4&       pmv,
        const glm::vec4&       color,
        const text_scissor_t&  scissor,
        const text_shadow_t&   shadow,
        const text_lcd_t&      lcd);

    // Uploads the accumulated QRhi text geometry after all draw batches are queued.
    void rhi_finalize_frame(const frame_context_t& ctx);

    // Records all queued QRhi text draws. Must be called inside the render pass.
    void rhi_record_frame(const frame_context_t& ctx);

    // Clears QRhi per-frame CPU/draw state without touching persistent resources.
    void rhi_reset_frame();

    // Clears the batch buffer without rendering.
    void clear_buffer();

private:
    struct impl_t;
    std::unique_ptr<impl_t> m_impl;
};

} // namespace vnm::plot
