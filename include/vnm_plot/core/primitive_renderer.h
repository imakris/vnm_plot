#pragma once

// VNM Plot Library - Core Primitive Renderer
// Qt-free renderer for basic primitives: rectangles and grid lines.

#include "gl_program.h"
#include "types.h"

#include <glm/glm.hpp>

#include <memory>
#include <vector>

class QRhi;
class QRhiBuffer;
class QRhiCommandBuffer;
class QRhiGraphicsPipeline;
class QRhiResourceUpdateBatch;
class QRhiShaderResourceBindings;

namespace vnm::plot {
class Profiler;  // Forward declaration
}

namespace vnm::plot {

class Asset_loader;

// -----------------------------------------------------------------------------
// Primitive_renderer
// -----------------------------------------------------------------------------
// Renders rectangles (batched) and grid lines (shader-based).
// This is the Qt-free core implementation. When the active frame_context_t
// carries a QRhi*, batch_rect / flush_rects / draw_grid_shader produce
// deferred RHI draw operations recorded to ctx.rhi_updates and replayed inside
// the open render pass via record_draws(). Without a QRhi the same calls
// drive the raw GL pipeline directly.
class Primitive_renderer
{
public:
    Primitive_renderer();
    ~Primitive_renderer();

    // Non-copyable (owns GPU resources)
    Primitive_renderer(const Primitive_renderer&) = delete;
    Primitive_renderer& operator=(const Primitive_renderer&) = delete;

    // Initialize GL resources
    // asset_loader: Provider for shader source code
    bool initialize(Asset_loader& asset_loader);

    // Clean up GL resources
    void cleanup_gl_resources();

    // Set profiler for performance tracking
    void set_profiler(vnm::plot::Profiler* profiler) { m_profiler = profiler; }
    void set_log_callback(GL_program::Log_callback callback);

    // --- Rect Pipeline ---
    // Batch a rectangle for drawing
    void batch_rect(const glm::vec4& color, const glm::vec4& rect_coords);

    // Upload and draw all batched rectangles. Under RHI this enqueues an
    // upload + draw op into the per-frame plan and clears the CPU batch; the
    // record_draws() call run inside the open pass dispatches the draw.
    // Under the GL fallback the upload + draw runs synchronously.
    void flush_rects(const frame_context_t& ctx, const glm::mat4& pmv);

    // Clear the rect batch without drawing (for skip_gl mode)
    void clear_rect_batch();

    // --- Grid Pipeline ---
    // Draw grid lines using shader. Under RHI the per-call UBO is filled and
    // queued onto ctx.rhi_updates; the actual draw is recorded by
    // record_draws() during the open pass.
    void draw_grid_shader(
        const frame_context_t& ctx,
        const glm::vec2& origin,
        const glm::vec2& size,
        const glm::vec4& color,
        const grid_layer_params_t& vertical_levels,
        const grid_layer_params_t& horizontal_levels);

    // Snapshot of how many ops have been queued so far this frame. The host
    // captures this between chrome phases and passes it to record_draws() to
    // play back exactly that slice. Always 0 under the GL path since draws
    // are emitted inline.
    std::size_t queued_op_count() const;

    // Replay queued rect / grid ops up to (but not including) op index `end`.
    // The internal cursor advances to `end`, so two record_draws() calls
    // with monotonically increasing `end` values play back disjoint slices
    // of the queue in order. Pass queued_op_count() to drain everything
    // queued so far. Called inside the host's open render pass on the RHI
    // path; no-op when no RHI is bound (the GL path already drew during the
    // queueing calls).
    void record_draws(const frame_context_t& ctx, std::size_t end);

    // Drop the per-frame draw plan and reset the playback cursor. Called by
    // the host after the last record_draws() of the frame to ready the
    // primitive renderer for the next frame's prepare phase.
    void reset_frame();

private:
    struct rect_vertex_t
    {
        glm::vec4 color;
        glm::vec4 rect_coords;  // x0, y0, x1, y1
    };

    struct gl_pipe_t
    {
        unsigned int vao            = 0;
        unsigned int vbo            = 0;
        size_t       capacity_bytes = 0;
    };

    gl_pipe_t m_rects_pipe;
    gl_pipe_t m_grid_quad_pipe;

    std::unique_ptr<GL_program> m_sp_rects;
    std::unique_ptr<GL_program> m_sp_grid;

    std::vector<rect_vertex_t> m_cpu_buffer;
    bool                       m_initialized = false;
    vnm::plot::Profiler*       m_profiler    = nullptr;
    GL_program::Log_callback    m_log_error;

    // RHI-side state. Pipelines, UBO ring buffer, vertex buffer for rects,
    // a static unit-quad VBO for grid, and the per-frame draw-op plan. Lives
    // out-of-line in primitive_renderer.cpp so the public header stays free
    // of QRhi includes.
    struct rhi_state_t;
    std::unique_ptr<rhi_state_t> m_rhi_state;

    // RHI pipeline and resource builders. Defined out-of-line in the .cpp so
    // they can touch QRhi types without dragging them into the public header.
    // Static because they only take the rhi_state, the QRhi handle, and the
    // render-target descriptor; they don't read other Primitive_renderer
    // state. Friendship via membership lets the helpers reach private types
    // (rect_vertex_t, rhi_state_t) without exposing them publicly.
    static bool rhi_ensure_rect_pipeline(
        rhi_state_t& rhi_state, QRhi* rhi, QRhiRenderTarget* rt);
    static bool rhi_ensure_grid_pipeline(
        rhi_state_t& rhi_state, QRhi* rhi, QRhiRenderTarget* rt);
    static bool rhi_ensure_grid_quad_vbo(
        rhi_state_t& rhi_state, QRhi* rhi, QRhiResourceUpdateBatch* updates);
    static void rhi_on_backend_change(rhi_state_t& rhi_state, QRhi* rhi);
    static void rhi_reset_frame_plan(rhi_state_t& rhi_state);

    static constexpr int k_rect_initial_quads = 256;
};

} // namespace vnm::plot
