#pragma once

// VNM Plot Library - Core Series Renderer
// Qt-free series data rendering with LOD support.

#include "gl_program.h"
#include "gpu_sample.h"
#include "types.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class QRhi;
class QRhiBuffer;
class QRhiCommandBuffer;
class QRhiGraphicsPipeline;
class QRhiResourceUpdateBatch;
class QRhiShaderResourceBindings;

namespace vnm::plot {

class Asset_loader;
class Profiler;

// -----------------------------------------------------------------------------
// Series Renderer
// -----------------------------------------------------------------------------
// Renders data series using OpenGL. Manages VBOs and shaders internally.
class Series_renderer
{
public:
    Series_renderer();
    ~Series_renderer();

    Series_renderer(const Series_renderer&) = delete;
    Series_renderer& operator=(const Series_renderer&) = delete;

    // Remember the asset loader used for lazy shader compilation. This does
    // not perform any GL work and cannot fail.
    void initialize(Asset_loader& asset_loader);

    // Clean up GL resources
    void cleanup_gl_resources();

    // Two-phase rendering. Under RHI, the host opens a resource-update batch,
    // calls prepare() to fill it with sample/UBO/per-frame uploads, calls
    // beginPass(rt, clear, depth, batch) to atomically submit those uploads
    // before any draw, and then calls render() inside the open pass to record
    // the draw commands. The split is forced by D3D11: cb->resourceUpdate is
    // only legal outside an open pass, and beginPass consumes its 4th
    // argument before the renderer's own draws would otherwise have written
    // to it. Under the GL fallback (no QRhi in ctx), prepare() is a no-op
    // and render() runs the full GL flow standalone, which keeps every
    // existing test that calls render() directly working.
    void prepare(const frame_context_t& ctx,
                 const std::map<int, std::shared_ptr<const series_data_t>>& series);

    // Render all series in the frame context. Records draws only when
    // ctx.rhi is non-null (uploads must already have been submitted via
    // prepare() + beginPass(batch)); runs the full GL flow when ctx.rhi is
    // null.
    void render(const frame_context_t& ctx,
                const std::map<int, std::shared_ptr<const series_data_t>>& series);

private:
    struct vbo_view_state_t
    {
        GLuint id = UINT_MAX;
        GLuint active_vbo = UINT_MAX;
        std::size_t last_ring_size = 0;
        std::size_t last_snapshot_elements = 0;
        uint64_t last_sequence = 0;
        const void* cached_data_identity = nullptr;
        uint64_t last_timestamp_order_sequence = 0;
        const void* last_timestamp_order_identity = nullptr;
        bool last_timestamps_monotonic = true;

        GLuint adjacency_ebo = UINT_MAX;
        std::size_t adjacency_ebo_capacity = 0;
        GLint adjacency_last_first = 0;
        GLsizei adjacency_last_count = 0;

        GLint last_first = 0;
        GLsizei last_count = 0;
        std::size_t last_lod_level = 0;
        // Timestamps are int64_t nanoseconds; sentinel SENTINEL_NONE means "no
        // valid value yet" so the first frame always invalidates cached state.
        static constexpr std::int64_t SENTINEL_NONE = std::numeric_limits<std::int64_t>::min();
        std::int64_t last_t_min = SENTINEL_NONE;
        std::int64_t last_t_max = SENTINEL_NONE;
        double last_width_px = std::numeric_limits<double>::quiet_NaN();
        Empty_window_behavior last_empty_window_behavior = Empty_window_behavior::DRAW_NOTHING;
        double last_applied_pps = 0.0;
        bool last_hold_last_forward = false;
        // Origin (ns) that produced the bytes currently in the VBO. Used to
        // invalidate the upload when the view's chosen origin moves to a new
        // snap bucket. SENTINEL_NONE forces the first frame to upload.
        std::int64_t uploaded_t_origin_ns = SENTINEL_NONE;
        // Renderer-owned scratch buffer for VBO uploads. Holds gpu_sample_t
        // values rebased against the active origin: the full snapshot followed
        // by an optional hold-last-forward synthetic sample. Reused across
        // uploads to avoid reallocation.
        std::vector<gpu_sample_t> staging;

        // Per-view RHI resources. Defined out-of-line in series_renderer.cpp
        // where QRhiBuffer is complete; the public header only sees the
        // forward declaration. unique_ptr-of-incomplete-type forces every
        // special member to be defined out-of-line, hence the explicit
        // declarations below.
        struct rhi_buffers_t;
        std::unique_ptr<rhi_buffers_t> rhi;

        std::size_t  rhi_vbo_capacity_bytes              = 0;
        std::size_t  rhi_line_window_vbo_capacity_bytes  = 0;

        vbo_view_state_t();
        ~vbo_view_state_t();
        vbo_view_state_t(const vbo_view_state_t&) = delete;
        vbo_view_state_t& operator=(const vbo_view_state_t&) = delete;
        vbo_view_state_t(vbo_view_state_t&&) noexcept;
        vbo_view_state_t& operator=(vbo_view_state_t&&) noexcept;

        void reset();
    };

    struct vbo_state_t
    {
        vbo_view_state_t main_view;
        vbo_view_state_t preview_view;
        struct aux_metric_cache_t
        {
            double min = 0.0;
            double max = 1.0;
            uint64_t sequence = 0;
            bool valid = false;
        };

        std::vector<aux_metric_cache_t> cached_aux_metric_levels;
        std::vector<aux_metric_cache_t> cached_aux_metric_levels_preview;
        const void* cached_aux_metric_identity = nullptr;
        const void* cached_aux_metric_identity_preview = nullptr;

        // Frame-scoped snapshot cache: shared between main_view and preview_view
        // to avoid redundant try_snapshot() calls within the same frame.
        uint64_t cached_snapshot_frame_id = 0;
        std::size_t cached_snapshot_level = SIZE_MAX;
        const Data_source* cached_snapshot_source = nullptr;
        data_snapshot_t cached_snapshot;
        std::shared_ptr<void> cached_snapshot_hold;
    };

    struct view_render_result_t
    {
        bool can_draw = false;
        GLint first = 0;
        GLsizei count = 0;
        std::size_t applied_level = 0;
        double applied_pps = 0.0;
        data_snapshot_t cached_snapshot;              // Reused in draw_pass for aux metric
        std::shared_ptr<void> cached_snapshot_hold;   // Keep snapshot alive
    };

    // Per-(series, view) draw plan computed in prepare() and consumed in
    // render(). Holds the raw policy/source pointers and the LOD-resolved
    // view results so the record-draws phase can replay decisions without
    // re-running process_view. Pointer fields stay valid because the host
    // passes the same series_map snapshot to both prepare() and render(),
    // and the renderer's own state (vbo_state) is owned by m_vbo_states.
    struct series_draw_state_t
    {
        int id = 0;
        std::shared_ptr<const series_data_t> series;
        Data_source* main_source = nullptr;
        Data_source* preview_source = nullptr;
        const Data_access_policy* main_access = nullptr;
        const Data_access_policy* preview_access = nullptr;
        Display_style main_style = static_cast<Display_style>(0);
        Display_style preview_style = static_cast<Display_style>(0);
        vbo_state_t* vbo_state = nullptr;
        std::vector<std::size_t> main_scales;
        std::vector<std::size_t> preview_scales;
        view_render_result_t main_result;
        view_render_result_t preview_result;
        bool has_preview = false;
        bool preview_matches_main = false;
    };

    struct colormap_resource_t
    {
        unsigned int texture = 0;
        std::size_t size = 0;
        uint64_t revision = 0;
    };

    struct colormap_resource_set_t
    {
        colormap_resource_t area;
        colormap_resource_t line;
    };

    struct series_pipe_t
    {
        // VBO is the GL-fallback vertex-buffer cache (per-pipe + per-layout).
        // VAOs moved out of entry_t into m_gl_vaos: GL state objects need to
        // outlive a single bind/draw cycle, but they are not part of the
        // pipe's per-layout vertex-buffer accounting.
        struct entry_t
        {
            GLuint vbo = 0;
        };
        std::unordered_map<std::uint64_t, entry_t> by_layout;
    };

    Asset_loader* m_asset_loader = nullptr;
    std::map<shader_set_t, std::shared_ptr<GL_program>> m_shaders;
    std::unordered_map<int, vbo_state_t> m_vbo_states;
    std::unordered_map<const series_data_t*, colormap_resource_set_t> m_colormap_textures;
    // Consolidated once-per-series error log deduplication.
    // Key encodes (series_id, error_category) as uint64_t.
    std::unordered_set<uint64_t> m_logged_errors;

    std::unique_ptr<series_pipe_t> m_pipe_line;
    std::unique_ptr<series_pipe_t> m_pipe_dots;
    std::unique_ptr<series_pipe_t> m_pipe_area;
    std::unique_ptr<series_pipe_t> m_pipe_colormap;

    // GL-fallback VAOs keyed by (pipe pointer, vbo). Outlives any single
    // draw because GL state objects must persist between bind/draw cycles.
    // Empty under RHI rendering.
    struct gl_vao_key_t
    {
        const series_pipe_t* pipe;
        GLuint vbo;
        bool operator==(const gl_vao_key_t& other) const noexcept
        {
            return pipe == other.pipe && vbo == other.vbo;
        }
    };
    struct gl_vao_key_hash_t
    {
        std::size_t operator()(const gl_vao_key_t& k) const noexcept
        {
            return std::hash<const void*>{}(k.pipe) ^ (std::hash<GLuint>{}(k.vbo) << 1);
        }
    };
    std::unordered_map<gl_vao_key_t, GLuint, gl_vao_key_hash_t> m_gl_vaos;

    // RHI-side state. The renderer drives LINE and DOTS through this path;
    // AREA and COLORMAP_* keep using the GL fallback, which is gated by
    // skip_gl whenever a QRhi is bound. Pipelines are cached per
    // Display_style. The full implementation sits in series_renderer.cpp
    // where the QRhi types are complete.
    struct rhi_state_t;
    std::unique_ptr<rhi_state_t> m_rhi_state;

    uint64_t m_frame_id = 0;  // Monotonic frame counter for snapshot caching

    void clear_frame_snapshot_caches();

    std::shared_ptr<GL_program> get_or_load_shader(
        const shader_set_t& shader_set,
        const Plot_config* config);
    series_pipe_t& pipe_for(Display_style style);
    // GL-fallback VAO management. The GL path needs a non-zero VAO bound for
    // every draw on a core-profile context; the RHI path drives attribute
    // layout through QRhiVertexInputLayout instead.
    GLuint ensure_gl_series_vao(Display_style style, GLuint vbo);
    GLuint ensure_colormap_texture(const series_data_t& series, Display_style style);

    view_render_result_t process_view(
        vbo_view_state_t& view_state,
        vbo_state_t& shared_state,
        uint64_t frame_id,
        Data_source& data_source,
        const Data_access_policy& access,
        const std::vector<std::size_t>& scales,
        std::int64_t t_min_ns,
        std::int64_t t_max_ns,
        std::int64_t t_origin_ns,
        double width_px,
        Empty_window_behavior empty_window_behavior,
        vnm::plot::Profiler* profiler,
        bool skip_gl,
        QRhi* rhi,
        QRhiResourceUpdateBatch* rhi_updates);

    void set_common_uniforms(
        GL_program& program,
        const glm::mat4& pmv,
        const frame_context_t& ctx,
        std::int64_t origin_ns);
    void modify_uniforms_for_preview(
        GL_program& program,
        const frame_context_t& ctx,
        std::int64_t origin_ns);

    // RHI helpers for LINE / DOTS. AREA and COLORMAP_* never reach these
    // paths (they fall to the GL fallback, which is gated by skip_gl).
    //
    // rhi_prepare_line_or_dots: writes to ctx.rhi_updates only. Builds the
    //   per-view UBO and (LINE-only) the per-frame line_window_vbo, and
    //   ensures the cached pipeline / SRB are valid. No cb->* draw calls.
    //   Must run before the host calls beginPass(batch) so the upload is
    //   submitted alongside the rest of ctx.rhi_updates.
    //
    // rhi_record_line_or_dots: emits cb->setGraphicsPipeline /
    //   setShaderResources / setVertexInput / setScissor / draw only. No
    //   buffer writes; safe to call inside the open render pass.
    void rhi_prepare_line_or_dots(
        const frame_context_t& ctx,
        const series_data_t* series,
        Display_style primitive_style,
        vbo_view_state_t& view_state,
        const view_render_result_t& view_result,
        bool is_preview,
        float line_width_px,
        float point_diameter_px,
        std::int64_t origin_ns);
    void rhi_record_line_or_dots(
        const frame_context_t& ctx,
        Display_style primitive_style,
        vbo_view_state_t& view_state,
        const view_render_result_t& view_result,
        bool is_preview);
};

} // namespace vnm::plot
