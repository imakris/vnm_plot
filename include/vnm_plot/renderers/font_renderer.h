#pragma once

// VNM Plot Library - Font Renderer
// Encapsulates font loading, text measurement, and MSDF-based rendering.

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

class QByteArray;

namespace vnm::plot {

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

    // Non-copyable and non-movable due to GL resource ownership.
    Font_renderer(const Font_renderer&) = delete;
    Font_renderer& operator=(const Font_renderer&) = delete;
    Font_renderer(Font_renderer&&) = delete;
    Font_renderer& operator=(Font_renderer&&) = delete;

    // Initializes the font system and ensures the thread-local resources are ready.
    // Must be called on a thread with an active OpenGL context before use.
    // force_rebuild recreates GL resources even if the pixel height matches.
    void initialize(int pixel_height, bool force_rebuild = false);

    // Returns true when the renderer is currently bound to a thread-local resource set.
    bool is_initialized() const;

    // Releases this instance's weak reference to the shared resources.
    void deinitialize();

    void set_log_callbacks(
        std::function<void(const std::string&)> log_error,
        std::function<void(const std::string&)> log_debug);

    // --- Text Measurement ---

    // Measures the horizontal pixel width of a given string.
    float measure_text_px(const char* text) const;

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

    // Renders all text currently in the batch buffer to the screen and clears the buffer.
    void draw_and_flush(const glm::mat4& pmv, const glm::vec4& color);

    // --- Resource Management ---

    // Static method to be called by the main renderer during its cleanup phase
    // to ensure thread-local GL resources are freed on the correct thread
    // with an active context.
    static void cleanup_thread_resources();

    // Call once during application shutdown (after all render threads are joined)
    // to release any remaining thread-local font resources for leak-free shutdown.
    static void shutdown_all_thread_resources();

private:
    struct impl_t;
    std::unique_ptr<impl_t> m_impl;
};

} // namespace vnm::plot
