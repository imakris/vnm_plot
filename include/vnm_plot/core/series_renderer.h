#pragma once

// VNM Plot Library - Core Series Renderer
// Qt-free series data rendering with LOD support.

#include "gl_program.h"
#include "types.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vnm::plot {

class Asset_loader;

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

    // Initialize with asset loader for shader loading
    void initialize(Asset_loader& asset_loader);

    // Clean up GL resources
    void cleanup_gl_resources();

    // Render all series in the frame context
    void render(const frame_context_t& ctx,
                const std::map<int, std::shared_ptr<series_data_t>>& series);

private:
    struct metrics_t
    {
        std::atomic<uint64_t> bytes_uploaded{0};
        std::atomic<uint64_t> bytes_allocated{0};
        std::atomic<uint64_t> vbo_reallocations{0};
        std::atomic<uint64_t> snapshot_failures{0};
    };

    struct vbo_view_state_t
    {
        GLuint id = UINT_MAX;
        GLuint active_vbo = UINT_MAX;
        std::size_t last_ring_size = 0;
        std::size_t last_snapshot_elements = 0;
        uint64_t last_sequence = 0;
        const void* cached_data_identity = nullptr;

        GLuint adjacency_ebo = UINT_MAX;
        std::size_t adjacency_ebo_capacity = 0;
        GLint adjacency_last_first = 0;
        GLsizei adjacency_last_count = 0;

        GLint last_first = 0;
        GLsizei last_count = 0;
        std::size_t last_lod_level = 0;
        double last_t_min = std::numeric_limits<double>::quiet_NaN();
        double last_t_max = std::numeric_limits<double>::quiet_NaN();
        double last_width_px = std::numeric_limits<double>::quiet_NaN();
        double last_applied_pps = 0.0;
        bool last_use_t_override = false;
        double last_t_min_override = 0.0;
        double last_t_max_override = 0.0;

        void reset() { *this = vbo_view_state_t{}; }
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
        bool use_t_override = false;
        double t_min_override = 0.0;
        double t_max_override = 0.0;
        data_snapshot_t cached_snapshot;              // Reused in draw_pass for aux metric
        std::shared_ptr<void> cached_snapshot_hold;   // Keep snapshot alive
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
        struct entry_t
        {
            GLuint vao = 0;
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

    metrics_t m_metrics;
    uint64_t m_frame_id = 0;  // Monotonic frame counter for snapshot caching

    std::shared_ptr<GL_program> get_or_load_shader(
        const shader_set_t& shader_set,
        const Render_config* config);
    series_pipe_t& pipe_for(Display_style style);
    GLuint ensure_series_vao(Display_style style, GLuint vbo, const Data_access_policy& access);
    GLuint ensure_colormap_texture(const series_data_t& series, Display_style style);

    view_render_result_t process_view(
        vbo_view_state_t& view_state,
        vbo_state_t& shared_state,
        uint64_t frame_id,
        Data_source& data_source,
        const std::function<double(const void*)>& get_timestamp,
        const std::vector<std::size_t>& scales,
        double t_min,
        double t_max,
        double width_px,
        bool allow_stale_on_empty,
        vnm::plot::Profiler* profiler,
        bool skip_gl);

    void set_common_uniforms(GL_program& program, const glm::mat4& pmv, const frame_context_t& ctx);
    void modify_uniforms_for_preview(GL_program& program, const frame_context_t& ctx);
};

} // namespace vnm::plot
