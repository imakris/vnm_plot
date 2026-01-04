#pragma once

// VNM Plot Library - Core Primitive Renderer
// Qt-free renderer for basic primitives: rectangles and grid lines.

#include "gl_program.h"
#include "layout_types.h"

#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace vnm::plot {
class Profiler;  // Forward declaration
}

namespace vnm::plot::core {

class Asset_loader;

// -----------------------------------------------------------------------------
// Primitive_renderer
// -----------------------------------------------------------------------------
// Renders rectangles (batched) and grid lines (shader-based).
// This is the Qt-free core implementation.
class Primitive_renderer
{
public:
    Primitive_renderer();
    ~Primitive_renderer();

    // Non-copyable (owns GL resources)
    Primitive_renderer(const Primitive_renderer&) = delete;
    Primitive_renderer& operator=(const Primitive_renderer&) = delete;

    // Initialize GL resources
    // asset_loader: Provider for shader source code
    bool initialize(Asset_loader& asset_loader);

    // Clean up GL resources
    void cleanup_gl_resources();

    // Set profiler for performance tracking
    void set_profiler(vnm::plot::Profiler* profiler) { m_profiler = profiler; }
    void set_log_callback(GL_program::LogCallback callback);

    // --- Rect Pipeline ---
    // Batch a rectangle for drawing
    void batch_rect(const glm::vec4& color, const glm::vec4& rect_coords);

    // Upload and draw all batched rectangles
    void flush_rects(const glm::mat4& pmv);

    // --- Grid Pipeline ---
    // Draw grid lines using shader
    void draw_grid_shader(
        const glm::vec2& origin,
        const glm::vec2& size,
        const glm::vec4& color,
        const grid_layer_params_t& vertical_levels,
        const grid_layer_params_t& horizontal_levels);

private:
    struct rect_vertex_t
    {
        glm::vec4 color;
        glm::vec4 rect_coords;  // x0, y0, x1, y1
    };

    struct pipe_t
    {
        unsigned int vao            = 0;
        unsigned int vbo            = 0;
        size_t       capacity_bytes = 0;
    };

    pipe_t m_rects_pipe;
    pipe_t m_grid_quad_pipe;

    std::unique_ptr<GL_program> m_sp_rects;
    std::unique_ptr<GL_program> m_sp_grid;

    std::vector<rect_vertex_t> m_cpu_buffer;
    bool                       m_initialized = false;
    vnm::plot::Profiler*       m_profiler    = nullptr;
    GL_program::LogCallback    m_log_error;

    static constexpr int k_rect_initial_quads = 256;
};

} // namespace vnm::plot::core
