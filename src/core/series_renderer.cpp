#include <vnm_plot/core/series_renderer.h>
#include <vnm_plot/core/algo.h>
#include <vnm_plot/core/asset_loader.h>
#include <vnm_plot/core/color_palette.h>
#include <vnm_plot/core/gl_program.h>
#include <vnm_plot/core/constants.h>
#include <vnm_plot/core/plot_config.h>

#include <glatter/glatter.h>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <vector>

namespace vnm::plot {
using namespace detail;

namespace {

constexpr glm::vec4 k_default_series_color(0.16f, 0.45f, 0.64f, 1.0f);
constexpr glm::vec4 k_default_series_color_dark(0.30f, 0.63f, 0.88f, 1.0f);
constexpr float k_default_color_epsilon = 0.01f;

bool is_default_series_color(const glm::vec4& color)
{
    return glm::all(glm::lessThan(
        glm::abs(color - k_default_series_color),
        glm::vec4(k_default_color_epsilon)));
}

bool compute_aux_metric_range(
    const series_data_t& series,
    const data_snapshot_t& snapshot,
    double& out_min,
    double& out_max)
{
    if (!series.access.get_aux_metric || !snapshot || snapshot.count == 0 || snapshot.stride == 0) {
        return false;
    }

    double min_value = std::numeric_limits<double>::infinity();
    double max_value = -std::numeric_limits<double>::infinity();
    bool have_any = false;

    for (std::size_t i = 0; i < snapshot.count; ++i) {
        const void* sample = snapshot.at(i);
        if (!sample) {
            continue;
        }
        const double value = series.access.get_aux_metric(sample);
        if (!std::isfinite(value)) {
            continue;
        }
        min_value = std::min(min_value, value);
        max_value = std::max(max_value, value);
        have_any = true;
    }

    if (!have_any) {
        return false;
    }

    out_min = min_value;
    out_max = max_value;
    return true;
}

} // anonymous namespace

Series_renderer::Series_renderer()
{
    m_pipe_line = std::make_unique<series_pipe_t>();
    m_pipe_dots = std::make_unique<series_pipe_t>();
    m_pipe_area = std::make_unique<series_pipe_t>();
    m_pipe_colormap = std::make_unique<series_pipe_t>();
}

Series_renderer::~Series_renderer() = default;

void Series_renderer::initialize(Asset_loader& asset_loader)
{
    m_asset_loader = &asset_loader;
}

void Series_renderer::cleanup_gl_resources()
{
    for (auto* pipe : {m_pipe_line.get(), m_pipe_dots.get(), m_pipe_area.get(), m_pipe_colormap.get()}) {
        if (!pipe) {
            continue;
        }
        for (auto& [_, entry] : pipe->by_layout) {
            if (entry.vao != 0) {
                glDeleteVertexArrays(1, &entry.vao);
                entry.vao = 0;
            }
            entry.vbo = 0;
        }
        pipe->by_layout.clear();
    }

    for (auto& [_, state] : m_vbo_states) {
        for (auto* view : {&state.main_view, &state.preview_view}) {
            if (view->id != UINT_MAX) {
                glDeleteBuffers(1, &view->id);
            }
            if (view->adjacency_ebo != 0) {
                glDeleteBuffers(1, &view->adjacency_ebo);
            }
            view->reset();
        }
    }
    m_vbo_states.clear();

    m_shaders.clear();

    for (auto& [_, resource] : m_colormap_textures) {
        if (resource.texture != 0) {
            glDeleteTextures(1, &resource.texture);
            resource.texture = 0;
        }
    }
    m_colormap_textures.clear();
    m_missing_signal_logged.clear();
}

Series_renderer::series_pipe_t& Series_renderer::pipe_for(Display_style style)
{
    if (!!(style & Display_style::COLORMAP_AREA) || !!(style & Display_style::COLORMAP_LINE)) {
        return *m_pipe_colormap;
    }
    if (!!(style & Display_style::DOTS)) {
        return *m_pipe_dots;
    }
    if (!!(style & Display_style::AREA)) {
        return *m_pipe_area;
    }
    return *m_pipe_line;
}

GLuint Series_renderer::ensure_colormap_texture(const series_data_t& series)
{
    if (series.colormap.samples.empty()) {
        if (auto it = m_colormap_textures.find(&series); it != m_colormap_textures.end()) {
            if (it->second.texture != 0) {
                glDeleteTextures(1, &it->second.texture);
            }
            m_colormap_textures.erase(it);
        }
        return 0;
    }

    auto& resource = m_colormap_textures[&series];

    if (resource.texture == 0) {
        glGenTextures(1, &resource.texture);
        glBindTexture(GL_TEXTURE_1D, resource.texture);
        glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        resource.size = 0;
        resource.revision = 0;
    }
    else {
        glBindTexture(GL_TEXTURE_1D, resource.texture);
    }

    const std::size_t desired_size = series.colormap.samples.size();
    const uint64_t desired_revision = series.colormap.revision;
    const bool size_changed = (resource.size != desired_size);
    const bool revision_changed = (resource.revision != desired_revision);

    if (size_changed || revision_changed) {
        if (size_changed) {
            glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA32F, static_cast<GLsizei>(desired_size),
                         0, GL_RGBA, GL_FLOAT, series.colormap.samples.data());
        }
        else {
            glTexSubImage1D(GL_TEXTURE_1D, 0, 0, static_cast<GLsizei>(desired_size),
                            GL_RGBA, GL_FLOAT, series.colormap.samples.data());
        }
        resource.size = desired_size;
        resource.revision = desired_revision;
    }

    glBindTexture(GL_TEXTURE_1D, 0);
    return resource.texture;
}

GLuint Series_renderer::ensure_series_vao(
    Display_style style,
    GLuint vbo,
    const series_data_t& series)
{
    auto& pipe = pipe_for(style);
    auto& entry = pipe.by_layout[series.access.layout_key];

    if (entry.vao != 0 && entry.vbo == vbo) {
        return entry.vao;
    }

    if (entry.vao == 0) {
        glGenVertexArrays(1, &entry.vao);
    }

    glBindVertexArray(entry.vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    if (series.access.setup_vertex_attributes) {
        series.access.setup_vertex_attributes();
    }
    entry.vbo = vbo;

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return entry.vao;
}

std::shared_ptr<GL_program> Series_renderer::get_or_load_shader(
    const shader_set_t& shader_set,
    const Render_config* config)
{
    Profiler* profiler = config ? config->profiler : nullptr;

    if (shader_set.vert.empty() || !m_asset_loader) {
        return nullptr;
    }

    // Cache lookup
    {
        VNM_PLOT_PROFILE_SCOPE(profiler, "cache_lookup");
        if (auto found = m_shaders.find(shader_set); found != m_shaders.end()) {
            return found->second;
        }
    }

    // Cache miss - load and compile shaders (expensive, but only happens once per shader set)
    VNM_PLOT_PROFILE_SCOPE(profiler, "cache_miss");

    auto vert_src = m_asset_loader->load(shader_set.vert);
    auto frag_src = m_asset_loader->load(shader_set.frag);
    std::optional<ByteBuffer> geom_src;
    if (!shader_set.geom.empty()) {
        geom_src = m_asset_loader->load(shader_set.geom);
    }

    if (!vert_src || !frag_src) {
        if (config && config->log_error) {
            config->log_error("Failed to load shader sources: " + shader_set.vert);
        }
        return nullptr;
    }

    std::string vert_str(vert_src->begin(), vert_src->end());
    std::string frag_str(frag_src->begin(), frag_src->end());
    std::string geom_str;
    if (geom_src) {
        geom_str.assign(geom_src->begin(), geom_src->end());
    }

    auto log_error = config ? config->log_error : std::function<void(const std::string&)>();
    auto sp = create_gl_program(vert_str, geom_str, frag_str, log_error);

    if (!sp) {
        return nullptr;
    }
    auto shared_sp = std::shared_ptr<GL_program>(std::move(sp));
    m_shaders.emplace(shader_set, shared_sp);
    return shared_sp;
}

Series_renderer::view_render_result_t Series_renderer::process_view(
    vbo_view_state_t& view_state,
    Data_source& data_source,
    const std::function<double(const void*)>& get_timestamp,
    const std::vector<std::size_t>& scales,
    double t_min,
    double t_max,
    double width_px,
    bool allow_stale_on_empty,
    vnm::plot::Profiler* profiler,
    bool skip_gl)
{
    view_render_result_t result;

    if (scales.empty() || t_max <= t_min || width_px <= 0.0) {
        return result;
    }

    const std::size_t level_count = scales.size();
    const std::size_t max_level_index = level_count > 0 ? level_count - 1 : 0;
    std::size_t target_level = std::min<std::size_t>(view_state.last_lod_level, max_level_index);

    constexpr std::size_t k_tried_stack_levels = 32;
    std::array<uint8_t, k_tried_stack_levels> tried_stack{};
    std::vector<uint8_t> tried_heap;
    uint8_t* tried = nullptr;
    if (level_count <= k_tried_stack_levels) {
        tried = tried_stack.data();
        std::fill(tried, tried + level_count, uint8_t{0});
    }
    else {
        tried_heap.assign(level_count, uint8_t{0});
        tried = tried_heap.data();
    }
    const auto was_tried = [&](std::size_t level) -> bool {
        return tried && level < level_count && tried[level] != 0;
    };
    const auto mark_tried = [&](std::size_t level) {
        if (tried && level < level_count) {
            tried[level] = 1;
        }
    };

    const int max_attempts = static_cast<int>(level_count) + 2;

    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        const std::size_t applied_level = std::min<std::size_t>(target_level, max_level_index);
        if (was_tried(applied_level)) {
            break;
        }
        mark_tried(applied_level);
        const std::size_t applied_scale = scales[applied_level];

        vnm::plot::snapshot_result_t snapshot_result;
        {
            VNM_PLOT_PROFILE_SCOPE(
                profiler,
                "renderer.frame.execute_passes.render_data_series.series.process_view.try_snapshot");
            snapshot_result = data_source.try_snapshot(applied_level);
        }
        if (!snapshot_result) {
            ++m_metrics.snapshot_failures;

            result.can_draw = (view_state.active_vbo != UINT_MAX && view_state.last_count > 0);
            if (result.can_draw) {
                result.first = view_state.last_first;
                result.count = view_state.last_count;
                result.applied_level = view_state.last_lod_level;
            }
            if (!result.can_draw && applied_level > 0) {
                target_level = applied_level - 1;
                continue;
            }
            break;
        }

        const auto& snapshot = snapshot_result.snapshot;
        if (!snapshot || snapshot.count == 0) {
            ++m_metrics.snapshot_failures;
            result.can_draw = (view_state.active_vbo != UINT_MAX && view_state.last_count > 0);
            if (result.can_draw) {
                result.first = view_state.last_first;
                result.count = view_state.last_count;
                result.applied_level = view_state.last_lod_level;
            }
            if (!result.can_draw && applied_level > 0) {
                target_level = applied_level - 1;
                continue;
            }
            break;
        }

        // Find visible range using binary search
        std::size_t first_idx = 0;
        std::size_t last_idx = snapshot.count;
        double first_ts = 0.0;
        double last_ts = 0.0;
        bool have_ts_bounds = false;
        if (get_timestamp) {
            VNM_PLOT_PROFILE_SCOPE(
                profiler,
                "renderer.frame.execute_passes.render_data_series.series.process_view.binary_search");
            const void* first_sample = snapshot.at(0);
            const void* last_sample = snapshot.at(snapshot.count - 1);
            if (first_sample && last_sample) {
                first_ts = get_timestamp(first_sample);
                last_ts = get_timestamp(last_sample);
                have_ts_bounds = std::isfinite(first_ts) && std::isfinite(last_ts);
            }
            first_idx = lower_bound_timestamp(snapshot, get_timestamp, t_min);
            if (first_idx > 0) {
                --first_idx;
            }
            last_idx = upper_bound_timestamp(snapshot, get_timestamp, t_max);
            last_idx = std::min(last_idx + 2, snapshot.count);
        }

        if (allow_stale_on_empty && have_ts_bounds && last_ts > first_ts) {
            const bool out_of_range = (t_max <= first_ts) || (t_min >= last_ts);
            if (out_of_range) {
                first_idx = 0;
                last_idx = snapshot.count;
                result.use_t_override = true;
                result.t_min_override = first_ts;
                result.t_max_override = last_ts;
            }
        }

        if (allow_stale_on_empty && have_ts_bounds && last_ts > first_ts &&
            !result.use_t_override)
        {
            const bool covers_window = (first_ts <= t_min) && (last_ts >= t_max);
            if (!covers_window) {
                first_idx = 0;
                last_idx = snapshot.count;
                result.use_t_override = true;
                result.t_min_override = first_ts;
                result.t_max_override = last_ts;
            }
        }

        if (first_idx >= last_idx) {
            const bool can_override =
                allow_stale_on_empty && have_ts_bounds && last_ts > first_ts &&
                !result.use_t_override;
            if (can_override) {
                first_idx = 0;
                last_idx = snapshot.count;
                result.use_t_override = true;
                result.t_min_override = first_ts;
                result.t_max_override = last_ts;
            }
            else if (allow_stale_on_empty &&
                     view_state.active_vbo != UINT_MAX &&
                     view_state.last_count > 0) {
                result.can_draw = true;
                result.first = view_state.last_first;
                result.count = view_state.last_count;
                result.applied_level = view_state.last_lod_level;
                break;
            }
            else if (applied_level > 0 && !was_tried(applied_level - 1)) {
                target_level = applied_level - 1;
                continue;
            }
            else {
                break;
            }
        }

        const GLsizei count = static_cast<GLsizei>(last_idx - first_idx);
        const std::size_t base_samples = (count > 0)
            ? static_cast<std::size_t>(count) * applied_scale
            : 0;
        double base_pps = (base_samples > 0)
            ? width_px / static_cast<double>(base_samples)
            : 0.0;
        if (allow_stale_on_empty && applied_level != 0) {
            auto base_snapshot_result = data_source.try_snapshot(0);
            if (base_snapshot_result && base_snapshot_result.snapshot.count > 0) {
                const std::size_t base_scale = scales.empty() ? 1 : scales[0];
                const std::size_t base_samples_ref =
                    base_snapshot_result.snapshot.count * base_scale;
                if (base_samples_ref > 0) {
                    base_pps = width_px / static_cast<double>(base_samples_ref);
                }
            }
        }

        const std::size_t desired_level = choose_lod_level(scales, applied_level, base_pps);
        if (desired_level != applied_level) {
            if (!was_tried(desired_level)) {
                target_level = desired_level;
                continue;
            }
        }

        {
            VNM_PLOT_PROFILE_SCOPE(
                profiler,
                "renderer.frame.execute_passes.render_data_series.series.cpu_prepare");
            // Ensure VBO exists and has enough capacity (skip GL calls if skip_gl is set)
            if (!skip_gl) {
                VNM_PLOT_PROFILE_SCOPE(
                    profiler,
                    "renderer.frame.execute_passes.render_data_series.series.cpu_prepare.ensure_vbo");
                if (view_state.id == UINT_MAX) {
                    glGenBuffers(1, &view_state.id);
                }
            }

            std::size_t needed_bytes = 0;
            const void* current_identity = nullptr;
            bool region_changed = false;
            bool seq_changed = false;
            bool lod_level_changed = false;
            bool identity_changed = false;
            bool must_upload = false;
            {
                VNM_PLOT_PROFILE_SCOPE(
                    profiler,
                    "renderer.frame.execute_passes.render_data_series.series.cpu_prepare.state_check");
                needed_bytes = snapshot.count * snapshot.stride;
                current_identity = data_source.identity();

                region_changed = (view_state.last_ring_size < needed_bytes);
                seq_changed = (snapshot.sequence != view_state.last_sequence);
                lod_level_changed = (applied_level != view_state.last_lod_level);
                identity_changed = (current_identity != view_state.cached_data_identity);

                must_upload = region_changed || seq_changed || lod_level_changed || identity_changed;
            }
            if (must_upload && !skip_gl) {
                VNM_PLOT_PROFILE_SCOPE(
                    profiler,
                    "renderer.frame.execute_passes.render_data_series.series.cpu_prepare.vbo_manage");
                glBindBuffer(GL_ARRAY_BUFFER, view_state.id);

                if (region_changed) {
                    const std::size_t alloc_size = needed_bytes + needed_bytes / 4;
                    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(alloc_size), nullptr, GL_DYNAMIC_DRAW);
                    view_state.last_ring_size = alloc_size;
                    ++m_metrics.vbo_reallocations;
                    m_metrics.bytes_allocated += alloc_size;
                }

                if (!snapshot.is_segmented()) {
                    glBufferSubData(
                        GL_ARRAY_BUFFER,
                        0,
                        static_cast<GLsizeiptr>(snapshot.count * snapshot.stride),
                        snapshot.data);
                }
                else {
                    const std::size_t count1 = snapshot.count1();
                    const std::size_t bytes1 = count1 * snapshot.stride;
                    const std::size_t bytes2 = snapshot.count2 * snapshot.stride;
                    if (bytes1 > 0) {
                        glBufferSubData(
                            GL_ARRAY_BUFFER,
                            0,
                            static_cast<GLsizeiptr>(bytes1),
                            snapshot.data);
                    }
                    if (bytes2 > 0) {
                        glBufferSubData(
                            GL_ARRAY_BUFFER,
                            static_cast<GLintptr>(bytes1),
                            static_cast<GLsizeiptr>(bytes2),
                            snapshot.data2);
                    }
                }
                m_metrics.bytes_uploaded += snapshot.count * snapshot.stride;

                // Note: glBindBuffer(0) removed - unbinding is unnecessary and adds overhead.
                // The VAO bind in draw_pass will set the correct buffer binding.
            }

            if (must_upload) {
                VNM_PLOT_PROFILE_SCOPE(
                    profiler,
                    "renderer.frame.execute_passes.render_data_series.series.cpu_prepare.upload_state");
                view_state.last_sequence = snapshot.sequence;
                view_state.cached_data_identity = current_identity;
            }

            {
                VNM_PLOT_PROFILE_SCOPE(
                    profiler,
                    "renderer.frame.execute_passes.render_data_series.series.cpu_prepare.state_update");
                view_state.last_snapshot_elements = snapshot.count;
            }
        }

        view_state.active_vbo = view_state.id;
        view_state.last_first = static_cast<GLint>(first_idx);
        view_state.last_count = count;

        view_state.last_lod_level = applied_level;

        result.can_draw = true;
        result.first = view_state.last_first;
        result.count = view_state.last_count;
        result.applied_level = applied_level;
        result.applied_pps = base_pps * static_cast<double>(applied_scale);
        break;
    }

    return result;
}

void Series_renderer::set_common_uniforms(
    GL_program& program,
    const glm::mat4& pmv,
    const frame_context_t& ctx)
{
    glUniformMatrix4fv(program.uniform_location("pmv"), 1, GL_FALSE, glm::value_ptr(pmv));

    const auto& layout = ctx.layout;
    glUniform1d(program.uniform_location("width"), layout.usable_width);
    glUniform1d(program.uniform_location("height"), layout.usable_height);
    glUniform1f(program.uniform_location("y_offset"), 0.0f);
    glUniform1f(program.uniform_location("win_h"), static_cast<float>(ctx.win_h));
    glUniform1d(program.uniform_location("t_min"), ctx.t0);
    glUniform1d(program.uniform_location("t_max"), ctx.t1);
    glUniform1f(program.uniform_location("v_min"), ctx.v0);
    glUniform1f(program.uniform_location("v_max"), ctx.v1);

    // Line rendering options
    const bool snap = ctx.config ? ctx.config->snap_lines_to_pixels : false;
    glUniform1i(program.uniform_location("snap_to_pixels"), snap ? 1 : 0);

    // Zero-axis color (same as grid lines)
    const bool dark_mode = ctx.config ? ctx.config->dark_mode : false;
    const Color_palette palette = dark_mode ? Color_palette::dark() : Color_palette::light();
    glUniform4fv(program.uniform_location("zero_axis_color"), 1, glm::value_ptr(palette.grid_line));
}

void Series_renderer::modify_uniforms_for_preview(
    GL_program& program,
    const frame_context_t& ctx)
{
    const auto& layout = ctx.layout;
    const double preview_top =
        layout.usable_height + std::max(0.0, layout.h_bar_height - double(k_scissor_pad_px));
    const float preview_y = static_cast<float>(preview_top);
    const float preview_height = static_cast<float>(ctx.adjusted_preview_height);

    glUniform1f(program.uniform_location("y_offset"), preview_y);
    glUniform1d(program.uniform_location("width"), static_cast<double>(ctx.win_w));
    glUniform1d(program.uniform_location("height"), static_cast<double>(preview_height));
    glUniform1f(program.uniform_location("v_min"), ctx.preview_v0);
    glUniform1f(program.uniform_location("v_max"), ctx.preview_v1);
    glUniform1d(program.uniform_location("t_min"), ctx.t_available_min);
    glUniform1d(program.uniform_location("t_max"), ctx.t_available_max);
}

void Series_renderer::render(
    const frame_context_t& ctx,
    const std::map<int, std::shared_ptr<series_data_t>>& series)
{
    if (series.empty() || !m_asset_loader) {
        return;
    }

    const auto& layout = ctx.layout;
    if (layout.usable_width <= 0.0 || layout.usable_height <= 0.0) {
        return;
    }

    vnm::plot::Profiler* profiler = ctx.config ? ctx.config->profiler : nullptr;
    VNM_PLOT_PROFILE_SCOPE(profiler, "renderer.frame.execute_passes.render_data_series");

    // Skip all GL calls if configured (for pure CPU profiling)
    const bool skip_gl = ctx.config && ctx.config->skip_gl_calls;

    bool dark_mode = false;
    float line_width = 1.0f;
    float area_fill_alpha = 0.3f;
    {
        VNM_PLOT_PROFILE_SCOPE(
            profiler,
            "renderer.frame.execute_passes.render_data_series.setup");
        dark_mode = ctx.config ? ctx.config->dark_mode : false;
        line_width = ctx.config ? static_cast<float>(ctx.config->line_width_px) : 1.0f;
        area_fill_alpha = ctx.config ? static_cast<float>(ctx.config->area_fill_alpha) : 0.3f;
    }
    const auto to_gl_scissor_y = [&](double top, double height) -> GLint {
        return static_cast<GLint>(lround(double(ctx.win_h) - (top + height)));
    };

    // Cleanup stale VBO states for series no longer in the map
    {
        VNM_PLOT_PROFILE_SCOPE(
            profiler,
            "renderer.frame.execute_passes.render_data_series.cleanup_vbos");
        for (auto it = m_vbo_states.begin(); it != m_vbo_states.end(); ) {
            if (series.find(it->first) == series.end()) {
                auto& state = it->second;
                for (auto* view : {&state.main_view, &state.preview_view}) {
                    if (!skip_gl && view->id != UINT_MAX) {
                        glDeleteBuffers(1, &view->id);
                    }
                    if (!skip_gl && view->adjacency_ebo != 0) {
                        glDeleteBuffers(1, &view->adjacency_ebo);
                    }
                }
                it = m_vbo_states.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    // Cleanup stale colormap textures
    {
        VNM_PLOT_PROFILE_SCOPE(
            profiler,
            "renderer.frame.execute_passes.render_data_series.cleanup_colormaps");
        for (auto it = m_colormap_textures.begin(); it != m_colormap_textures.end(); ) {
            bool found = false;
            for (const auto& [_, s] : series) {
                if (s.get() == it->first) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                if (!skip_gl && it->second.texture != 0) {
                    glDeleteTextures(1, &it->second.texture);
                }
                it = m_colormap_textures.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    // Note: We always enable blend for series rendering and restore state at end.
    // Avoid glIsEnabled() which is a synchronous GL query that can stall pipeline.
    if (!skip_gl) {
        VNM_PLOT_PROFILE_SCOPE(
            profiler,
            "renderer.frame.execute_passes.render_data_series.blend_setup");
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        // Set line width once for all draw passes to avoid repeated glLineWidth calls
        glLineWidth(line_width);
        // Enable scissor test once - we'll only update the rectangle per draw pass
        glEnable(GL_SCISSOR_TEST);
    }

    for (const auto& [id, s] : series) {
        VNM_PLOT_PROFILE_SCOPE(profiler, "renderer.frame.execute_passes.render_data_series.series");
        if (!s || !s->enabled || !s->data_source) {
            continue;
        }

        Display_style style = s->style;
        if (!!(style & Display_style::COLORMAP_LINE) && !s->access.get_signal) {
            if (m_missing_signal_logged.insert(id).second) {
                if (ctx.config && ctx.config->log_error) {
                    ctx.config->log_error(
                        "COLORMAP_LINE requires Data_access_policy::get_signal (series "
                        + std::to_string(id) + ")");
                }
            }
            style = static_cast<Display_style>(
                static_cast<int>(style) & ~static_cast<int>(Display_style::COLORMAP_LINE));
        }
        if (!style) {
            continue;
        }

        auto& vbo_state = m_vbo_states[id];

        // Build LOD scales vector using shared helper
        const std::vector<std::size_t> scales = [&]() {
            VNM_PLOT_PROFILE_SCOPE(
                profiler,
                "renderer.frame.execute_passes.render_data_series.series.prepare");
            return compute_lod_scales(*s->data_source);
        }();
        const void* data_identity = s->data_source->identity();
        if (vbo_state.cached_aux_metric_identity != data_identity) {
            vbo_state.cached_aux_metric_identity = data_identity;
            vbo_state.cached_aux_metric_levels.clear();
        }
        if (vbo_state.cached_aux_metric_levels.size() != scales.size()) {
            vbo_state.cached_aux_metric_levels.assign(scales.size(), {});
        }

        // Process main view
        const std::size_t prev_lod_level = vbo_state.main_view.last_lod_level;
        auto main_result = [&]() {
            VNM_PLOT_PROFILE_SCOPE(
                profiler,
                "renderer.frame.execute_passes.render_data_series.series.process_view");
            return process_view(
                vbo_state.main_view,
                *s->data_source,
                s->access.get_timestamp,
                scales,
                ctx.t0, ctx.t1,
                layout.usable_width,
                false,
                profiler,
                skip_gl);
        }();
        if (ctx.config && ctx.config->log_debug &&
            main_result.can_draw &&
            main_result.applied_level != prev_lod_level)
        {
            std::string message = "LOD selection: series=" + std::to_string(id)
                + " level=" + std::to_string(main_result.applied_level)
                + " pps=" + std::to_string(main_result.applied_pps);
            ctx.config->log_debug(message);
        }

        // Helper to draw one pass for a specific primitive style
        auto draw_pass = [&](Display_style primitive_style,
                             vbo_view_state_t& view_state,
                             const view_render_result_t& view_result,
                             bool is_preview) {
            VNM_PLOT_PROFILE_SCOPE(
                profiler,
                "renderer.frame.execute_passes.render_data_series.series.draw_pass");
            const GLsizei count = view_result.count;
            if (count <= 0) {
                return;
            }

            // ============================================================================
            // SELECT DRAWING MODE BASED ON DISPLAY STYLE
            // ============================================================================
            // Different display styles require different OpenGL primitives:
            //
            // DOTS:          GL_POINTS
            //                - Each vertex becomes a point sprite
            //                - Geometry shader can expand to billboard quad
            //
            // LINE:          GL_LINE_STRIP
            //                - Vertices form a continuous polyline
            //                - Each pair of consecutive vertices forms a line segment
            //                - No information about neighboring segments
            //
            // COLORMAP_LINE: GL_LINE_STRIP_ADJACENCY
            //                - Enhanced line strip with adjacency information
            //                - Geometry shader receives 4 vertices per segment:
            //                  [v_prev, v_start, v_end, v_next]
            //                - Enables proper join rendering (see ADJACENCY_LINE_RENDERING.md)
            //                - Required for consistent appearance across LODs
            //
            // AREA:          GL_LINE_STRIP (processed differently in geometry shader)
            //
            const GLenum drawing_mode =
                (primitive_style == Display_style::DOTS) ? GL_POINTS :
                (primitive_style == Display_style::COLORMAP_LINE) ? GL_LINE_STRIP_ADJACENCY :
                GL_LINE_STRIP;

            // Minimum vertex count varies by primitive type:
            // - POINTS:              1 vertex minimum
            // - LINE_STRIP:          2 vertices minimum (one segment)
            // - LINE_STRIP_ADJACENCY: 2 vertices minimum (expanded to 4 with boundary duplication)
            //
            // Note: GL_LINE_STRIP_ADJACENCY requires 4 vertices for the final draw call,
            // but we duplicate the first and last vertices to satisfy this requirement,
            // so we only need count >= 2 actual data vertices.
            const GLsizei min_vertices = (drawing_mode == GL_LINE_STRIP_ADJACENCY) ? 2 :
                                        (drawing_mode == GL_LINE_STRIP) ? 2 : 1;
            if (count < min_vertices) {
                return;
            }

            // CPU-side color/uniform preparation (no GL calls)
            glm::vec4 draw_color;
            glm::vec4 line_col;
            {
                VNM_PLOT_PROFILE_SCOPE(profiler, "color_prep");
                draw_color = s->color;
                if (primitive_style == Display_style::AREA || primitive_style == Display_style::COLORMAP_AREA) {
                    draw_color.w *= area_fill_alpha;
                }
                if (dark_mode) {
                    if (is_default_series_color(draw_color)) {
                        draw_color = k_default_series_color_dark;
                    }
                }
                line_col = s->color;
                if (dark_mode && is_default_series_color(line_col)) {
                    line_col = k_default_series_color_dark;
                }
            }

            // CPU-side colormap aux-range computation (must run before skip_gl return)
            const vbo_state_t::aux_metric_cache_t* aux_cache = nullptr;
            std::size_t aux_range_scale = 1;
            if (primitive_style == Display_style::COLORMAP_AREA && !s->colormap.samples.empty()) {
                auto& aux_cache_entry = vbo_state.cached_aux_metric_levels[view_result.applied_level];
                data_snapshot_t snapshot;
                {
                    VNM_PLOT_PROFILE_SCOPE(
                        profiler,
                        "renderer.frame.execute_passes.render_data_series.series.ensure_full_resolution_aux_metric_range");
                    snapshot = s->data_source->snapshot(view_result.applied_level);
                }
                if (snapshot) {
                    const bool can_reuse =
                        aux_cache_entry.valid && aux_cache_entry.sequence == snapshot.sequence;

                    if (!can_reuse) {
                        double aux_min = 0.0;
                        double aux_max = 1.0;
                        VNM_PLOT_PROFILE_SCOPE(
                            profiler,
                            "renderer.frame.execute_passes.render_data_series.series.compute_aux_metric_range");
                        if (compute_aux_metric_range(*s, snapshot, aux_min, aux_max)) {
                            aux_cache_entry.min = aux_min;
                            aux_cache_entry.max = aux_max;
                            aux_cache_entry.valid = true;
                        }
                        else {
                            if (!aux_cache_entry.valid) {
                                aux_cache_entry.min = 0.0;
                                aux_cache_entry.max = 1.0;
                                aux_cache_entry.valid = true;
                            }
                        }
                        aux_cache_entry.sequence = snapshot.sequence;
                    }
                }
                else {
                    // Empty or invalid snapshot - keep last valid range to avoid flicker.
                    if (!aux_cache_entry.valid) {
                        aux_cache_entry.min = 0.0;
                        aux_cache_entry.max = 1.0;
                    }
                }

                std::size_t aux_range_level = view_result.applied_level;
                for (std::size_t i = 0; i < vbo_state.cached_aux_metric_levels.size(); ++i) {
                    if (vbo_state.cached_aux_metric_levels[i].valid) {
                        aux_range_level = i;
                        break;
                    }
                }
                aux_cache = &vbo_state.cached_aux_metric_levels[aux_range_level];
                if (aux_range_level < scales.size()) {
                    aux_range_scale = scales[aux_range_level];
                }
            }

            // Skip all GL calls when in no-GL mode (early return after CPU prep)
            if (skip_gl) {
                return;
            }

            // VBO required for GL rendering
            if (view_state.active_vbo == UINT_MAX) {
                return;
            }

            // Get shader for this specific style
            std::shared_ptr<GL_program> pass_shader;
            {
                VNM_PLOT_PROFILE_SCOPE(profiler, "shader_lookup");
                const shader_set_t* pass_shader_set_ptr;
                {
                    VNM_PLOT_PROFILE_SCOPE(profiler, "shader_for");
                    pass_shader_set_ptr = &s->shader_for(primitive_style);
                }
                {
                    VNM_PLOT_PROFILE_SCOPE(profiler, "get_or_load_shader");
                    pass_shader = get_or_load_shader(*pass_shader_set_ptr, ctx.config);
                }
            }
            if (!pass_shader) {
                return;
            }

            {
                VNM_PLOT_PROFILE_SCOPE(profiler, "setup_uniforms");
                const GLuint program_id = pass_shader->program_id();
                glUseProgram(program_id);
                set_common_uniforms(*pass_shader, ctx.pmv, ctx);
            }
            if (is_preview) {
                VNM_PLOT_PROFILE_SCOPE(profiler, "preview_uniforms");
                modify_uniforms_for_preview(*pass_shader, ctx);
                if (view_result.use_t_override) {
                    glUniform1d(
                        pass_shader->uniform_location("t_min"),
                        view_result.t_min_override);
                    glUniform1d(
                        pass_shader->uniform_location("t_max"),
                        view_result.t_max_override);
                }
            }

            {
                VNM_PLOT_PROFILE_SCOPE(profiler, "series_uniforms");
                glUniform4fv(pass_shader->uniform_location("color"), 1, glm::value_ptr(draw_color));

                if (primitive_style == Display_style::AREA || primitive_style == Display_style::COLORMAP_AREA) {
                    glUniform1f(pass_shader->uniform_location("line_width"), line_width);
                    glUniform4fv(pass_shader->uniform_location("line_color"), 1, glm::value_ptr(line_col));
                }

                if (const GLint loc = pass_shader->uniform_location("u_line_px"); loc >= 0) {
                    glUniform1f(loc, line_width);
                }

                if (s->access.bind_uniforms) {
                    s->access.bind_uniforms(pass_shader->program_id());
                }
            }

            // Handle colormap GL setup for colormap styles
            GLuint colormap_tex = 0;
            if (primitive_style == Display_style::COLORMAP_AREA ||
                primitive_style == Display_style::COLORMAP_LINE) {
                colormap_tex = ensure_colormap_texture(*s);
                if (colormap_tex != 0) {
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_1D, colormap_tex);

                    if (primitive_style == Display_style::COLORMAP_AREA) {
                        // Use cached aux metric values (computed in CPU section above)
                        const float aux_min_f = static_cast<float>(aux_cache ? aux_cache->min : 0.0);
                        const float aux_max_f = static_cast<float>(aux_cache ? aux_cache->max : 1.0);
                        const float aux_span = aux_max_f - aux_min_f;
                        const float inv_aux_span = (std::abs(aux_span) > 1e-12f) ? (1.0f / aux_span) : 0.0f;

                        glUniform1f(pass_shader->uniform_location("aux_min"), aux_min_f);
                        glUniform1f(pass_shader->uniform_location("aux_max"), aux_max_f);
                        const std::size_t applied_scale = (view_result.applied_level < scales.size())
                            ? scales[view_result.applied_level]
                            : 1;
                        if (const GLint loc = pass_shader->uniform_location("u_volume_min"); loc >= 0) {
                            const float scale_ratio = (aux_range_scale > 0)
                                ? (static_cast<float>(applied_scale) / static_cast<float>(aux_range_scale))
                                : 1.0f;
                            glUniform1f(loc, aux_min_f * scale_ratio);
                        }
                        if (const GLint loc = pass_shader->uniform_location("u_inv_volume_span"); loc >= 0) {
                            const float scale_ratio = (aux_range_scale > 0)
                                ? (static_cast<float>(applied_scale) / static_cast<float>(aux_range_scale))
                                : 1.0f;
                            const float scaled_inv_span = (scale_ratio > 0.0f) ? (inv_aux_span / scale_ratio) : 0.0f;
                            glUniform1f(loc, scaled_inv_span);
                        }
                        if (const GLint loc = pass_shader->uniform_location("u_colormap_tex"); loc >= 0) {
                            glUniform1i(loc, 0);
                        }
                        const float volume_scale = (applied_scale > 0)
                            ? (1.0f / static_cast<float>(applied_scale))
                            : 1.0f;
                        if (const GLint loc = pass_shader->uniform_location("u_volume_scale"); loc >= 0) {
                            glUniform1f(loc, volume_scale);
                        }
                    }
                    else {
                        if (const GLint loc = pass_shader->uniform_location("u_colormap_tex"); loc >= 0) {
                            glUniform1i(loc, 0);
                        }
                    }
                }
            }

            {
                VNM_PLOT_PROFILE_SCOPE(profiler, "vao_bind");
                const GLuint vao = ensure_series_vao(primitive_style, view_state.active_vbo, *s);
                glBindVertexArray(vao);
            }

            // Note: glLineWidth is set once at the start of render() to avoid per-draw overhead

            // Scissor test is enabled at start of render() - just update rectangle
            bool do_draw = true;
            if (is_preview) {
                const double preview_height = ctx.adjusted_preview_height;
                if (!(preview_height > 0.0)) {
                    do_draw = false;
                }
                else {
                    const double preview_top =
                        layout.usable_height + std::max(0.0, layout.h_bar_height - double(k_scissor_pad_px));
                    const GLint scissor_y = to_gl_scissor_y(preview_top, preview_height);
                    const GLsizei scissor_h = static_cast<GLsizei>(lround(preview_height));
                    if (scissor_h <= 0) {
                        do_draw = false;
                    }
                    else {
                        glScissor(
                            0,
                            scissor_y,
                            static_cast<GLsizei>(lround(ctx.win_w)),
                            scissor_h);
                    }
                }
            }
            else {
                glScissor(
                    0,
                    to_gl_scissor_y(0.0, layout.usable_height),
                    static_cast<GLsizei>(lround(layout.usable_width)),
                    static_cast<GLsizei>(lround(layout.usable_height)));
            }

            if (do_draw) {
                VNM_PLOT_PROFILE_SCOPE(profiler, "gpu_issue");

                // ====================================================================
                // ISSUE DRAW CALL
                // ====================================================================
                // For GL_LINE_STRIP_ADJACENCY, we need to provide adjacency vertices
                // at the boundaries. Since our vertex buffer contains the actual data
                // points without pre-duplicated boundary vertices, we use indexed
                // drawing to duplicate the first and last vertices on-the-fly.
                //
                // ADJACENCY VERTEX LAYOUT FOR GL_LINE_STRIP_ADJACENCY:
                //
                // The geometry shader expects vertices in groups of 4 per segment:
                //   Segment 0: [v_adj0, v0, v1, v_adj1]
                //   Segment 1: [v_adj1, v1, v2, v_adj2]
                //   ...
                //
                // For a line strip, adjacent segments share vertices, so:
                //   v_adj1 for segment 0 = v0 (the actual start of segment 1)
                //   v_adj0 for segment N = v_N (the actual end of segment N-1)
                //
                // BOUNDARY HANDLING:
                //   - First segment: v_adj0 doesn't exist, so we duplicate v0
                //   - Last segment:  v_adjN doesn't exist, so we duplicate v_N
                //
                // EXAMPLE:
                //   Vertex buffer: [v0, v1, v2, v3, v4]  (5 vertices, 4 segments)
                //
                //   Index buffer:  [0, 0, 1, 2, 3, 4, 4]  (7 indices)
                //                   ^  ^           ^  ^
                //                   |  |           |  |
                //                   |  first actual|  last duplicated (adjacency)
                //                   first duplicated   last actual
                //                   (adjacency)
                //
                //   Geometry shader invocations:
                //     Segment 0: gs_in[] = [v0, v0, v1, v2]  (v0 duplicated as prev)
                //     Segment 1: gs_in[] = [v0, v1, v2, v3]  (normal)
                //     Segment 2: gs_in[] = [v1, v2, v3, v4]  (normal)
                //     Segment 3: gs_in[] = [v2, v3, v4, v4]  (v4 duplicated as next)
                //
                // ELEMENT BUFFER MANAGEMENT:
                //   - We maintain a persistent EBO per view to avoid recreating it every frame
                //   - EBO is resized (with headroom) only when the index count exceeds capacity
                //   - For OpenGL core profile, we MUST use a bound EBO; client-side index
                //     arrays (passing indices.data() to glDrawElements) are not supported
                //
                // See ADJACENCY_LINE_RENDERING.md for more details.
                //
                if (drawing_mode == GL_LINE_STRIP_ADJACENCY) {
                    // Calculate total index count: original vertices + 2 duplicates
                    const GLsizei adjacency_count = count + 2;
                    const std::size_t required_capacity = static_cast<std::size_t>(adjacency_count);

                    // ================================================================
                    // ENSURE EBO EXISTS AND HAS SUFFICIENT CAPACITY
                    // ================================================================
                    if (view_state.adjacency_ebo == 0) {
                        // First use: create the EBO
                        glGenBuffers(1, &view_state.adjacency_ebo);
                    }

                    // Bind the EBO (required for both allocation and drawing)
                    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, view_state.adjacency_ebo);

                    // Reallocate if current capacity is insufficient
                    // Add 25% headroom to reduce reallocation frequency
                    if (view_state.adjacency_ebo_capacity < required_capacity) {
                        const std::size_t new_capacity = required_capacity + required_capacity / 4;
                        const GLsizeiptr buffer_size = static_cast<GLsizeiptr>(new_capacity * sizeof(GLuint));

                        glBufferData(GL_ELEMENT_ARRAY_BUFFER, buffer_size, nullptr, GL_DYNAMIC_DRAW);
                        view_state.adjacency_ebo_capacity = new_capacity;
                    }

                    // ================================================================
                    // GENERATE AND UPLOAD ADJACENCY INDICES
                    // ================================================================
                    // Build the index array on CPU (could be optimized to use persistent
                    // mapped buffer, but for typical sizes this is fast enough)
                    std::vector<GLuint> indices;
                    indices.reserve(adjacency_count);

                    // Duplicate first vertex for adjacency (boundary case)
                    // This provides the "previous" vertex for the first segment
                    indices.push_back(static_cast<GLuint>(view_result.first));

                    // Add all actual vertices in order
                    for (GLsizei i = 0; i < count; ++i) {
                        indices.push_back(static_cast<GLuint>(view_result.first + i));
                    }

                    // Duplicate last vertex for adjacency (boundary case)
                    // This provides the "next" vertex for the last segment
                    indices.push_back(static_cast<GLuint>(view_result.first + count - 1));

                    // Upload indices to GPU
                    const GLsizeiptr upload_size = static_cast<GLsizeiptr>(adjacency_count * sizeof(GLuint));
                    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, upload_size, indices.data());

                    // ================================================================
                    // ISSUE INDEXED DRAW CALL
                    // ================================================================
                    // With EBO bound, pass nullptr to glDrawElements (indices come from bound buffer)
                    glDrawElements(GL_LINE_STRIP_ADJACENCY, adjacency_count, GL_UNSIGNED_INT, nullptr);

                    // Unbind EBO (good practice, though not strictly required)
                    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
                }
                else {
                    // Standard non-adjacency draw call (DOTS, LINE, AREA)
                    glDrawArrays(drawing_mode, view_result.first, count);
                }
            }

            // Note: VAO unbinding moved to cleanup section to avoid per-draw overhead

            if (colormap_tex != 0) {
                glBindTexture(GL_TEXTURE_1D, 0);
            }
        };

        // Main view rendering - multiple passes for combined styles
        if (main_result.can_draw) {
            // Handle colormap area first if present
            if (!!(style & Display_style::COLORMAP_AREA)) {
                draw_pass(Display_style::COLORMAP_AREA, vbo_state.main_view, main_result, false);
            }
            // Then colormap line (rendered before plain line)
            if (!!(style & Display_style::COLORMAP_LINE)) {
                draw_pass(Display_style::COLORMAP_LINE, vbo_state.main_view, main_result, false);
            }
            // Then area (rendered before lines so lines appear on top)
            if (!!(style & Display_style::AREA)) {
                draw_pass(Display_style::AREA, vbo_state.main_view, main_result, false);
            }
            // Then lines
            if (!!(style & Display_style::LINE)) {
                draw_pass(Display_style::LINE, vbo_state.main_view, main_result, false);
            }
            // Finally dots (on top)
            if (!!(style & Display_style::DOTS)) {
                draw_pass(Display_style::DOTS, vbo_state.main_view, main_result, false);
            }
        }

        // Process preview view if visible
        if (ctx.adjusted_preview_height > 0.0) {
            const std::size_t prev_preview_lod_level = vbo_state.preview_view.last_lod_level;
            auto preview_result = [&]() {
                VNM_PLOT_PROFILE_SCOPE(
                    profiler,
                    "renderer.frame.execute_passes.render_data_series.series.process_view");
                return process_view(
                    vbo_state.preview_view,
                    *s->data_source,
                    s->access.get_timestamp,
                    scales,
                    ctx.t_available_min, ctx.t_available_max,
                    ctx.win_w,
                    true,
                    profiler,
                    skip_gl);
            }();
            if (ctx.config && ctx.config->log_debug &&
                preview_result.can_draw &&
                preview_result.applied_level != prev_preview_lod_level)
            {
                std::string message =
                    "LOD selection (preview): series=" + std::to_string(id)
                    + " level=" + std::to_string(preview_result.applied_level)
                    + " pps=" + std::to_string(preview_result.applied_pps);
                ctx.config->log_debug(message);
            }

            if (preview_result.can_draw) {
                // Preview uses same multi-pass approach
                if (!!(style & Display_style::COLORMAP_AREA)) {
                    draw_pass(Display_style::COLORMAP_AREA, vbo_state.preview_view, preview_result, true);
                }
                if (!!(style & Display_style::COLORMAP_LINE)) {
                    draw_pass(Display_style::COLORMAP_LINE, vbo_state.preview_view, preview_result, true);
                }
                if (!!(style & Display_style::AREA)) {
                    draw_pass(Display_style::AREA, vbo_state.preview_view, preview_result, true);
                }
                if (!!(style & Display_style::LINE)) {
                    draw_pass(Display_style::LINE, vbo_state.preview_view, preview_result, true);
                }
                if (!!(style & Display_style::DOTS)) {
                    draw_pass(Display_style::DOTS, vbo_state.preview_view, preview_result, true);
                }
            }
        }
    }

    if (!skip_gl) {
        VNM_PLOT_PROFILE_SCOPE(
            profiler,
            "renderer.frame.execute_passes.render_data_series.blend_cleanup");
        glUseProgram(0);
        glBindVertexArray(0);
        // Restore default line width
        glLineWidth(1.0f);
        // Disable scissor test; leave blend enabled to restore previous state.
        // Assumption: caller had GL_BLEND enabled before calling render().
        glDisable(GL_SCISSOR_TEST);
    }
}

} // namespace vnm::plot
