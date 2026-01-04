#include <vnm_plot/core/series_renderer.h>
#include <vnm_plot/core/asset_loader.h>
#include <vnm_plot/core/gl_program.h>
#include <vnm_plot/core/constants.h>

#include <glatter/glatter.h>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace vnm::plot::core {

namespace {

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

    const auto* base = static_cast<const std::uint8_t*>(snapshot.data);
    for (std::size_t i = 0; i < snapshot.count; ++i) {
        const void* sample = base + i * snapshot.stride;
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

// Binary search for timestamp in snapshot
std::size_t lower_bound_timestamp(
    const data_snapshot_t& snapshot,
    const std::function<double(const void*)>& get_timestamp,
    double t)
{
    if (!snapshot || snapshot.count == 0) {
        return 0;
    }

    const auto* base = static_cast<const std::uint8_t*>(snapshot.data);
    std::size_t lo = 0;
    std::size_t hi = snapshot.count;

    while (lo < hi) {
        std::size_t mid = lo + (hi - lo) / 2;
        const void* sample = base + mid * snapshot.stride;
        if (get_timestamp(sample) < t) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

std::size_t upper_bound_timestamp(
    const data_snapshot_t& snapshot,
    const std::function<double(const void*)>& get_timestamp,
    double t)
{
    if (!snapshot || snapshot.count == 0) {
        return 0;
    }

    const auto* base = static_cast<const std::uint8_t*>(snapshot.data);
    std::size_t lo = 0;
    std::size_t hi = snapshot.count;

    while (lo < hi) {
        std::size_t mid = lo + (hi - lo) / 2;
        const void* sample = base + mid * snapshot.stride;
        if (get_timestamp(sample) <= t) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
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
        if (!pipe) continue;
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
}

Series_renderer::series_pipe_t& Series_renderer::pipe_for(Display_style style)
{
    if (!!(style & Display_style::COLORMAP_AREA)) {
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
    if (shader_set.vert.empty() || !m_asset_loader) {
        return nullptr;
    }

    if (auto found = m_shaders.find(shader_set); found != m_shaders.end()) {
        return found->second;
    }

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

std::size_t Series_renderer::choose_level_from_base_pps(
    const std::vector<std::size_t>& scales,
    std::size_t current_level,
    double base_pps)
{
    if (scales.empty() || base_pps <= 0.0) {
        return 0;
    }

    // Target: ~2 pixels per sample for good visual quality
    constexpr double target_pps = 2.0;
    const double desired_scale = base_pps / target_pps;

    std::size_t best_level = 0;
    for (std::size_t i = 0; i < scales.size(); ++i) {
        if (static_cast<double>(scales[i]) <= desired_scale) {
            best_level = i;
        }
    }

    // Hysteresis: don't change level unless there's significant benefit
    if (current_level < scales.size()) {
        const double current_pps = base_pps / static_cast<double>(scales[current_level]);
        const double new_pps = base_pps / static_cast<double>(scales[best_level]);

        // Only change if improvement is > 50%
        if (std::abs(new_pps - target_pps) < std::abs(current_pps - target_pps) * 0.5) {
            return current_level;
        }
    }

    return best_level;
}

Series_renderer::view_render_result_t Series_renderer::process_view(
    vbo_view_state_t& view_state,
    Data_source& data_source,
    const std::function<double(const void*)>& get_timestamp,
    const std::vector<std::size_t>& scales,
    double t_min,
    double t_max,
    double width_px,
    int series_id,
    const Render_config* config)
{
    view_render_result_t result;

    if (t_max <= t_min || width_px <= 0.0) {
        return result;
    }

    const double base_pps = width_px / (t_max - t_min);
    const std::size_t level = choose_level_from_base_pps(scales, view_state.last_lod_level, base_pps);
    result.applied_level = level;
    result.applied_pps = base_pps / (level < scales.size() ? static_cast<double>(scales[level]) : 1.0);

    auto snapshot_result = data_source.try_snapshot(level);
    if (!snapshot_result) {
        ++m_metrics.snapshot_failures;
        return result;
    }

    const auto& snapshot = snapshot_result.snapshot;
    if (!snapshot || snapshot.count == 0) {
        return result;
    }

    // Find visible range using binary search
    const std::size_t first_idx = lower_bound_timestamp(snapshot, get_timestamp, t_min);
    const std::size_t last_idx = upper_bound_timestamp(snapshot, get_timestamp, t_max);

    if (first_idx >= last_idx) {
        return result;
    }

    // Ensure VBO exists and has enough capacity
    if (view_state.id == UINT_MAX) {
        glGenBuffers(1, &view_state.id);
    }

    const std::size_t needed_bytes = snapshot.count * snapshot.stride;
    const bool data_changed = (snapshot.sequence != view_state.last_sequence) ||
                              (data_source.identity() != reinterpret_cast<const void*>(view_state.active_vbo));

    if (data_changed || view_state.last_ring_size < needed_bytes) {
        glBindBuffer(GL_ARRAY_BUFFER, view_state.id);

        if (view_state.last_ring_size < needed_bytes) {
            // Allocate with some headroom
            const std::size_t alloc_size = needed_bytes + needed_bytes / 4;
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(alloc_size), nullptr, GL_DYNAMIC_DRAW);
            view_state.last_ring_size = alloc_size;
            ++m_metrics.vbo_reallocations;
            m_metrics.bytes_allocated += alloc_size;
        }

        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(snapshot.count * snapshot.stride), snapshot.data);
        m_metrics.bytes_uploaded += snapshot.count * snapshot.stride;

        glBindBuffer(GL_ARRAY_BUFFER, 0);

        view_state.last_sequence = snapshot.sequence;
        view_state.last_snapshot_elements = snapshot.count;
        view_state.active_vbo = view_state.id;
    }

    view_state.last_lod_level = level;

    result.can_draw = true;
    result.first = static_cast<GLint>(first_idx);
    result.count = static_cast<GLsizei>(last_idx - first_idx);

    view_state.last_first = result.first;
    view_state.last_count = result.count;

    return result;
}

void Series_renderer::set_common_uniforms(GLuint program, const glm::mat4& pmv, const frame_context_t& ctx)
{
    glUniformMatrix4fv(glGetUniformLocation(program, "pmv"), 1, GL_FALSE, glm::value_ptr(pmv));

    const auto& layout = ctx.layout;
    glUniform1f(glGetUniformLocation(program, "plot_width"), static_cast<float>(layout.usable_width));
    glUniform1f(glGetUniformLocation(program, "plot_height"), static_cast<float>(layout.usable_height));
    glUniform1d(glGetUniformLocation(program, "t_min"), ctx.t0);
    glUniform1d(glGetUniformLocation(program, "t_max"), ctx.t1);
    glUniform1f(glGetUniformLocation(program, "v_min"), ctx.v0);
    glUniform1f(glGetUniformLocation(program, "v_max"), ctx.v1);
}

void Series_renderer::modify_uniforms_for_preview(GLuint program, const frame_context_t& ctx)
{
    const auto& layout = ctx.layout;
    const float preview_y = static_cast<float>(layout.usable_height + layout.h_bar_height);
    const float preview_height = static_cast<float>(ctx.adjusted_preview_height);

    glUniform1f(glGetUniformLocation(program, "plot_y_offset"), preview_y);
    glUniform1f(glGetUniformLocation(program, "plot_height"), preview_height);
    glUniform1f(glGetUniformLocation(program, "v_min"), ctx.preview_v0);
    glUniform1f(glGetUniformLocation(program, "v_max"), ctx.preview_v1);
    glUniform1d(glGetUniformLocation(program, "t_min"), ctx.t_available_min);
    glUniform1d(glGetUniformLocation(program, "t_max"), ctx.t_available_max);
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

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    for (const auto& [id, s] : series) {
        if (!s || !s->enabled || !s->data_source) {
            continue;
        }

        const auto& shader_set = s->shader_for(s->style);
        auto shader = get_or_load_shader(shader_set, ctx.config);
        if (!shader) {
            continue;
        }

        auto& vbo_state = m_vbo_states[id];

        // Build LOD scales vector
        std::vector<std::size_t> scales;
        const std::size_t num_levels = s->data_source->lod_levels();
        scales.reserve(num_levels);
        for (std::size_t i = 0; i < num_levels; ++i) {
            scales.push_back(s->data_source->lod_scale(i));
        }

        // Process main view
        auto main_result = process_view(
            vbo_state.main_view,
            *s->data_source,
            s->access.get_timestamp,
            scales,
            ctx.t0, ctx.t1,
            layout.usable_width,
            id,
            ctx.config);

        if (main_result.can_draw) {
            const GLuint program_id = shader->program_id();
            glUseProgram(program_id);

            set_common_uniforms(program_id, ctx.pmv, ctx);
            glUniform4fv(glGetUniformLocation(program_id, "color"), 1, glm::value_ptr(s->color));

            // Bind custom uniforms if any
            if (s->access.bind_uniforms) {
                s->access.bind_uniforms(program_id);
            }

            // Handle colormap
            GLuint colormap_tex = 0;
            if (!!(s->style & Display_style::COLORMAP_AREA)) {
                colormap_tex = ensure_colormap_texture(*s);
                if (colormap_tex != 0) {
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_1D, colormap_tex);
                    glUniform1i(glGetUniformLocation(program_id, "colormap"), 0);

                    // Update aux metric range if needed
                    auto snapshot = s->data_source->snapshot(main_result.applied_level);
                    if (snapshot) {
                        double aux_min = 0.0, aux_max = 1.0;
                        if (compute_aux_metric_range(*s, snapshot, aux_min, aux_max)) {
                            vbo_state.cached_aux_metric_min = aux_min;
                            vbo_state.cached_aux_metric_max = aux_max;
                        }
                    }
                    glUniform1f(glGetUniformLocation(program_id, "aux_min"),
                               static_cast<float>(vbo_state.cached_aux_metric_min));
                    glUniform1f(glGetUniformLocation(program_id, "aux_max"),
                               static_cast<float>(vbo_state.cached_aux_metric_max));
                }
            }

            const GLuint vao = ensure_series_vao(s->style, vbo_state.main_view.id, *s);
            glBindVertexArray(vao);

            // Draw based on style
            if (!!(s->style & Display_style::DOTS)) {
                glDrawArrays(GL_POINTS, main_result.first, main_result.count);
            }
            else if (!!(s->style & Display_style::AREA)) {
                glDrawArrays(GL_LINE_STRIP, main_result.first, main_result.count);
            }
            else {
                glDrawArrays(GL_LINE_STRIP, main_result.first, main_result.count);
            }

            glBindVertexArray(0);

            if (colormap_tex != 0) {
                glBindTexture(GL_TEXTURE_1D, 0);
            }
        }

        // Process preview view if visible
        if (ctx.adjusted_preview_height > 0.0) {
            auto preview_result = process_view(
                vbo_state.preview_view,
                *s->data_source,
                s->access.get_timestamp,
                scales,
                ctx.t_available_min, ctx.t_available_max,
                layout.usable_width,
                id,
                ctx.config);

            if (preview_result.can_draw) {
                const GLuint program_id = shader->program_id();
                glUseProgram(program_id);

                set_common_uniforms(program_id, ctx.pmv, ctx);
                modify_uniforms_for_preview(program_id, ctx);
                glUniform4fv(glGetUniformLocation(program_id, "color"), 1, glm::value_ptr(s->color));

                if (s->access.bind_uniforms) {
                    s->access.bind_uniforms(program_id);
                }

                const GLuint vao = ensure_series_vao(s->style, vbo_state.preview_view.id, *s);
                glBindVertexArray(vao);

                glDrawArrays(GL_LINE_STRIP, preview_result.first, preview_result.count);

                glBindVertexArray(0);
            }
        }
    }

    glUseProgram(0);
    glDisable(GL_BLEND);
}

} // namespace vnm::plot::core
