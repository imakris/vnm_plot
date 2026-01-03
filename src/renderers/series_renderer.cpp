#include <vnm_plot/renderers/series_renderer.h>
#include <vnm_plot/color_palette.h>
#include <vnm_plot/plot_algo.h>
#include <vnm_plot/plot_config.h>

#include <glatter/glatter.h>
#include <glm/gtc/type_ptr.hpp>
#include <QOpenGLShaderProgram>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace vnm::plot {

namespace {

std::shared_ptr<QOpenGLShaderProgram> load_shader_program(
    const QString& vert, const QString& geom, const QString& frag, const Plot_config* config)
{
    const auto sp = std::make_shared<QOpenGLShaderProgram>();
    if (!sp->addCacheableShaderFromSourceFile(QOpenGLShader::Vertex, vert)) {
        if (config && config->log_error) {
            config->log_error(QString("Vert shader error: %1").arg(sp->log()).toStdString());
        }
        return nullptr;
    }
    if (!geom.isEmpty() && !sp->addCacheableShaderFromSourceFile(QOpenGLShader::Geometry, geom)) {
        if (config && config->log_error) {
            config->log_error(QString("Geom shader error: %1").arg(sp->log()).toStdString());
        }
        return nullptr;
    }
    if (!sp->addCacheableShaderFromSourceFile(QOpenGLShader::Fragment, frag)) {
        if (config && config->log_error) {
            config->log_error(QString("Frag shader error: %1").arg(sp->log()).toStdString());
        }
        return nullptr;
    }
    if (!sp->link()) {
        if (config && config->log_error) {
            config->log_error(QString("Linker error: %1").arg(sp->log()).toStdString());
        }
        return nullptr;
    }
    return sp;
}

bool compute_aux_metric_range(
    const series_data_t& series,
    const data_snapshot_t& snapshot,
    double& out_min,
    double& out_max)
{
    if (!series.get_aux_metric || !snapshot || snapshot.count == 0 || snapshot.stride == 0) {
        return false;
    }

    double min_value = std::numeric_limits<double>::infinity();
    double max_value = -std::numeric_limits<double>::infinity();
    bool have_any = false;

    const auto* base = static_cast<const std::uint8_t*>(snapshot.data);
    for (std::size_t i = 0; i < snapshot.count; ++i) {
        const void* sample = base + i * snapshot.stride;
        const double value = series.get_aux_metric(sample);
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

    for (auto& [_, shader] : m_shaders) {
        shader.reset();
    }
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
    if (style & Display_style::COLORMAP_AREA) {
        return *m_pipe_colormap;
    }
    if (style & Display_style::DOTS) {
        return *m_pipe_dots;
    }
    if (style & Display_style::AREA) {
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
    auto& entry = pipe.by_layout[series.layout_key];

    if (entry.vao != 0 && entry.vbo == vbo) {
        return entry.vao;
    }

    if (entry.vao == 0) {
        glGenVertexArrays(1, &entry.vao);
    }

    glBindVertexArray(entry.vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    if (series.setup_vertex_attributes) {
        series.setup_vertex_attributes();
    }
    entry.vbo = vbo;

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return entry.vao;
}

std::shared_ptr<QOpenGLShaderProgram> Series_renderer::get_or_load_shader(
    const shader_set_t& shader_set,
    const Plot_config* config)
{
    if (shader_set.vert.empty()) {
        return nullptr;
    }

    if (auto found = m_shaders.find(shader_set); found != m_shaders.end()) {
        return found->second;
    }

    auto sp = load_shader_program(
        QString::fromStdString(shader_set.vert),
        QString::fromStdString(shader_set.geom),
        QString::fromStdString(shader_set.frag),
        config);

    if (!sp) {
        return nullptr;
    }
    m_shaders.emplace(shader_set, sp);
    return sp;
}

std::size_t Series_renderer::choose_level_from_base_pps(
    const std::vector<std::size_t>& scales,
    std::size_t current_level,
    double base_pps)
{
    if (scales.empty() || !(base_pps > 0.0)) {
        return 0;
    }

    const std::size_t max_level = scales.size() - 1;
    std::size_t level = std::min(current_level, max_level);

    auto subdivision_between = [&](std::size_t lower, std::size_t higher) -> double {
        if (higher >= scales.size() || lower >= scales.size()) {
            return 0.0;
        }
        const double lower_scale = static_cast<double>(scales[lower]);
        const double higher_scale = static_cast<double>(scales[higher]);
        if (!(lower_scale > 0.0)) {
            return 0.0;
        }
        return higher_scale / lower_scale;
    };

    auto level_pixels_per_sample = [&](std::size_t lvl) -> double {
        if (lvl >= scales.size()) {
            return 0.0;
        }
        return base_pps * static_cast<double>(scales[lvl]);
    };

    while (level + 1 < scales.size()) {
        const double subdivision = subdivision_between(level, level + 1);
        if (!(subdivision > 1.0)) {
            break;
        }
        const double threshold_up = 1.0 / subdivision;
        const double current_pps = level_pixels_per_sample(level);
        if (current_pps < threshold_up) {
            ++level;
        }
        else {
            break;
        }
    }

    while (level > 0) {
        const double current_pps = level_pixels_per_sample(level);
        if (current_pps > 1.0) {
            --level;
        }
        else {
            break;
        }
    }

    return level;
}

void Series_renderer::set_common_uniforms(GLuint program, const glm::mat4& pmv, const frame_context_t& ctx)
{
    glUniformMatrix4fv(glGetUniformLocation(program, "pmv"), 1, GL_FALSE, glm::value_ptr(pmv));
    glUniform1d(glGetUniformLocation(program, "t_min"), ctx.t0);
    glUniform1d(glGetUniformLocation(program, "t_max"), ctx.t1);
    glUniform1d(glGetUniformLocation(program, "v_min"), ctx.v0);
    glUniform1d(glGetUniformLocation(program, "v_max"), ctx.v1);
    glUniform1d(glGetUniformLocation(program, "width"), ctx.layout.usable_width);
    glUniform1d(glGetUniformLocation(program, "height"), ctx.layout.usable_height);
    glUniform1f(glGetUniformLocation(program, "y_offset"), 0.f);

    const bool snap_lines = ctx.config ? ctx.config->snap_lines_to_pixels : true;
    if (const GLint loc = glGetUniformLocation(program, "snap_to_pixels"); loc >= 0) {
        glUniform1i(loc, snap_lines ? 1 : 0);
    }
}

void Series_renderer::modify_uniforms_for_preview(GLuint program, const frame_context_t& ctx)
{
    glUniform1d(glGetUniformLocation(program, "t_min"), ctx.snapshot.cfg.t_available_min);
    glUniform1d(glGetUniformLocation(program, "t_max"), ctx.snapshot.cfg.t_available_max);
    glUniform1d(glGetUniformLocation(program, "v_min"), ctx.preview_v0);
    glUniform1d(glGetUniformLocation(program, "v_max"), ctx.preview_v1);
    glUniform1d(glGetUniformLocation(program, "width"), double(ctx.win_w));
    glUniform1d(glGetUniformLocation(program, "height"), ctx.snapshot.adjusted_preview_height - constants::k_scissor_pad_px);
    glUniform1f(glGetUniformLocation(program, "y_offset"),
        float(ctx.layout.usable_height) + float(ctx.snapshot.base_label_height_px) + constants::k_scissor_pad_px);

    const bool snap_lines = ctx.config ? ctx.config->snap_lines_to_pixels : true;
    if (const GLint loc = glGetUniformLocation(program, "snap_to_pixels"); loc >= 0) {
        glUniform1i(loc, snap_lines ? 1 : 0);
    }
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
    const Plot_config* config)
{
    view_render_result_t result;

    if (scales.empty()) {
        return result;
    }

    const std::size_t level_count = scales.size();
    const std::size_t max_level_index = level_count - 1;
    std::size_t target_level = std::min<std::size_t>(view_state.last_lod_level, max_level_index);

    constexpr int k_max_lod_attempts = 3;

    for (int attempt = 0; attempt < k_max_lod_attempts; ++attempt) {
        const std::size_t applied_level = std::min<std::size_t>(target_level, max_level_index);
        const std::size_t applied_scale = scales[applied_level];

        data_snapshot_t snap = data_source.snapshot(applied_level);

        if (!snap.data || snap.count == 0) {
            view_state.last_lod_level = applied_level;
            m_metrics.snapshot_failures.fetch_add(1, std::memory_order_relaxed);

            result.can_draw = (view_state.active_vbo != UINT_MAX && view_state.last_count > 0);
            if (result.can_draw) {
                result.first = view_state.last_first;
                result.count = view_state.last_count;
                result.applied_level = applied_level;
            }
            break;
        }

        // Binary search for visible range
        const auto* snap_begin = static_cast<const std::uint8_t*>(snap.data);

        // Find visible range using timestamp accessor
        std::size_t start_idx = 0;
        std::size_t end_idx = snap.count;

        if (get_timestamp) {
            // Binary search for start
            std::size_t lo = 0, hi = snap.count;
            while (lo < hi) {
                std::size_t mid = (lo + hi) / 2;
                double t = get_timestamp(snap_begin + mid * snap.stride);
                if (t < t_min) {
                    lo = mid + 1;
                } else {
                    hi = mid;
                }
            }
            start_idx = lo > 0 ? lo - 1 : 0;

            // Binary search for end
            lo = start_idx;
            hi = snap.count;
            while (lo < hi) {
                std::size_t mid = (lo + hi) / 2;
                double t = get_timestamp(snap_begin + mid * snap.stride);
                if (t <= t_max) {
                    lo = mid + 1;
                } else {
                    hi = mid;
                }
            }
            end_idx = std::min(lo + 2, snap.count);
        }

        const GLint first = static_cast<GLint>(start_idx);
        const GLsizei count = (end_idx > start_idx) ? static_cast<GLsizei>(end_idx - start_idx) : 0;

        const std::size_t base_samples = (count > 0) ? static_cast<std::size_t>(count) * applied_scale : 0;
        const double base_pps = (base_samples > 0 && width_px > 0.0)
            ? width_px / static_cast<double>(base_samples)
            : 0.0;

        const std::size_t desired_level = choose_level_from_base_pps(scales, applied_level, base_pps);

        if (desired_level != applied_level) {
            target_level = desired_level;
            continue;
        }

        // Manage VBO
        const uint64_t new_sequence = snap.sequence;
        const std::size_t ring_physical_size_bytes = snap.stride * snap.count;

        const bool region_changed = (view_state.last_ring_size != ring_physical_size_bytes) || (view_state.id == UINT_MAX);
        const bool seq_changed = (new_sequence != view_state.last_sequence);
        const bool lod_level_changed = (applied_level != view_state.last_lod_level);

        const bool must_upload = region_changed || seq_changed || lod_level_changed;

        if (must_upload) {
            if (view_state.id == UINT_MAX) {
                glGenBuffers(1, &view_state.id);
            }

            glBindBuffer(GL_ARRAY_BUFFER, view_state.id);
            if (region_changed) {
                glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(ring_physical_size_bytes), nullptr, GL_STREAM_DRAW);
                m_metrics.vbo_reallocations.fetch_add(1, std::memory_order_relaxed);
                m_metrics.bytes_allocated.fetch_add(ring_physical_size_bytes, std::memory_order_relaxed);
            }

            const std::size_t upload_bytes = snap.count * snap.stride;
            glBufferSubData(GL_ARRAY_BUFFER, 0, upload_bytes, snap.data);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            m_metrics.bytes_uploaded.fetch_add(upload_bytes, std::memory_order_relaxed);

            view_state.last_ring_size = ring_physical_size_bytes;
            view_state.last_sequence = new_sequence;
        }

        view_state.last_snapshot_elements = snap.count;
        view_state.active_vbo = view_state.id;
        view_state.last_first = first;
        view_state.last_count = count;

        const double applied_pps = base_pps * static_cast<double>(scales[applied_level]);

        if (lod_level_changed && config && config->log_debug) {
            const QString message = QString("[LOD] Series %1 L%2 (pps=%3)")
                .arg(series_id)
                .arg(static_cast<unsigned long long>(applied_level))
                .arg(applied_pps, 0, 'g', 6);
            config->log_debug(message.toStdString());
        }

        view_state.last_lod_level = applied_level;

        result.can_draw = true;
        result.first = first;
        result.count = count;
        result.applied_level = applied_level;
        result.applied_pps = applied_pps;

        break;
    }

    return result;
}

void Series_renderer::render(
    const frame_context_t& ctx,
    const std::map<int, std::shared_ptr<series_data_t>>& series)
{
    VNM_PLOT_PROFILE_SCOPE(ctx.config ? ctx.config->profiler.get() : nullptr, "series.render");

    const bool dark_mode = ctx.config ? ctx.config->dark_mode : false;
    const float line_width = ctx.config ? static_cast<float>(ctx.config->line_width_px) : 1.0f;
    std::unordered_set<const series_data_t*> colormap_series_this_frame;
    std::unordered_set<int> active_ids;

    for (const auto& [id, data] : series) {
        if (!data || !data->enabled || !data->data_source) {
            continue;
        }

        active_ids.insert(id);

        std::vector<Display_style> pass_styles;
        pass_styles.reserve(3);

        const bool has_shader_sets = !data->shader_sets.empty();
        if (!has_shader_sets) {
            if (data->style & Display_style::COLORMAP_AREA) {
                pass_styles.push_back(Display_style::COLORMAP_AREA);
            }
            else if (data->style & Display_style::DOTS) {
                pass_styles.push_back(Display_style::DOTS);
            }
            else if (data->style & Display_style::AREA) {
                pass_styles.push_back(Display_style::AREA);
            }
            else {
                pass_styles.push_back(Display_style::LINE);
            }
        }
        else {
            if (data->style & Display_style::COLORMAP_AREA) {
                pass_styles.push_back(Display_style::COLORMAP_AREA);
            }
            else {
                if (data->style & Display_style::AREA) {
                    pass_styles.push_back(Display_style::AREA);
                }
                if (data->style & Display_style::LINE) {
                    pass_styles.push_back(Display_style::LINE);
                }
                if (data->style & Display_style::DOTS) {
                    pass_styles.push_back(Display_style::DOTS);
                }
            }

            if (pass_styles.empty()) {
                pass_styles.push_back(Display_style::LINE);
            }
        }

        auto shader_set_for_style = [&](Display_style style) -> const shader_set_t* {
            if (!data->shader_sets.empty()) {
                auto it = data->shader_sets.find(style);
                if (it != data->shader_sets.end()) {
                    return &it->second;
                }
                return nullptr;
            }
            return &data->shader_set;
        };

        vbo_state_t& vbo_state = m_vbo_states[id];
        const void* data_identity = data->data_source->identity();
        if (vbo_state.cached_data_identity != data_identity) {
            for (auto* view : {&vbo_state.main_view, &vbo_state.preview_view}) {
                if (view->id != UINT_MAX) {
                    glDeleteBuffers(1, &view->id);
                }
                view->reset();
            }
            vbo_state.cached_data_identity = data_identity;
            vbo_state.cached_aux_metric_identity = nullptr;
            vbo_state.cached_aux_metric_sequence = 0;
            vbo_state.has_cached_aux_metric_range = false;
        }

        // Compute LOD scales from data source
        std::vector<std::size_t> scales;
        const std::size_t levels = data->data_source->lod_levels();
        scales.reserve(levels);
        for (std::size_t lvl = 0; lvl < levels; ++lvl) {
            scales.push_back(data->data_source->lod_scale(lvl));
        }

        if (scales.empty()) {
            continue;
        }

        view_render_result_t main_result = process_view(
            vbo_state.main_view, *data->data_source, data->get_timestamp, scales,
            ctx.t0, ctx.t1, ctx.layout.usable_width, id, ctx.config);

        view_render_result_t preview_result = process_view(
            vbo_state.preview_view, *data->data_source, data->get_timestamp, scales,
            ctx.snapshot.cfg.t_available_min, ctx.snapshot.cfg.t_available_max,
            double(ctx.win_w), id, ctx.config);

        if (!main_result.can_draw && !preview_result.can_draw) {
            continue;
        }

        GLint first_main = main_result.first;
        GLsizei base_count_main = main_result.count;
        GLint first_prev = preview_result.first;
        GLsizei base_count_prev = preview_result.count;

        double aux_min = 0.0;
        double aux_max = 1.0;
        bool have_aux_range = false;
        bool wants_colormap = false;
        const float area_fill_alpha = ctx.config
            ? static_cast<float>(std::clamp(ctx.config->area_fill_alpha, 0.0, 1.0))
            : 1.0f;

        for (const auto pass_style : pass_styles) {
            if (pass_style == Display_style::COLORMAP_AREA) {
                wants_colormap = true;
                break;
            }
        }

        if (wants_colormap && data->get_aux_metric) {
            const void* identity = data->data_source->identity();
            if (vbo_state.cached_aux_metric_identity != identity) {
                vbo_state.cached_aux_metric_identity = identity;
                vbo_state.cached_aux_metric_sequence = 0;
                vbo_state.has_cached_aux_metric_range = false;
            }

            data_snapshot_t aux_snapshot = data->data_source->snapshot(0);
            if (aux_snapshot &&
                (!vbo_state.has_cached_aux_metric_range ||
                 vbo_state.cached_aux_metric_sequence != aux_snapshot.sequence))
            {
                double range_min = 0.0;
                double range_max = 0.0;
                if (compute_aux_metric_range(*data, aux_snapshot, range_min, range_max)) {
                    vbo_state.cached_aux_metric_min = range_min;
                    vbo_state.cached_aux_metric_max = range_max;
                    vbo_state.cached_aux_metric_sequence = aux_snapshot.sequence;
                    vbo_state.has_cached_aux_metric_range = true;
                }
            }

            if (vbo_state.has_cached_aux_metric_range) {
                aux_min = vbo_state.cached_aux_metric_min;
                aux_max = vbo_state.cached_aux_metric_max;
                have_aux_range = true;
            }
        }

        const auto& pl = ctx.layout;
        const auto to_gl_scissor_y = [&](double top, double height) -> GLint {
            return static_cast<GLint>(lround(double(ctx.win_h) - (top + height)));
        };

        for (const auto pass_style : pass_styles) {
            const shader_set_t* shader_set = shader_set_for_style(pass_style);
            if (!shader_set || shader_set->vert.empty()) {
                continue;
            }

            std::shared_ptr<QOpenGLShaderProgram> sp = get_or_load_shader(*shader_set, ctx.config);
            if (!sp) {
                continue;
            }

            const bool needs_colormap_pipeline = (pass_style == Display_style::COLORMAP_AREA);
            const GLenum drawing_mode = (pass_style == Display_style::DOTS) ? GL_POINTS : GL_LINE_STRIP;

            GLsizei count_main = base_count_main;
            GLsizei count_prev = base_count_prev;

            if (drawing_mode == GL_LINE_STRIP) {
                if (count_main < 2) count_main = 0;
                if (count_prev < 2) count_prev = 0;
            }

            if (count_main == 0 && count_prev == 0) {
                continue;
            }

            sp->bind();

            glm::vec4 uniform_color = data->color;
            if (pass_style == Display_style::AREA || pass_style == Display_style::COLORMAP_AREA) {
                uniform_color.w *= area_fill_alpha;
            }
            if (dark_mode) {
                // Lighten the default blue in dark mode
                if (glm::all(glm::lessThan(glm::abs(uniform_color - glm::vec4(0.16f, 0.45f, 0.64f, 1.0f)), glm::vec4(0.01f)))) {
                    uniform_color = glm::vec4(0.30f, 0.63f, 0.88f, 1.0f);
                }
            }

            glUniform4fv(glGetUniformLocation(sp->programId(), "color"), 1, glm::value_ptr(uniform_color));

            const float volume_min = have_aux_range ? static_cast<float>(aux_min) : 0.0f;
            const float volume_span = have_aux_range ? static_cast<float>(aux_max - aux_min) : 0.0f;
            const float inv_volume_span = (volume_span > 1e-12f) ? (1.0f / volume_span) : 0.0f;
            if (const GLint loc = glGetUniformLocation(sp->programId(), "u_line_px"); loc >= 0) {
                glUniform1f(loc, line_width);
            }
            if (const GLint loc = glGetUniformLocation(sp->programId(), "u_volume_min"); loc >= 0) {
                glUniform1f(loc, volume_min);
            }
            if (const GLint loc = glGetUniformLocation(sp->programId(), "u_inv_volume_span"); loc >= 0) {
                glUniform1f(loc, inv_volume_span);
            }
            if (data->bind_uniforms) {
                data->bind_uniforms(sp->programId());
            }

            if (needs_colormap_pipeline) {
                colormap_series_this_frame.insert(data.get());
                const GLuint bound_colormap_tex = ensure_colormap_texture(*data);
                if (const GLint loc = glGetUniformLocation(sp->programId(), "u_colormap_tex"); loc >= 0) {
                    glUniform1i(loc, 0);
                }
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_1D, bound_colormap_tex);
            }

            glEnable(GL_SCISSOR_TEST);

            if (count_main > 0 && vbo_state.main_view.active_vbo != UINT_MAX) {
                GLuint vao_main = ensure_series_vao(pass_style, vbo_state.main_view.active_vbo, *data);
                glBindVertexArray(vao_main);
                set_common_uniforms(sp->programId(), ctx.pmv, ctx);
                if (needs_colormap_pipeline) {
                    const std::size_t main_scale = scales[main_result.applied_level];
                    const float volume_scale = (main_scale > 0) ? (1.0f / static_cast<float>(main_scale)) : 1.0f;
                    if (const GLint loc = glGetUniformLocation(sp->programId(), "u_volume_scale"); loc >= 0) {
                        glUniform1f(loc, volume_scale);
                    }
                }

                const GLint scissor_y = to_gl_scissor_y(0.0, pl.usable_height);
                glScissor(
                    0,
                    scissor_y,
                    static_cast<GLsizei>(lround(pl.usable_width)),
                    static_cast<GLsizei>(lround(pl.usable_height)));
                if (drawing_mode == GL_LINE_STRIP) {
                    glLineWidth(line_width);
                }
                glDrawArrays(drawing_mode, first_main, count_main);
            }

            if (ctx.snapshot.adjusted_preview_height > 0.0 && count_prev > 0 && vbo_state.preview_view.active_vbo != UINT_MAX) {
                GLuint vao_prev = ensure_series_vao(pass_style, vbo_state.preview_view.active_vbo, *data);
                glBindVertexArray(vao_prev);
                modify_uniforms_for_preview(sp->programId(), ctx);
                if (needs_colormap_pipeline) {
                    const std::size_t preview_scale = scales[preview_result.applied_level];
                    const float volume_scale = (preview_scale > 0) ? (1.0f / static_cast<float>(preview_scale)) : 1.0f;
                    if (const GLint loc = glGetUniformLocation(sp->programId(), "u_volume_scale"); loc >= 0) {
                        glUniform1f(loc, volume_scale);
                    }
                }

                const double preview_top = pl.usable_height + ctx.snapshot.base_label_height_px + constants::k_scissor_pad_px;
                const double preview_height = ctx.snapshot.adjusted_preview_height - constants::k_scissor_pad_px;
                GLint scissor_y = to_gl_scissor_y(preview_top, preview_height);
                GLsizei scissor_h = static_cast<GLsizei>(lround(preview_height));
                if (scissor_h > 0) {
                    glScissor(0, scissor_y, static_cast<GLsizei>(ctx.win_w), scissor_h);
                    if (drawing_mode == GL_LINE_STRIP) {
                        glLineWidth(line_width);
                    }
                    glDrawArrays(drawing_mode, first_prev, count_prev);
                }
            }

            if (drawing_mode == GL_LINE_STRIP) {
                glLineWidth(1.0f);
            }
            glDisable(GL_SCISSOR_TEST);
            glBindVertexArray(0);

            if (needs_colormap_pipeline) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_1D, 0);
            }
        }
    }

    // Clean up colormap textures for inactive series
    for (auto it = m_colormap_textures.begin(); it != m_colormap_textures.end();) {
        if (colormap_series_this_frame.count(it->first) == 0) {
            if (it->second.texture != 0) {
                glDeleteTextures(1, &it->second.texture);
            }
            it = m_colormap_textures.erase(it);
        }
        else {
            ++it;
        }
    }

    // Clean up VBOs for inactive series
    for (auto it = m_vbo_states.begin(); it != m_vbo_states.end();) {
        if (active_ids.find(it->first) == active_ids.end()) {
            auto& state = it->second;
            for (auto* view : {&state.main_view, &state.preview_view}) {
                if (view->id != UINT_MAX) {
                    glDeleteBuffers(1, &view->id);
                }
                view->reset();
            }
            it = m_vbo_states.erase(it);
        }
        else {
            ++it;
        }
    }
}

} // namespace vnm::plot
