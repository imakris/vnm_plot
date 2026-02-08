#include <vnm_plot/core/series_renderer.h>
#include <vnm_plot/core/algo.h>
#include <vnm_plot/core/asset_loader.h>
#include <vnm_plot/core/color_palette.h>
#include <vnm_plot/core/gl_program.h>
#include <vnm_plot/core/constants.h>
#include <vnm_plot/core/default_shaders.h>
#include <vnm_plot/core/plot_config.h>

#include <glatter/glatter.h>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string_view>
#include <unordered_set>

namespace vnm::plot {
using namespace detail;

namespace {

constexpr glm::vec4 k_default_series_color(0.16f, 0.45f, 0.64f, 1.0f);
constexpr glm::vec4 k_default_series_color_dark(0.30f, 0.63f, 0.88f, 1.0f);
constexpr float k_default_color_epsilon = 0.01f;

std::string normalize_asset_name(std::string_view name)
{
    std::string_view out = name;
    if (out.rfind("qrc:/", 0) == 0) {
        out.remove_prefix(5);
    }
    else if (out.rfind(":/", 0) == 0) {
        out.remove_prefix(2);
    }
    if (out.rfind("vnm_plot/", 0) == 0) {
        out.remove_prefix(9);
    }
    return std::string(out);
}

shader_set_t normalize_shader_set(const shader_set_t& shader)
{
    shader_set_t res;
    res.vert = normalize_asset_name(shader.vert);
    res.geom = normalize_asset_name(shader.geom);
    res.frag = normalize_asset_name(shader.frag);
    return res;
}

const shader_set_t& select_series_shader(const series_data_t& series, Display_style style)
{
    auto it = series.shaders.find(style);
    if (it != series.shaders.end() && !it->second.empty()) {
        return it->second;
    }
    if (!series.shader_set.empty()) {
        return series.shader_set;
    }
    return default_shader_for_layout(series.access.layout_key, style);
}

bool is_default_series_color(const glm::vec4& color)
{
    return glm::all(glm::lessThan(
        glm::abs(color - k_default_series_color),
        glm::vec4(k_default_color_epsilon)));
}

bool compute_aux_metric_range(
    const Data_source* data_source,
    const Data_access_policy& access,
    const data_snapshot_t& snapshot,
    double& out_min,
    double& out_max,
    bool& out_used_data_source_range)
{
    out_used_data_source_range = false;

    if (!access.get_aux_metric || !snapshot || snapshot.count == 0 || snapshot.stride == 0) {
        return false;
    }

    if (data_source &&
        data_source->has_aux_metric_range() &&
        !data_source->aux_metric_range_needs_rescan())
    {
        const auto [ds_min, ds_max] = data_source->aux_metric_range();
        if (std::isfinite(ds_min) && std::isfinite(ds_max) && ds_min <= ds_max) {
            out_min = ds_min;
            out_max = ds_max;
            out_used_data_source_range = true;
            return true;
        }
    }

    double min_value = std::numeric_limits<double>::infinity();
    double max_value = -std::numeric_limits<double>::infinity();
    bool have_any = false;

    for (std::size_t i = 0; i < snapshot.count; ++i) {
        const void* sample = snapshot.at(i);
        if (!sample) {
            continue;
        }
        const double value = access.get_aux_metric(sample);
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
            if (view->adjacency_ebo != UINT_MAX) {
                glDeleteBuffers(1, &view->adjacency_ebo);
            }
            view->reset();
        }
    }
    m_vbo_states.clear();

    m_shaders.clear();

    for (auto& [_, resources] : m_colormap_textures) {
        for (auto* resource : {&resources.area, &resources.line}) {
            if (resource->texture != 0) {
                glDeleteTextures(1, &resource->texture);
                resource->texture = 0;
            }
        }
    }
    m_colormap_textures.clear();
    m_logged_errors.clear();
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

GLuint Series_renderer::ensure_colormap_texture(const series_data_t& series, Display_style style)
{
    const bool is_line = (style == Display_style::COLORMAP_LINE);
    const auto& colormap = is_line ? series.colormap_line : series.colormap_area;

    if (colormap.samples.empty()) {
        if (auto it = m_colormap_textures.find(&series); it != m_colormap_textures.end()) {
            auto& resource = is_line ? it->second.line : it->second.area;
            if (resource.texture != 0) {
                glDeleteTextures(1, &resource.texture);
            }
            resource = {};
            if (it->second.area.texture == 0 && it->second.line.texture == 0) {
                m_colormap_textures.erase(it);
            }
        }
        return 0;
    }

    auto& resources = m_colormap_textures[&series];
    auto& resource = is_line ? resources.line : resources.area;

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

    const std::size_t desired_size = colormap.samples.size();
    const uint64_t desired_revision = colormap.revision;
    const bool size_changed = (resource.size != desired_size);
    const bool revision_changed = (resource.revision != desired_revision);

    if (size_changed || revision_changed) {
        if (size_changed) {
            glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA32F, static_cast<GLsizei>(desired_size),
                         0, GL_RGBA, GL_FLOAT, colormap.samples.data());
        }
        else {
            glTexSubImage1D(GL_TEXTURE_1D, 0, 0, static_cast<GLsizei>(desired_size),
                            GL_RGBA, GL_FLOAT, colormap.samples.data());
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
    const Data_access_policy& access)
{
    auto& pipe = pipe_for(style);
    auto& entry = pipe.by_layout[access.layout_key];

    if (entry.vao != 0 && entry.vbo == vbo) {
        return entry.vao;
    }

    if (entry.vao == 0) {
        glGenVertexArrays(1, &entry.vao);
    }

    glBindVertexArray(entry.vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    if (access.setup_vertex_attributes) {
        access.setup_vertex_attributes();
    }
    entry.vbo = vbo;

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return entry.vao;
}

std::shared_ptr<GL_program> Series_renderer::get_or_load_shader(
    const shader_set_t& shader_set,
    const Plot_config* config)
{
    if (shader_set.vert.empty() || !m_asset_loader) {
        return nullptr;
    }

    const shader_set_t normalized = normalize_shader_set(shader_set);
    if (auto found = m_shaders.find(normalized); found != m_shaders.end()) {
        return found->second;
    }

    auto vert_src = m_asset_loader->load(normalized.vert);
    auto frag_src = m_asset_loader->load(normalized.frag);
    std::optional<ByteBuffer> geom_src;
    if (!normalized.geom.empty()) {
        geom_src = m_asset_loader->load(normalized.geom);
    }

    if (!vert_src || !frag_src) {
        if (config && config->log_error) {
            config->log_error("Failed to load shader sources: " + normalized.vert);
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
    m_shaders.emplace(normalized, shared_sp);
    return shared_sp;
}

Series_renderer::view_render_result_t Series_renderer::process_view(
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
    bool skip_gl)
{
    view_render_result_t result;

    if (scales.empty() || t_max <= t_min || width_px <= 0.0) {
        return result;
    }

    if (shared_state.cached_snapshot_frame_id != frame_id) {
        shared_state.cached_snapshot_frame_id = 0;
        shared_state.cached_snapshot_level = SIZE_MAX;
        shared_state.cached_snapshot_source = nullptr;
        shared_state.cached_snapshot = {};
        shared_state.cached_snapshot_hold.reset();
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

    // Populate result from cached stale values when a fresh snapshot is unavailable.
    const auto try_stale_fallback = [&](view_render_result_t& r) -> bool {
        const void* current_identity = data_source.identity();
        const bool identity_ok =
            (view_state.cached_data_identity != nullptr) &&
            (view_state.cached_data_identity == current_identity) &&
            (view_state.active_vbo != UINT_MAX) &&
            (view_state.last_count > 0);
        if (!identity_ok) return false;
        r.can_draw = true;
        r.first = view_state.last_first;
        r.count = view_state.last_count;
        r.applied_level = view_state.last_lod_level;
        r.applied_pps = view_state.last_applied_pps;
        r.use_t_override = view_state.last_use_t_override;
        r.t_min_override = view_state.last_t_min_override;
        r.t_max_override = view_state.last_t_max_override;
        return true;
    };

    const int max_attempts = static_cast<int>(level_count) + 2;

    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        const std::size_t applied_level = std::min<std::size_t>(target_level, max_level_index);
        if (was_tried(applied_level)) {
            break;
        }
        mark_tried(applied_level);
        const std::size_t applied_scale = scales[applied_level];

        const uint64_t current_seq = data_source.current_sequence(applied_level);
        if (current_seq != 0 &&
            current_seq == view_state.last_sequence &&
            applied_level == view_state.last_lod_level &&
            view_state.active_vbo != UINT_MAX &&
            view_state.last_count > 0 &&
            view_state.cached_data_identity == data_source.identity() &&
            view_state.last_t_min == t_min &&
            view_state.last_t_max == t_max &&
            view_state.last_width_px == width_px)
        {
            result.can_draw = true;
            result.first = view_state.last_first;
            result.count = view_state.last_count;
            result.applied_level = applied_level;
            result.applied_pps = view_state.last_applied_pps;
            result.use_t_override = view_state.last_use_t_override;
            result.t_min_override = view_state.last_t_min_override;
            result.t_max_override = view_state.last_t_max_override;
            return result;
        }

        vnm::plot::snapshot_result_t snapshot_result;
        {
            VNM_PLOT_PROFILE_SCOPE(profiler, "process_view.try_snapshot");
            if (shared_state.cached_snapshot_frame_id == frame_id &&
                shared_state.cached_snapshot_level == applied_level &&
                shared_state.cached_snapshot_source == &data_source &&
                shared_state.cached_snapshot)
            {
                snapshot_result.snapshot = shared_state.cached_snapshot;
                snapshot_result.status = snapshot_result_t::Snapshot_status::READY;
            }
            else {
                snapshot_result = data_source.try_snapshot(applied_level);
                if (snapshot_result) {
                    shared_state.cached_snapshot_frame_id = frame_id;
                    shared_state.cached_snapshot_level = applied_level;
                    shared_state.cached_snapshot_source = &data_source;
                    shared_state.cached_snapshot = snapshot_result.snapshot;
                    shared_state.cached_snapshot_hold = snapshot_result.snapshot.hold;
                }
            }
        }

        if (!snapshot_result || !snapshot_result.snapshot || snapshot_result.snapshot.count == 0) {
            ++m_metrics.snapshot_failures;
            if (try_stale_fallback(result)) break;
            if (applied_level > 0) { target_level = applied_level - 1; continue; }
            break;
        }

        const auto& snapshot = snapshot_result.snapshot;

        // Find visible range using binary search
        std::size_t first_idx = 0;
        std::size_t last_idx = snapshot.count;
        double first_ts = 0.0;
        double last_ts = 0.0;
        bool have_ts_bounds = false;
        if (get_timestamp) {
            VNM_PLOT_PROFILE_SCOPE(profiler, "process_view.binary_search");
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
            else if (allow_stale_on_empty && try_stale_fallback(result)) {
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
            ? static_cast<std::size_t>(count) * applied_scale : 0;
        const double base_pps = (base_samples > 0)
            ? width_px / static_cast<double>(base_samples) : 0.0;

        const std::size_t desired_level = choose_lod_level(scales, applied_level, base_pps);
        if (desired_level != applied_level) {
            if (!was_tried(desired_level)) {
                target_level = desired_level;
                continue;
            }
        }

        {
            VNM_PLOT_PROFILE_SCOPE(profiler, "process_view.cpu_prepare");
            if (!skip_gl && view_state.id == UINT_MAX) {
                glGenBuffers(1, &view_state.id);
            }

            const std::size_t needed_bytes = snapshot.count * snapshot.stride;
            const void* current_identity = data_source.identity();
            const bool region_changed = (view_state.last_ring_size < needed_bytes);
            const bool must_upload = region_changed
                || (snapshot.sequence != view_state.last_sequence)
                || (applied_level != view_state.last_lod_level)
                || (current_identity != view_state.cached_data_identity);

            if (must_upload && !skip_gl) {
                glBindBuffer(GL_ARRAY_BUFFER, view_state.id);
                if (region_changed) {
                    const std::size_t alloc_size = needed_bytes + needed_bytes / 4;
                    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(alloc_size), nullptr, GL_DYNAMIC_DRAW);
                    view_state.last_ring_size = alloc_size;
                    ++m_metrics.vbo_reallocations;
                    m_metrics.bytes_allocated += alloc_size;
                }
                if (!snapshot.is_segmented()) {
                    glBufferSubData(GL_ARRAY_BUFFER, 0,
                        static_cast<GLsizeiptr>(snapshot.count * snapshot.stride), snapshot.data);
                }
                else {
                    const std::size_t count1 = snapshot.count1();
                    const std::size_t bytes1 = count1 * snapshot.stride;
                    const std::size_t bytes2 = snapshot.count2 * snapshot.stride;
                    if (bytes1 > 0) {
                        glBufferSubData(GL_ARRAY_BUFFER, 0,
                            static_cast<GLsizeiptr>(bytes1), snapshot.data);
                    }
                    if (bytes2 > 0) {
                        glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(bytes1),
                            static_cast<GLsizeiptr>(bytes2), snapshot.data2);
                    }
                }
                m_metrics.bytes_uploaded += snapshot.count * snapshot.stride;
            }
            if (must_upload) {
                view_state.last_sequence = snapshot.sequence;
                view_state.cached_data_identity = current_identity;
            }
            view_state.last_snapshot_elements = snapshot.count;
        }

        view_state.active_vbo = view_state.id;
        view_state.last_first = static_cast<GLint>(first_idx);
        view_state.last_count = count;

        view_state.last_lod_level = applied_level;
        view_state.last_t_min = t_min;
        view_state.last_t_max = t_max;
        view_state.last_width_px = width_px;

        result.can_draw = true;
        result.first = view_state.last_first;
        result.count = view_state.last_count;
        result.applied_level = applied_level;
        result.applied_pps = base_pps * static_cast<double>(applied_scale);
        view_state.last_applied_pps = result.applied_pps;
        view_state.last_use_t_override = result.use_t_override;
        view_state.last_t_min_override = result.t_min_override;
        view_state.last_t_max_override = result.t_max_override;
        // Cache snapshot for reuse in draw_pass (eliminates redundant snapshot call)
        result.cached_snapshot = snapshot;
        result.cached_snapshot_hold = snapshot.hold;
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
    const std::map<int, std::shared_ptr<const series_data_t>>& series)
{
    if (series.empty() || !m_asset_loader) {
        return;
    }

    const auto& layout = ctx.layout;
    if (layout.usable_width <= 0.0 || layout.usable_height <= 0.0) {
        return;
    }

    // Increment frame counter for snapshot caching
    ++m_frame_id;

    vnm::plot::Profiler* profiler = ctx.config ? ctx.config->profiler.get() : nullptr;
    VNM_PLOT_PROFILE_SCOPE(profiler, "renderer.frame.execute_passes.render_data_series");

    // Skip all GL calls if configured (for pure CPU profiling)
    const bool skip_gl = ctx.config && ctx.config->skip_gl_calls;

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
                if (!skip_gl && view->id != UINT_MAX) {
                    glDeleteBuffers(1, &view->id);
                }
                if (!skip_gl && view->adjacency_ebo != UINT_MAX) {
                    glDeleteBuffers(1, &view->adjacency_ebo);
                }
            }
            it = m_vbo_states.erase(it);
        }
        else {
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
                if (!skip_gl) {
                    for (auto* resource : {&it->second.area, &it->second.line}) {
                        if (resource->texture != 0) {
                            glDeleteTextures(1, &resource->texture);
                        }
                    }
                }
                it = m_colormap_textures.erase(it);
            }
            else {
                ++it;
            }
    }

    if (!skip_gl) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        // Set line width once for all draw passes to avoid repeated glLineWidth calls
        glLineWidth(line_width);
        // Enable scissor test once - we'll only update the rectangle per draw pass
        glEnable(GL_SCISSOR_TEST);
    }

    struct Series_draw_state
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

    std::vector<Series_draw_state> draw_states;
    draw_states.reserve(series.size());

    const double preview_visibility = ctx.config ? ctx.config->preview_visibility : 1.0;
    const bool preview_visible = ctx.adjusted_preview_height > 0.0 && preview_visibility > 0.0;

      enum class Error_cat : uint32_t {
          MISSING_SIGNAL, MISSING_SIGNAL_PREVIEW,
          PREVIEW_MISSING_SOURCE, PREVIEW_INVALID_ACCESS,
          MISSING_SHADER
      };
    const auto log_error_once = [&](Error_cat cat, int series_id,
                                    const std::string& message) {
        if (!ctx.config || !ctx.config->log_error) return;
        const uint64_t key = (static_cast<uint64_t>(cat) << 32)
            | static_cast<uint64_t>(static_cast<uint32_t>(series_id));
        if (m_logged_errors.insert(key).second) {
            ctx.config->log_error(message);
        }
    };

    for (const auto& [id, s] : series) {
        VNM_PLOT_PROFILE_SCOPE(profiler, "renderer.frame.execute_passes.render_data_series.series");
        if (!s || !s->enabled) {
            continue;
        }

        Data_source* main_source = s->main_source();
        if (!main_source) {
            continue;
        }

        const Data_access_policy& main_access = s->main_access();

        Display_style main_style = s->style;
        if (!!(main_style & Display_style::COLORMAP_LINE) && !main_access.get_signal) {
            log_error_once(Error_cat::MISSING_SIGNAL, id,
                "COLORMAP_LINE requires Data_access_policy::get_signal (series "
                    + std::to_string(id) + ")");
            main_style = static_cast<Display_style>(
                static_cast<int>(main_style) & ~static_cast<int>(Display_style::COLORMAP_LINE));
        }
        if (!main_style) {
            continue;
        }

        const bool has_preview_config = s->has_preview_config();
        const bool preview_access_invalid =
            has_preview_config && !s->preview_config->access.is_valid();
        const bool preview_skip_invalid = s->preview_access_invalid_for_source();
        Data_source* preview_source = nullptr;
        const Data_access_policy* preview_access = nullptr;
        Display_style preview_style = static_cast<Display_style>(0);
        bool preview_matches_main = false;
        bool preview_valid = false;

        if (preview_visible) {
            preview_source = s->preview_source();
            preview_access = &s->preview_access();
            preview_style = s->effective_preview_style();
            preview_matches_main = s->preview_matches_main();

            if (has_preview_config && !preview_source) {
                log_error_once(Error_cat::PREVIEW_MISSING_SOURCE, id,
                    "Preview config set but preview data_source is null (series "
                        + std::to_string(id) + ")");
                preview_style = static_cast<Display_style>(0);
            }

            if (preview_access_invalid && preview_source) {
                if (preview_skip_invalid) {
                    log_error_once(Error_cat::PREVIEW_INVALID_ACCESS, id,
                        "Preview access policy invalid; skipping preview for mismatched source (series "
                            + std::to_string(id) + ")");
                    preview_source = nullptr;
                    preview_style = static_cast<Display_style>(0);
                }
                else {
                    log_error_once(Error_cat::PREVIEW_INVALID_ACCESS, id,
                        "Preview access policy invalid; using main access (series "
                            + std::to_string(id) + ")");
                }
            }

            if (!!(preview_style & Display_style::COLORMAP_LINE) &&
                !(preview_access && preview_access->get_signal))
            {
                log_error_once(Error_cat::MISSING_SIGNAL_PREVIEW, id,
                    "COLORMAP_LINE requires Data_access_policy::get_signal (preview series "
                        + std::to_string(id) + ")");
                preview_style = static_cast<Display_style>(
                    static_cast<int>(preview_style) & ~static_cast<int>(Display_style::COLORMAP_LINE));
            }

            if (preview_source && preview_access && !!preview_style) {
                preview_valid = true;
            }
            else {
                preview_source = nullptr;
            }
        }

        auto& vbo_state = m_vbo_states[id];

        std::vector<std::size_t> main_scales = compute_lod_scales(*main_source);
        const void* main_identity = main_source->identity();
        if (vbo_state.cached_aux_metric_identity != main_identity) {
            vbo_state.cached_aux_metric_identity = main_identity;
            vbo_state.cached_aux_metric_levels.clear();
        }
        if (vbo_state.cached_aux_metric_levels.size() != main_scales.size()) {
            vbo_state.cached_aux_metric_levels.assign(main_scales.size(), {});
        }

        std::vector<std::size_t> preview_scales;
        if (preview_valid) {
            if (preview_matches_main) {
                preview_scales = main_scales;
            }
            else {
                preview_scales = compute_lod_scales(*preview_source);
            }
        }

        if (preview_valid && !preview_matches_main) {
            const void* preview_identity = preview_source->identity();
            if (vbo_state.cached_aux_metric_identity_preview != preview_identity) {
                vbo_state.cached_aux_metric_identity_preview = preview_identity;
                vbo_state.cached_aux_metric_levels_preview.clear();
            }
            if (vbo_state.cached_aux_metric_levels_preview.size() != preview_scales.size()) {
                vbo_state.cached_aux_metric_levels_preview.assign(preview_scales.size(), {});
            }
        }
        else {
            vbo_state.cached_aux_metric_identity_preview = nullptr;
            vbo_state.cached_aux_metric_levels_preview.clear();
        }

        // Process main view
        const std::size_t prev_lod_level = vbo_state.main_view.last_lod_level;
        auto main_result = process_view(
            vbo_state.main_view, vbo_state, m_frame_id, *main_source,
            main_access.get_timestamp, main_scales,
            ctx.t0, ctx.t1, layout.usable_width, false, profiler, skip_gl);
        if (ctx.config && ctx.config->log_debug &&
            main_result.can_draw &&
            main_result.applied_level != prev_lod_level)
        {
            std::string message = "LOD selection: series=" + std::to_string(id)
                + " level=" + std::to_string(main_result.applied_level)
                + " pps=" + std::to_string(main_result.applied_pps);
            ctx.config->log_debug(message);
        }

        view_render_result_t preview_result;
        if (preview_visible && preview_valid) {
            preview_result = process_view(
                vbo_state.preview_view, vbo_state, m_frame_id, *preview_source,
                preview_access->get_timestamp, preview_scales,
                ctx.t_available_min, ctx.t_available_max, ctx.win_w,
                true, profiler, skip_gl);
        }

        Series_draw_state draw_state;
        draw_state.id = id;
        draw_state.series = s;
        draw_state.main_source = main_source;
        draw_state.preview_source = preview_source;
        draw_state.main_access = &main_access;
        draw_state.preview_access = preview_access;
        draw_state.main_style = main_style;
        draw_state.preview_style = preview_style;
        draw_state.vbo_state = &vbo_state;
        draw_state.main_scales = std::move(main_scales);
        draw_state.preview_scales = std::move(preview_scales);
        draw_state.main_result = main_result;
        draw_state.preview_result = preview_result;
        draw_state.has_preview = preview_visible && preview_valid;
        draw_state.preview_matches_main = preview_matches_main;
        draw_states.push_back(std::move(draw_state));
    }

    auto draw_pass = [&](Series_draw_state& draw_state,
                         Display_style primitive_style,
                         vbo_view_state_t& view_state,
                         const view_render_result_t& view_result,
                         bool is_preview) {
        const GLsizei count = view_result.count;
        if (count <= 0) {
            return;
        }

        const bool use_adjacency =
            (primitive_style == Display_style::LINE) ||
            (primitive_style == Display_style::AREA) ||
            (primitive_style == Display_style::COLORMAP_AREA) ||
            (primitive_style == Display_style::COLORMAP_LINE);
        const GLenum drawing_mode = (primitive_style == Display_style::DOTS)
            ? GL_POINTS
            : (use_adjacency ? GL_LINE_STRIP_ADJACENCY : GL_LINE_STRIP);
        if (drawing_mode != GL_POINTS && count < 2) {
            return;
        }

        const series_data_t& series = *draw_state.series;
        auto& vbo_state = *draw_state.vbo_state;
        Data_source* data_source = is_preview
            ? draw_state.preview_source
            : draw_state.main_source;
        const Data_access_policy* access = is_preview ? draw_state.preview_access : draw_state.main_access;
        const auto& scales = is_preview ? draw_state.preview_scales : draw_state.main_scales;
        const bool use_preview_cache = is_preview && !draw_state.preview_matches_main;

        if (!data_source || !access) {
            return;
        }

        // CPU-side color/uniform preparation (no GL calls)
        glm::vec4 draw_color;
        glm::vec4 line_col;
        {
            draw_color = series.color;
            if (primitive_style == Display_style::AREA || primitive_style == Display_style::COLORMAP_AREA) {
                draw_color.w *= area_fill_alpha;
            }
            if (dark_mode) {
                if (is_default_series_color(draw_color)) {
                    draw_color = k_default_series_color_dark;
                }
            }
            line_col = series.color;
            if (dark_mode && is_default_series_color(line_col)) {
                line_col = k_default_series_color_dark;
            }
            // Apply preview visibility alpha
            if (is_preview) {
                const float pv = static_cast<float>(preview_visibility);
                draw_color.w *= pv;
                line_col.w *= pv;
            }
        }

        // CPU-side colormap aux-range computation (must run before skip_gl return)
        const vbo_state_t::aux_metric_cache_t* aux_cache = nullptr;
        std::size_t aux_range_scale = 1;
        if (primitive_style == Display_style::COLORMAP_AREA && !series.colormap_area.samples.empty()) {
            auto& aux_cache_levels = use_preview_cache
                ? vbo_state.cached_aux_metric_levels_preview
                : vbo_state.cached_aux_metric_levels;
            if (view_result.applied_level >= aux_cache_levels.size()) {
                return;
            }
            auto& aux_cache_entry = aux_cache_levels[view_result.applied_level];
            bool has_any_aux_cache = false;
            for (const auto& entry : aux_cache_levels) {
                if (entry.valid) {
                    has_any_aux_cache = true;
                    break;
                }
            }
            // Reuse snapshot from process_view() instead of taking a redundant one.
            // If we don't have a cached snapshot and no aux cache exists yet, grab one.
            data_snapshot_t snapshot = view_result.cached_snapshot;
            if (!snapshot && !has_any_aux_cache) {
                if (vbo_state.cached_snapshot_frame_id == m_frame_id &&
                    vbo_state.cached_snapshot_level == view_result.applied_level &&
                    vbo_state.cached_snapshot_source == data_source &&
                    vbo_state.cached_snapshot)
                {
                    snapshot = vbo_state.cached_snapshot;
                }
                else
                if (data_source) {
                    auto snapshot_result = data_source->try_snapshot(view_result.applied_level);
                    if (snapshot_result) {
                        snapshot = snapshot_result.snapshot;
                        vbo_state.cached_snapshot_frame_id = m_frame_id;
                        vbo_state.cached_snapshot_level = view_result.applied_level;
                        vbo_state.cached_snapshot_source = data_source;
                        vbo_state.cached_snapshot = snapshot_result.snapshot;
                        vbo_state.cached_snapshot_hold = snapshot_result.snapshot.hold;
                    }
                }
            }
            if (snapshot) {
                const bool can_reuse =
                    aux_cache_entry.valid && aux_cache_entry.sequence == snapshot.sequence;

                if (!can_reuse) {
                    double aux_min = 0.0;
                    double aux_max = 1.0;
                    bool used_data_source_range = false;
                    if (compute_aux_metric_range(
                            data_source,
                            *access,
                            snapshot,
                            aux_min,
                            aux_max,
                            used_data_source_range)) {
                        std::size_t cache_level = view_result.applied_level;
                        uint64_t cache_sequence = snapshot.sequence;
                        if (used_data_source_range) {
                            // Data-source ranges are in LOD 0 units.
                            cache_level = 0;
                            cache_sequence = 0;
                            if (data_source) {
                                cache_sequence = data_source->current_sequence(0);
                            }
                            if (cache_sequence == 0) {
                                // If we can't get LOD 0 sequence, try to get it from a snapshot.
                                // This fallback is necessary when current_sequence() is not implemented.
                                if (view_result.applied_level == 0) {
                                    // We're already at LOD 0, so snapshot sequence is correct.
                                    cache_sequence = snapshot.sequence;
                                } else {
                                    // We need LOD 0 sequence but have a different LOD snapshot.
                                    // Try to get LOD 0 snapshot for correct sequence tracking.
                                    auto lod0_snapshot = data_source->try_snapshot(0);
                                    if (lod0_snapshot) {
                                        cache_sequence = lod0_snapshot.snapshot.sequence;
                                    } else {
                                        // Can't get reliable LOD 0 sequence - use applied level
                                        // sequence and don't cache at LOD 0 to avoid staleness.
                                        cache_level = view_result.applied_level;
                                        cache_sequence = snapshot.sequence;
                                    }
                                }
                            }
                        }
                        auto& target_entry = aux_cache_levels[cache_level];
                        target_entry.min = aux_min;
                        target_entry.max = aux_max;
                        target_entry.valid = true;
                        target_entry.sequence = cache_sequence;
                        if (used_data_source_range && cache_level != view_result.applied_level) {
                            aux_cache_entry.min = aux_min;
                            aux_cache_entry.max = aux_max;
                            aux_cache_entry.valid = true;
                            aux_cache_entry.sequence = snapshot.sequence;
                        }
                    }
                    else {
                        if (!aux_cache_entry.valid) {
                            aux_cache_entry.min = 0.0;
                            aux_cache_entry.max = 1.0;
                            aux_cache_entry.valid = true;
                        }
                    }
                    if (!used_data_source_range) {
                        aux_cache_entry.sequence = snapshot.sequence;
                    }
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
            for (std::size_t i = 0; i < aux_cache_levels.size(); ++i) {
                if (aux_cache_levels[i].valid) {
                    aux_range_level = i;
                    break;
                }
            }
            aux_cache = &aux_cache_levels[aux_range_level];
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

        const shader_set_t& shader_set = select_series_shader(series, primitive_style);
        if (shader_set.empty()) {
            log_error_once(Error_cat::MISSING_SHADER, draw_state.id,
                "Missing shader set for series " + std::to_string(draw_state.id)
                    + " (layout_key=" + std::to_string(series.access.layout_key) + ")");
            return;
        }
        auto pass_shader = get_or_load_shader(shader_set, ctx.config);
        if (!pass_shader) {
            return;
        }

        glUseProgram(pass_shader->program_id());
        set_common_uniforms(*pass_shader, ctx.pmv, ctx);
        if (is_preview) {
            modify_uniforms_for_preview(*pass_shader, ctx);
            if (view_result.use_t_override) {
                glUniform1d(pass_shader->uniform_location("t_min"), view_result.t_min_override);
                glUniform1d(pass_shader->uniform_location("t_max"), view_result.t_max_override);
            }
        }

        glUniform4fv(pass_shader->uniform_location("color"), 1, glm::value_ptr(draw_color));
        if (const GLint loc = pass_shader->uniform_location("u_line_px"); loc >= 0) {
            glUniform1f(loc, line_width);
        }
        if (access->bind_uniforms) {
            access->bind_uniforms(pass_shader->program_id());
        }

        // Handle colormap GL setup for colormap styles
        GLuint colormap_tex = 0;
        if (primitive_style == Display_style::COLORMAP_AREA ||
            primitive_style == Display_style::COLORMAP_LINE) {
            colormap_tex = ensure_colormap_texture(series, primitive_style);
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
                // Set color_multiplier for preview fading
                if (const GLint loc = pass_shader->uniform_location("color_multiplier"); loc >= 0) {
                    const float pv = is_preview ? static_cast<float>(preview_visibility) : 1.0f;
                    glUniform4f(loc, 1.0f, 1.0f, 1.0f, pv);
                }
            }
        }

        glBindVertexArray(ensure_series_vao(primitive_style, view_state.active_vbo, *access));

        if (use_adjacency) {
            const std::size_t required_indices = static_cast<std::size_t>(count) + 2;
            bool needs_upload =
                (view_state.adjacency_last_first != view_result.first) ||
                (view_state.adjacency_last_count != count);

            if (view_state.adjacency_ebo == UINT_MAX) {
                glGenBuffers(1, &view_state.adjacency_ebo);
                view_state.adjacency_ebo_capacity = 0;
                needs_upload = true;
            }

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, view_state.adjacency_ebo);

            if (view_state.adjacency_ebo_capacity < required_indices) {
                glBufferData(
                    GL_ELEMENT_ARRAY_BUFFER,
                    static_cast<GLsizeiptr>(required_indices * sizeof(GLuint)),
                    nullptr,
                    GL_DYNAMIC_DRAW);
                view_state.adjacency_ebo_capacity = required_indices;
                needs_upload = true;
            }

            if (needs_upload) {
                std::vector<GLuint> indices(required_indices);
                const GLuint first = static_cast<GLuint>(view_result.first);
                indices[0] = first;
                for (GLsizei i = 0; i < count; ++i) {
                    indices[static_cast<std::size_t>(i + 1)] = first + static_cast<GLuint>(i);
                }
                indices[required_indices - 1] = first + static_cast<GLuint>(count - 1);

                glBufferSubData(
                    GL_ELEMENT_ARRAY_BUFFER,
                    0,
                    static_cast<GLsizeiptr>(required_indices * sizeof(GLuint)),
                    indices.data());
                view_state.adjacency_last_first = view_result.first;
                view_state.adjacency_last_count = count;
            }
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
            if (use_adjacency) {
                const GLsizei adjacency_count = count + 2;
                glDrawElements(drawing_mode, adjacency_count, GL_UNSIGNED_INT, nullptr);
            }
            else {
                glDrawArrays(drawing_mode, view_result.first, count);
            }
        }

        // Note: VAO unbinding moved to cleanup section to avoid per-draw overhead

        if (colormap_tex != 0) {
            glBindTexture(GL_TEXTURE_1D, 0);
        }
    };

    auto draw_group = [&](Display_style primitive_style, bool is_preview) {
        for (auto& draw_state : draw_states) {
            if (!draw_state.series || !draw_state.vbo_state) {
                continue;
            }
            const Display_style view_style = is_preview
                ? draw_state.preview_style
                : draw_state.main_style;
            if (!(view_style & primitive_style)) {
                continue;
            }
            if (is_preview && !draw_state.has_preview) {
                continue;
            }
            const auto& view_result = is_preview ? draw_state.preview_result : draw_state.main_result;
            if (!view_result.can_draw) {
                continue;
            }
            auto& view_state = is_preview
                ? draw_state.vbo_state->preview_view
                : draw_state.vbo_state->main_view;
            draw_pass(draw_state, primitive_style, view_state, view_result, is_preview);
        }
    };

    draw_group(Display_style::COLORMAP_AREA, false);
    draw_group(Display_style::AREA, false);
    draw_group(Display_style::COLORMAP_LINE, false);
    draw_group(Display_style::LINE, false);
    draw_group(Display_style::DOTS, false);

    if (preview_visible) {
        draw_group(Display_style::COLORMAP_AREA, true);
        draw_group(Display_style::AREA, true);
        draw_group(Display_style::COLORMAP_LINE, true);
        draw_group(Display_style::LINE, true);
        draw_group(Display_style::DOTS, true);
    }

    if (!skip_gl) {
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
