#pragma once

// VNM Plot Library - Primitive Renderer
// Renders basic primitives: rectangles and grid lines.

#include "../plot_config.h"
#include "../plot_types.h"

#include <glm/glm.hpp>

#include <array>
#include <memory>
#include <vector>

class QOpenGLShaderProgram;

namespace vnm::plot {

// -----------------------------------------------------------------------------
// Primitive Renderer
// -----------------------------------------------------------------------------
// Renders rectangles (batched) and grid lines (shader-based).
class Primitive_renderer
{
public:
    Primitive_renderer();
    ~Primitive_renderer();

    // Non-copyable (owns GL resources)
    Primitive_renderer(const Primitive_renderer&) = delete;
    Primitive_renderer& operator=(const Primitive_renderer&) = delete;

    // Initialize GL resources
    bool initialize();

    // Clean up GL resources
    void cleanup_gl_resources();

    // Set profiler for performance tracking
    void set_profiler(Profiler* profiler) { m_profiler = profiler; }
    void set_log_callbacks(const Plot_config* config);

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
    struct rect_vertex_t;

    struct pipe_t
    {
        unsigned int vao            = 0;
        unsigned int vbo            = 0;
        size_t       capacity_bytes = 0;
    };

    pipe_t m_rects_pipe;
    pipe_t m_grid_quad_pipe;

    std::unique_ptr<QOpenGLShaderProgram> m_sp_rects;
    std::unique_ptr<QOpenGLShaderProgram> m_sp_grid;

    std::vector<rect_vertex_t> m_cpu_buffer;
    bool                       m_initialized = false;
    Profiler*                  m_profiler    = nullptr;
    std::function<void(const std::string&)> m_log_error;

    static constexpr int k_rect_initial_quads = 256;
};

} // namespace vnm::plot
