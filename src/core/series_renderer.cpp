#include <vnm_plot/core/series_renderer.h>
#include <vnm_plot/core/algo.h>
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

Series_renderer::view_render_result_t Series_renderer::process_view(
    vbo_view_state_t& view_state,
    Data_source& data_source,
    const std::function<double(const void*)>& get_timestamp,
    const std::vector<std::size_t>& scales,
    double t_min,
    double t_max,
    double width_px)
{
    view_render_result_t result;

    if (scales.empty() || t_max <= t_min || width_px <= 0.0) {
        return result;
    }

    const std::size_t level_count = scales.size();
    const std::size_t max_level_index = level_count > 0 ? level_count - 1 : 0;
    std::size_t target_level = std::min<std::size_t>(view_state.last_lod_level, max_level_index);

    constexpr int k_max_lod_attempts = 3;

    for (int attempt = 0; attempt < k_max_lod_attempts; ++attempt) {
        const std::size_t applied_level = std::min<std::size_t>(target_level, max_level_index);
        const std::size_t applied_scale = scales[applied_level];

        auto snapshot_result = data_source.try_snapshot(applied_level);
        if (!snapshot_result) {
            view_state.last_lod_level = applied_level;
            ++m_metrics.snapshot_failures;

            result.can_draw = (view_state.active_vbo != UINT_MAX && view_state.last_count > 0);
            if (result.can_draw) {
                result.first = view_state.last_first;
                result.count = view_state.last_count;
                result.applied_level = applied_level;
            }
            break;
        }

        const auto& snapshot = snapshot_result.snapshot;
        if (!snapshot || snapshot.count == 0) {
            view_state.last_lod_level = applied_level;
            ++m_metrics.snapshot_failures;
            break;
        }

        // Find visible range using binary search
        std::size_t first_idx = 0;
        std::size_t last_idx = snapshot.count;
        if (get_timestamp) {
            first_idx = algo::lower_bound_timestamp(
                snapshot.data, snapshot.count, snapshot.stride, get_timestamp, t_min);
            if (first_idx > 0) {
                --first_idx;
            }
            last_idx = algo::upper_bound_timestamp(
                snapshot.data, snapshot.count, snapshot.stride, get_timestamp, t_max);
            last_idx = std::min(last_idx + 2, snapshot.count);
        }

        if (first_idx >= last_idx) {
            return result;
        }

        const GLsizei count = static_cast<GLsizei>(last_idx - first_idx);
        const std::size_t base_samples = (count > 0) ? static_cast<std::size_t>(count) * applied_scale : 0;
        const double base_pps = (base_samples > 0)
            ? width_px / static_cast<double>(base_samples)
            : 0.0;

        const std::size_t desired_level = algo::choose_lod_level(scales, applied_level, base_pps);
        if (desired_level != applied_level) {
            target_level = desired_level;
            continue;
        }

        // Ensure VBO exists and has enough capacity
        if (view_state.id == UINT_MAX) {
            glGenBuffers(1, &view_state.id);
        }

        const std::size_t needed_bytes = snapshot.count * snapshot.stride;
        const void* current_identity = data_source.identity();

        const bool region_changed = (view_state.last_ring_size < needed_bytes);
        const bool seq_changed = (snapshot.sequence != view_state.last_sequence);
        const bool lod_level_changed = (applied_level != view_state.last_lod_level);
        const bool identity_changed = (current_identity != view_state.cached_data_identity);

        const bool must_upload = region_changed || seq_changed || lod_level_changed || identity_changed;
        if (must_upload) {
            glBindBuffer(GL_ARRAY_BUFFER, view_state.id);

            if (region_changed) {
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
            view_state.cached_data_identity = current_identity;
        }

        view_state.last_snapshot_elements = snapshot.count;
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

void Series_renderer::set_common_uniforms(GLuint program, const glm::mat4& pmv, const frame_context_t& ctx)
{
    glUniformMatrix4fv(glGetUniformLocation(program, "pmv"), 1, GL_FALSE, glm::value_ptr(pmv));

    const auto& layout = ctx.layout;
    glUniform1d(glGetUniformLocation(program, "width"), layout.usable_width);
    glUniform1d(glGetUniformLocation(program, "height"), layout.usable_height);
    glUniform1f(glGetUniformLocation(program, "y_offset"), 0.0f);
    glUniform1d(glGetUniformLocation(program, "t_min"), ctx.t0);
    glUniform1d(glGetUniformLocation(program, "t_max"), ctx.t1);
    glUniform1f(glGetUniformLocation(program, "v_min"), ctx.v0);
    glUniform1f(glGetUniformLocation(program, "v_max"), ctx.v1);

    // Line rendering options
    const bool snap = ctx.config ? ctx.config->snap_lines_to_pixels : true;
    glUniform1i(glGetUniformLocation(program, "snap_to_pixels"), snap ? 1 : 0);
}

void Series_renderer::modify_uniforms_for_preview(GLuint program, const frame_context_t& ctx)
{
    const auto& layout = ctx.layout;
    const float preview_y = static_cast<float>(layout.usable_height + layout.h_bar_height);
    const float preview_height = static_cast<float>(ctx.adjusted_preview_height);

    glUniform1f(glGetUniformLocation(program, "y_offset"), preview_y);
    glUniform1d(glGetUniformLocation(program, "width"), static_cast<double>(ctx.win_w));
    glUniform1d(glGetUniformLocation(program, "height"), static_cast<double>(preview_height));
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

    const bool dark_mode = ctx.config ? ctx.config->dark_mode : false;
    const float line_width = ctx.config ? static_cast<float>(ctx.config->line_width_px) : 1.0f;
    const float area_fill_alpha = ctx.config ? static_cast<float>(ctx.config->area_fill_alpha) : 0.3f;
    const auto to_gl_scissor_y = [&](double top, double height) -> GLint {
        return static_cast<GLint>(lround(double(ctx.win_h) - (top + height)));
    };

    // Cleanup stale VBO states for series no longer in the map
    for (auto it = m_vbo_states.begin(); it != m_vbo_states.end(); ) {
        if (series.find(it->first) == series.end()) {
            auto& state = it->second;
            for (auto* view : {&state.main_view, &state.preview_view}) {
                if (view->id != UINT_MAX) {
                    glDeleteBuffers(1, &view->id);
                }
            }
            it = m_vbo_states.erase(it);
        } else {
            ++it;
        }
    }

    // Cleanup stale colormap textures
    for (auto it = m_colormap_textures.begin(); it != m_colormap_textures.end(); ) {
        bool found = false;
        for (const auto& [_, s] : series) {
            if (s.get() == it->first) {
                found = true;
                break;
            }
        }
        if (!found) {
            if (it->second.texture != 0) {
                glDeleteTextures(1, &it->second.texture);
            }
            it = m_colormap_textures.erase(it);
        } else {
            ++it;
        }
    }

    const GLboolean was_blend = glIsEnabled(GL_BLEND);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    for (const auto& [id, s] : series) {
        if (!s || !s->enabled || !s->data_source) {
            continue;
        }

        auto& vbo_state = m_vbo_states[id];

        // Build LOD scales vector using shared helper
        const std::vector<std::size_t> scales = algo::compute_lod_scales(*s->data_source);

        // Process main view
        auto main_result = process_view(
            vbo_state.main_view,
            *s->data_source,
            s->access.get_timestamp,
            scales,
            ctx.t0, ctx.t1,
            layout.usable_width);

        // Helper to draw one pass for a specific primitive style
        auto draw_pass = [&](Display_style primitive_style,
                             vbo_view_state_t& view_state,
                             const view_render_result_t& view_result,
                             bool is_preview) {
            const GLsizei count = view_result.count;
            if (count <= 0) {
                return;
            }

            if (view_state.active_vbo == UINT_MAX) {
                return;
            }

            const GLenum drawing_mode = (primitive_style == Display_style::DOTS) ? GL_POINTS : GL_LINE_STRIP;
            if (drawing_mode == GL_LINE_STRIP && count < 2) {
                return;
            }

            // Get shader for this specific style
            const auto& pass_shader_set = s->shader_for(primitive_style);
            auto pass_shader = get_or_load_shader(pass_shader_set, ctx.config);
            if (!pass_shader) {
                return;
            }

            const GLuint program_id = pass_shader->program_id();
            glUseProgram(program_id);

            set_common_uniforms(program_id, ctx.pmv, ctx);
            if (is_preview) {
                modify_uniforms_for_preview(program_id, ctx);
            }

            glm::vec4 draw_color = s->color;
            if (primitive_style == Display_style::AREA || primitive_style == Display_style::COLORMAP_AREA) {
                draw_color.w *= area_fill_alpha;
            }
            if (dark_mode) {
                if (is_default_series_color(draw_color)) {
                    draw_color = k_default_series_color_dark;
                }
            }
            glUniform4fv(glGetUniformLocation(program_id, "color"), 1, glm::value_ptr(draw_color));

            if (primitive_style == Display_style::AREA || primitive_style == Display_style::COLORMAP_AREA) {
                glUniform1f(glGetUniformLocation(program_id, "line_width"), line_width);
                glUniform4fv(glGetUniformLocation(program_id, "line_color"), 1, glm::value_ptr(draw_color));
            }

            if (const GLint loc = glGetUniformLocation(program_id, "u_line_px"); loc >= 0) {
                glUniform1f(loc, line_width);
            }

            if (s->access.bind_uniforms) {
                s->access.bind_uniforms(program_id);
            }

            // Handle colormap for COLORMAP_AREA
            GLuint colormap_tex = 0;
            if (primitive_style == Display_style::COLORMAP_AREA) {
                colormap_tex = ensure_colormap_texture(*s);
                if (colormap_tex != 0) {
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_1D, colormap_tex);
                    glUniform1i(glGetUniformLocation(program_id, "colormap"), 0);

                    const auto snapshot = s->data_source->snapshot(view_result.applied_level);
                    if (snapshot) {
                        double aux_min = 0.0, aux_max = 1.0;
                        if (compute_aux_metric_range(*s, snapshot, aux_min, aux_max)) {
                            vbo_state.cached_aux_metric_min = aux_min;
                            vbo_state.cached_aux_metric_max = aux_max;
                        }
                    }
                    const float aux_min_f = static_cast<float>(vbo_state.cached_aux_metric_min);
                    const float aux_max_f = static_cast<float>(vbo_state.cached_aux_metric_max);
                    const float aux_span = aux_max_f - aux_min_f;
                    const float inv_aux_span = (std::abs(aux_span) > 1e-12f) ? (1.0f / aux_span) : 0.0f;

                    glUniform1f(glGetUniformLocation(program_id, "aux_min"), aux_min_f);
                    glUniform1f(glGetUniformLocation(program_id, "aux_max"), aux_max_f);
                    if (const GLint loc = glGetUniformLocation(program_id, "u_volume_min"); loc >= 0) {
                        glUniform1f(loc, aux_min_f);
                    }
                    if (const GLint loc = glGetUniformLocation(program_id, "u_inv_volume_span"); loc >= 0) {
                        glUniform1f(loc, inv_aux_span);
                    }
                    if (const GLint loc = glGetUniformLocation(program_id, "u_colormap_tex"); loc >= 0) {
                        glUniform1i(loc, 0);
                    }
                    const std::size_t applied_scale = (view_result.applied_level < scales.size())
                        ? scales[view_result.applied_level]
                        : 1;
                    const float volume_scale = (applied_scale > 0) ? (1.0f / static_cast<float>(applied_scale)) : 1.0f;
                    if (const GLint loc = glGetUniformLocation(program_id, "u_volume_scale"); loc >= 0) {
                        glUniform1f(loc, volume_scale);
                    }
                }
            }

            const GLuint vao = ensure_series_vao(primitive_style, view_state.active_vbo, *s);
            glBindVertexArray(vao);

            if (drawing_mode == GL_LINE_STRIP) {
                glLineWidth(line_width);
            }

            bool scissor_enabled = false;
            bool do_draw = true;
            if (is_preview) {
                const double preview_height = ctx.adjusted_preview_height;
                if (!(preview_height > 0.0)) {
                    do_draw = false;
                } else {
                    const double preview_top = layout.usable_height + layout.h_bar_height;
                    const GLint scissor_y = to_gl_scissor_y(preview_top, preview_height);
                    const GLsizei scissor_h = static_cast<GLsizei>(lround(preview_height));
                    if (scissor_h <= 0) {
                        do_draw = false;
                    } else {
                        glEnable(GL_SCISSOR_TEST);
                        glScissor(
                            0,
                            scissor_y,
                            static_cast<GLsizei>(lround(ctx.win_w)),
                            scissor_h);
                        scissor_enabled = true;
                    }
                }
            } else {
                glEnable(GL_SCISSOR_TEST);
                glScissor(
                    0,
                    to_gl_scissor_y(0.0, layout.usable_height),
                    static_cast<GLsizei>(lround(layout.usable_width)),
                    static_cast<GLsizei>(lround(layout.usable_height)));
                scissor_enabled = true;
            }

            if (do_draw) {
                glDrawArrays(drawing_mode, view_result.first, count);
            }

            if (scissor_enabled) {
                glDisable(GL_SCISSOR_TEST);
            }
            if (drawing_mode == GL_LINE_STRIP) {
                glLineWidth(1.0f);
            }

            glBindVertexArray(0);

            if (colormap_tex != 0) {
                glBindTexture(GL_TEXTURE_1D, 0);
            }
        };

        // Main view rendering - multiple passes for combined styles
        if (main_result.can_draw) {
            // Handle colormap area first if present
            if (!!(s->style & Display_style::COLORMAP_AREA)) {
                draw_pass(Display_style::COLORMAP_AREA, vbo_state.main_view, main_result, false);
            }
            // Then area (rendered before lines so lines appear on top)
            if (!!(s->style & Display_style::AREA)) {
                draw_pass(Display_style::AREA, vbo_state.main_view, main_result, false);
            }
            // Then lines
            if (!!(s->style & Display_style::LINE)) {
                draw_pass(Display_style::LINE, vbo_state.main_view, main_result, false);
            }
            // Finally dots (on top)
            if (!!(s->style & Display_style::DOTS)) {
                draw_pass(Display_style::DOTS, vbo_state.main_view, main_result, false);
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
                ctx.win_w);

            if (preview_result.can_draw) {
                // Preview uses same multi-pass approach
                if (!!(s->style & Display_style::COLORMAP_AREA)) {
                    draw_pass(Display_style::COLORMAP_AREA, vbo_state.preview_view, preview_result, true);
                }
                if (!!(s->style & Display_style::AREA)) {
                    draw_pass(Display_style::AREA, vbo_state.preview_view, preview_result, true);
                }
                if (!!(s->style & Display_style::LINE)) {
                    draw_pass(Display_style::LINE, vbo_state.preview_view, preview_result, true);
                }
                if (!!(s->style & Display_style::DOTS)) {
                    draw_pass(Display_style::DOTS, vbo_state.preview_view, preview_result, true);
                }
            }
        }
    }

    glUseProgram(0);
    if (!was_blend) {
        glDisable(GL_BLEND);
    }
}

} // namespace vnm::plot::core
