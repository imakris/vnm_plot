#pragma once

// VNM Plot Library - Core Series Renderer
// Qt-free series data rendering with LOD support.

#include "data_types.h"
#include "render_types.h"

#include <glatter/glatter.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

namespace vnm::plot::core {

class Asset_loader;
class GL_program;

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

        GLint last_first = 0;
        GLsizei last_count = 0;
        std::size_t last_lod_level = 0;

        void reset()
        {
            id = UINT_MAX;
            active_vbo = UINT_MAX;
            last_ring_size = 0;
            last_snapshot_elements = 0;
            last_sequence = 0;
            cached_data_identity = nullptr;
            last_first = 0;
            last_count = 0;
            last_lod_level = 0;
        }
    };

    struct vbo_state_t
    {
        vbo_view_state_t main_view;
        vbo_view_state_t preview_view;

        const void* cached_data_identity = nullptr;
        double cached_aux_metric_min = 0.0;
        double cached_aux_metric_max = 1.0;
        uint64_t cached_aux_metric_sequence = 0;
        const void* cached_aux_metric_identity = nullptr;
        bool has_cached_aux_metric_range = false;
    };

    struct view_render_result_t
    {
        bool can_draw = false;
        GLint first = 0;
        GLsizei count = 0;
        std::size_t applied_level = 0;
        double applied_pps = 0.0;
    };

    struct colormap_resource_t
    {
        unsigned int texture = 0;
        std::size_t size = 0;
        uint64_t revision = 0;
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
    std::unordered_map<const series_data_t*, colormap_resource_t> m_colormap_textures;

    std::unique_ptr<series_pipe_t> m_pipe_line;
    std::unique_ptr<series_pipe_t> m_pipe_dots;
    std::unique_ptr<series_pipe_t> m_pipe_area;
    std::unique_ptr<series_pipe_t> m_pipe_colormap;

    metrics_t m_metrics;

    std::shared_ptr<GL_program> get_or_load_shader(
        const shader_set_t& shader_set,
        const Render_config* config);
    series_pipe_t& pipe_for(Display_style style);
    GLuint ensure_series_vao(Display_style style, GLuint vbo, const series_data_t& series);
    GLuint ensure_colormap_texture(const series_data_t& series);

    static std::size_t choose_level_from_base_pps(
        const std::vector<std::size_t>& scales,
        std::size_t current_level,
        double base_pps);

    view_render_result_t process_view(
        vbo_view_state_t& view_state,
        Data_source& data_source,
        const std::function<double(const void*)>& get_timestamp,
        const std::vector<std::size_t>& scales,
        double t_min,
        double t_max,
        double width_px);

    void set_common_uniforms(GLuint program, const glm::mat4& pmv, const frame_context_t& ctx);
    void modify_uniforms_for_preview(GLuint program, const frame_context_t& ctx);
};

} // namespace vnm::plot::core
