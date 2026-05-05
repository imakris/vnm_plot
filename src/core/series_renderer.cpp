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

// VNM_PLOT_HAS_QRHI is defined by CMake when this TU links Qt6::GuiPrivate.
#ifdef VNM_PLOT_HAS_QRHI
#  include <rhi/qrhi.h>
#  include <QFile>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>
#include <unordered_set>

namespace vnm::plot {
using detail::choose_lod_level;
using detail::choose_origin_ns;
using detail::compute_lod_scales;
using detail::k_scissor_pad_px;
using detail::lower_bound_timestamp;
using detail::upper_bound_timestamp;

namespace {

constexpr glm::vec4 k_default_series_color(0.16f, 0.45f, 0.64f, 1.0f);
constexpr glm::vec4 k_default_series_color_dark(0.30f, 0.63f, 0.88f, 1.0f);
constexpr float k_default_color_epsilon = 0.01f;

bool to_glint_rounded(double value, GLint& out)
{
    if (!std::isfinite(value)) {
        return false;
    }

    out = static_cast<GLint>(lround(value));
    return true;
}

bool to_positive_glsizei(double value, GLsizei& out)
{
    if (!std::isfinite(value)) {
        return false;
    }

    const long rounded = lround(value);
    if (rounded <= 0) {
        return false;
    }

    out = static_cast<GLsizei>(rounded);
    return true;
}

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

    if (!access.get_aux_metric || !snapshot.is_valid()) {
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

// Per-view RHI resources held off-line so the public header forward-
// declares the type. The unique_ptr<rhi_buffers_t> in vbo_view_state_t
// pulls in QRhiBuffer transitively through this struct's members; under
// the GL fallback build (no QRhi) the rhi_buffers_t is never instantiated
// so its definition collapses to an empty placeholder.
//
// The SRB is per-view because it captures concrete buffer handles. The
// pipeline state object stays cached in rhi_state_t (its descriptor only
// depends on layout, not on which buffer the SRB references). When a
// buffer is reallocated to grow capacity, the SRB's last_* pointers no
// longer match and the SRB is rebuilt before the next draw.
#ifdef VNM_PLOT_HAS_QRHI
struct Series_renderer::vbo_view_state_t::rhi_buffers_t
{
    std::unique_ptr<QRhiBuffer> vbo;
    std::unique_ptr<QRhiBuffer> ubo;
    // LINE-specific per-frame buffer that holds the active sample window
    // padded with leading and trailing duplicates. Bound four times as a
    // vertex buffer (offsets 0, 16, 32, 48; stride 16; per-instance) so the
    // vertex shader receives prev/p0/p1/next as plain attributes. Vertex
    // attributes side-step the SM 5.0 UAV restriction that blocks SSBOs in
    // the D3D11 vertex stage.
    std::unique_ptr<QRhiBuffer> line_window_vbo;

    // Per-(view, primitive_style/pass) UBO + SRB cache. Each drawable primitive
    // needs an independent UBO because every resource update is submitted
    // before the render pass starts; combined Display_style values would
    // otherwise overwrite the previously prepared uniform bytes before the
    // matching draw records.
    struct srb_entry_t
    {
        std::unique_ptr<QRhiBuffer> ubo;
        std::unique_ptr<QRhiShaderResourceBindings> srb;
        QRhiBuffer* last_ubo = nullptr;
        std::size_t ubo_capacity_bytes = 0;
    };
    srb_entry_t dots_srb;
    srb_entry_t line_srb;
    srb_entry_t area_fill_srb;
    srb_entry_t area_axis_srb;
};
#else
struct Series_renderer::vbo_view_state_t::rhi_buffers_t {};
#endif

Series_renderer::vbo_view_state_t::vbo_view_state_t() = default;
Series_renderer::vbo_view_state_t::~vbo_view_state_t() = default;
Series_renderer::vbo_view_state_t::vbo_view_state_t(vbo_view_state_t&&) noexcept = default;
Series_renderer::vbo_view_state_t&
Series_renderer::vbo_view_state_t::operator=(vbo_view_state_t&&) noexcept = default;

void Series_renderer::vbo_view_state_t::reset()
{
    *this = vbo_view_state_t{};
}

// -----------------------------------------------------------------------------
// RHI state: pipelines, shaders, and the global UBO mirror.
// -----------------------------------------------------------------------------
//
// Each Display_style maps to one cached QRhiGraphicsPipeline. The pipeline
// descriptor only depends on the binding LAYOUT (which stages reference each
// slot, and the vertex-input layout), not on which buffers fill those slots,
// so one pipeline is shared across every series. The QRhiShaderResourceBindings
// that actually reference per-series buffers live on the per-view rhi_buffers_t
// so each series gets its own SRB.

struct Series_renderer::rhi_state_t
{
    enum class pipeline_kind_t : uint32_t
    {
        DOTS = 0,
        LINE = 1,
        AREA = 2
    };

    struct pipeline_key_t
    {
        pipeline_kind_t kind;

        bool operator==(const pipeline_key_t& o) const noexcept
        {
            return kind == o.kind;
        }
    };

    struct pipeline_key_hash_t
    {
        std::size_t operator()(const pipeline_key_t& k) const noexcept
        {
            return static_cast<std::size_t>(k.kind);
        }
    };

#ifdef VNM_PLOT_HAS_QRHI
    struct rhi_pipeline_t
    {
        std::unique_ptr<QRhiGraphicsPipeline> pipeline;
        QShader vert;
        QShader frag;
        // Render-pass descriptor captured at pipeline creation. If the host's
        // current render target carries a different descriptor (e.g. resize
        // recreated the FBO with a different color format or sample count),
        // the cached pipeline is no longer compatible and is rebuilt.
        QRhiRenderPassDescriptor* last_rpd = nullptr;
        int                       last_sample_count = 1;
    };

    std::unordered_map<pipeline_key_t, rhi_pipeline_t, pipeline_key_hash_t> pipelines;
    QShader cached_dot_vert;
    QShader cached_dot_frag;
    QShader cached_line_vert;
    QShader cached_line_frag;
    QShader cached_area_vert;
    QShader cached_area_frag;
    bool    shaders_loaded = false;

    QRhi*                last_rhi             = nullptr;
    QRhiResourceUpdateBatch* pending_updates  = nullptr;
    // Render target captured at the start of Series_renderer::render. The
    // pipeline state object needs the render-pass descriptor and sample
    // count from the active target, and QRhiCommandBuffer does not expose a
    // public accessor for it — Plot_renderer hands it over instead.
    QRhiRenderTarget*    last_render_target   = nullptr;

    // Per-frame draw plan computed in prepare() (under RHI) and replayed in
    // render(). Stays empty under the GL fallback path; render() builds it
    // on the fly in that case so tests that call render() standalone keep
    // working. The vector lives on the renderer rather than in a stack
    // frame because the prepare() / render() split happens across two host
    // calls, with cb->beginPass(batch) sandwiched in between. The fp32
    // origins computed in prepare() ride inside the per-view UBO/staging
    // bytes already submitted to the resource-update batch, so they do
    // not need to be cached here for render() to read back.
    std::vector<series_draw_state_t> frame_draw_states;
    bool         frame_preview_visible   = false;
    // True if prepare() filled this plan. Reset after render() consumes it
    // so a stray render() without a matching prepare() is a no-op for the
    // RHI draws (the GL fallback path runs the plan-build inline).
    bool         frame_plan_ready        = false;
#endif
};

// -----------------------------------------------------------------------------
// std140 UBO mirror for the QSB shader uniform_blocks.glsl::Series_view_t,
// plus the per-shader trailing values consumed by plot_dot_quad, plot_line,
// and plot_area.
// -----------------------------------------------------------------------------
//
// The shaders declare:
//
//     layout(std140, binding = 0) uniform Block {
//         Series_view_t view;        // 128 bytes
//         float         line_px;     // for LINE
//         int           snap;        // for LINE
//         float         dot_px;      // for DOTS
//         vec4          axis_color;  // for AREA
//         int           axis_pass;   // for AREA
//     } u;
//
// std140 requires:
//   - mat4 aligns to a vec4 (16 bytes)
//   - vec4 aligns to 16 bytes
//   - scalar floats and ints align to 4 bytes
//   - the structure as a whole aligns to a vec4
//
// Series_view_t layout (each row is 16 bytes):
//   offset 0   pmv      (mat4 = 4 vec4 = 64 bytes)
//   offset 64  color    (vec4 = 16)
//   offset 80  t_min    (float)
//   offset 84  t_max    (float)
//   offset 88  v_min    (float)
//   offset 92  v_max    (float)
//   offset 96  width    (float)
//   offset 100 height   (float)
//   offset 104 y_offset (float)
//   offset 108 win_h    (float)
//   offset 112 framebuffer_y_up (int)
//   total: 128 bytes
//
// Per-shader trailing fields sit just past the view, padded out to vec4 to
// satisfy std140's structure-trailing rule. The renderer allocates enough UBO
// bytes for the largest block and uploads the concrete block size needed by
// each primitive.
struct Series_view_std140
{
    float pmv[16];      // offset   0
    float color[4];     // offset  64
    float t_min;        // offset  80
    float t_max;        // offset  84
    float v_min;        // offset  88
    float v_max;        // offset  92
    float width;        // offset  96
    float height;       // offset 100
    float y_offset;     // offset 104
    float win_h;        // offset 108
    int32_t framebuffer_y_up; // offset 112
    float _pad0[3];     // offset 116
};
static_assert(offsetof(Series_view_std140, pmv)      ==   0, "Series_view_t std140 pmv offset");
static_assert(offsetof(Series_view_std140, color)    ==  64, "Series_view_t std140 color offset");
static_assert(offsetof(Series_view_std140, t_min)    ==  80, "Series_view_t std140 t_min offset");
static_assert(offsetof(Series_view_std140, t_max)    ==  84, "Series_view_t std140 t_max offset");
static_assert(offsetof(Series_view_std140, v_min)    ==  88, "Series_view_t std140 v_min offset");
static_assert(offsetof(Series_view_std140, v_max)    ==  92, "Series_view_t std140 v_max offset");
static_assert(offsetof(Series_view_std140, width)    ==  96, "Series_view_t std140 width offset");
static_assert(offsetof(Series_view_std140, height)   == 100, "Series_view_t std140 height offset");
static_assert(offsetof(Series_view_std140, y_offset) == 104, "Series_view_t std140 y_offset offset");
static_assert(offsetof(Series_view_std140, win_h)    == 108, "Series_view_t std140 win_h offset");
static_assert(offsetof(Series_view_std140, framebuffer_y_up) == 112, "Series_view_t framebuffer_y_up offset");
static_assert(sizeof(Series_view_std140)            == 128, "Series_view_t std140 size");

// Whole-block layout: Series_view + LINE trailing (line_px, snap_to_pixels).
// Padded out to a 16-byte multiple so the host-side struct mirrors the
// vec4-aligned trailing element rule std140 enforces on the GLSL block.
struct Line_block_std140
{
    Series_view_std140 view;          // offset 0
    float              line_px;       // offset 128
    int                snap_to_pixels;// offset 132
    float              _pad0;         // offset 136
    float              _pad1;         // offset 140
};
static_assert(sizeof(Line_block_std140) == 144, "Line_block_std140 must be a multiple of 16");
static_assert(offsetof(Line_block_std140, line_px)        == 128, "Line_block line_px offset");
static_assert(offsetof(Line_block_std140, snap_to_pixels) == 132, "Line_block snap_to_pixels offset");

// Whole-block layout: Series_view + DOTS trailing (point_diameter_px).
// Padded out to 144 bytes for the same reason as Line_block_std140.
struct Dot_block_std140
{
    Series_view_std140 view;              // offset 0
    float              point_diameter_px; // offset 128
    float              _pad0;             // offset 132
    float              _pad1;             // offset 136
    float              _pad2;             // offset 140
};
static_assert(sizeof(Dot_block_std140) == 144, "Dot_block_std140 must be a multiple of 16");
static_assert(offsetof(Dot_block_std140, point_diameter_px) == 128, "Dot_block point_diameter_px offset");

// Whole-block layout: Series_view + AREA trailing (zero_axis_color,
// axis_pass). Padded out to a 16-byte multiple.
struct Area_block_std140
{
    Series_view_std140 view;                // offset 0
    float              zero_axis_color[4];  // offset 128
    int                axis_pass;           // offset 144
    float              _pad0;               // offset 148
    float              _pad1;               // offset 152
    float              _pad2;               // offset 156
};
static_assert(sizeof(Area_block_std140) == 160, "Area_block_std140 must be a multiple of 16");
static_assert(offsetof(Area_block_std140, zero_axis_color) == 128, "Area_block zero_axis_color offset");
static_assert(offsetof(Area_block_std140, axis_pass)       == 144, "Area_block axis_pass offset");

constexpr std::uint32_t k_series_ubo_bytes = 160;
static_assert(sizeof(Line_block_std140) <= k_series_ubo_bytes, "ubo bytes fit LINE block");
static_assert(sizeof(Dot_block_std140)  <= k_series_ubo_bytes, "ubo bytes fit DOTS block");
static_assert(sizeof(Area_block_std140) == k_series_ubo_bytes, "ubo bytes match AREA block");

#ifdef VNM_PLOT_HAS_QRHI
// Load a baked .qsb artifact from the embedded Qt resource produced by
// qt_add_shaders. The PREFIX is /vnm_plot in CMakeLists.txt; the BASE is
// the project root, so an input shaders/qsb/foo.vert ends up at
// :/vnm_plot/shaders/qsb/foo.vert.qsb.
static QShader load_qsb(const char* alias)
{
    QFile file(QStringLiteral(":/vnm_plot/shaders/qsb/") + QString::fromLatin1(alias));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QShader::fromSerialized(file.readAll());
}
#endif

Series_renderer::Series_renderer()
{
    m_pipe_line = std::make_unique<series_pipe_t>();
    m_pipe_dots = std::make_unique<series_pipe_t>();
    m_pipe_area = std::make_unique<series_pipe_t>();
    m_pipe_colormap = std::make_unique<series_pipe_t>();
    m_rhi_state = std::make_unique<rhi_state_t>();
}

Series_renderer::~Series_renderer() = default;

void Series_renderer::initialize(Asset_loader& asset_loader)
{
    m_asset_loader = &asset_loader;
}

void Series_renderer::cleanup_gl_resources()
{
    clear_frame_snapshot_caches();

    for (auto* pipe : {m_pipe_line.get(), m_pipe_dots.get(), m_pipe_area.get(), m_pipe_colormap.get()}) {
        if (!pipe) {
            continue;
        }
        for (auto& [_, entry] : pipe->by_layout) {
            entry.vbo = 0;
        }
        pipe->by_layout.clear();
    }

    for (auto& [_, vao] : m_gl_vaos) {
        if (vao != 0) {
            glDeleteVertexArrays(1, &vao);
        }
    }
    m_gl_vaos.clear();

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

#ifdef VNM_PLOT_HAS_QRHI
    m_rhi_state->pipelines.clear();
    m_rhi_state->shaders_loaded = false;
    m_rhi_state->cached_dot_vert = {};
    m_rhi_state->cached_dot_frag = {};
    m_rhi_state->cached_line_vert = {};
    m_rhi_state->cached_line_frag = {};
    m_rhi_state->cached_area_vert = {};
    m_rhi_state->cached_area_frag = {};
    m_rhi_state->last_rhi = nullptr;
    m_rhi_state->pending_updates = nullptr;
    m_rhi_state->frame_draw_states.clear();
    m_rhi_state->frame_plan_ready = false;
#endif
}

void Series_renderer::clear_frame_snapshot_caches()
{
    for (auto& [_, state] : m_vbo_states) {
        state.cached_snapshot_frame_id = 0;
        state.cached_snapshot_level = SIZE_MAX;
        state.cached_snapshot_source = nullptr;
        state.cached_snapshot = {};
        state.cached_snapshot_hold.reset();
    }
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

GLuint Series_renderer::ensure_gl_series_vao(
    Display_style style,
    GLuint vbo)
{
    auto& pipe = pipe_for(style);
    // The VBO holds a fixed gpu_sample_t layout. The GL fallback path creates
    // one VAO per (pipe, vbo) pair and stores it in m_gl_vaos so the entry_t
    // tracks only the bound vbo handle (per the C1 type contract that VAOs
    // are not part of series_pipe_t::entry_t).
    pipe.by_layout[0].vbo = vbo;

    const gl_vao_key_t key{&pipe, vbo};
    auto it = m_gl_vaos.find(key);
    if (it != m_gl_vaos.end()) {
        glBindVertexArray(it->second);
        return it->second;
    }

    GLuint vao = 0;
    glGenVertexArrays(1, &vao);
    m_gl_vaos.emplace(key, vao);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    constexpr GLsizei k_stride            = static_cast<GLsizei>(sizeof(gpu_sample_t));
    constexpr std::size_t k_offset_t_rel  = offsetof(gpu_sample_t, t_rel);
    constexpr std::size_t k_offset_y      = offsetof(gpu_sample_t, y);

    // DOTS expands each sample into a four-vertex quad via instancing. AREA
    // needs sample i and sample i+1 per instance, fed through two attribute
    // bindings on the same VBO with location 4/5 offset by one gpu_sample_t
    // stride. Both styles set divisor=1 so the GPU's attribute fetcher does
    // the per-instance lookup. LINE and the colormap styles pull samples
    // through an SSBO and ignore the vertex attributes.
    if (!!(style & Display_style::DOTS)) {
        glVertexAttribPointer(
            0, 1, GL_FLOAT, GL_FALSE, k_stride,
            reinterpret_cast<void*>(k_offset_t_rel));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(
            1, 1, GL_FLOAT, GL_FALSE, k_stride,
            reinterpret_cast<void*>(k_offset_y));
        glEnableVertexAttribArray(1);
        // The DOTS shader does not consume locations 2/3 (y_min/y_max).
        glDisableVertexAttribArray(2);
        glDisableVertexAttribArray(3);

        constexpr GLuint k_dot_attrib_locations[] = {0u, 1u};
        for (GLuint loc : k_dot_attrib_locations) {
            glVertexAttribDivisor(loc, 1u);
        }
    }
    else
    if (!!(style & Display_style::AREA)) {
        // p0 = sample i, p1 = sample i+1. Same VBO, locations 4/5 shifted
        // by one gpu_sample_t so the per-instance fetch reads the next
        // sample without shader-side indexing.
        glVertexAttribPointer(
            0, 1, GL_FLOAT, GL_FALSE, k_stride,
            reinterpret_cast<void*>(k_offset_t_rel));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(
            1, 1, GL_FLOAT, GL_FALSE, k_stride,
            reinterpret_cast<void*>(k_offset_y));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(
            4, 1, GL_FLOAT, GL_FALSE, k_stride,
            reinterpret_cast<void*>(sizeof(gpu_sample_t) + k_offset_t_rel));
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(
            5, 1, GL_FLOAT, GL_FALSE, k_stride,
            reinterpret_cast<void*>(sizeof(gpu_sample_t) + k_offset_y));
        glEnableVertexAttribArray(5);
        // The AREA shader does not consume locations 2/3 (y_min/y_max).
        glDisableVertexAttribArray(2);
        glDisableVertexAttribArray(3);

        constexpr GLuint k_area_attrib_locations[] = {0u, 1u, 4u, 5u};
        for (GLuint loc : k_area_attrib_locations) {
            glVertexAttribDivisor(loc, 1u);
        }
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return vao;
}

std::shared_ptr<GL_program> Series_renderer::get_or_load_shader(
    const shader_set_t& shader_set,
    const Plot_config* config)
{
    const auto log_error = [&](const std::string& message) {
        if (config && config->log_error) {
            config->log_error(message);
        }
    };

    if (!m_asset_loader) {
        log_error("Series_renderer: asset loader not initialized; cannot compile shaders");
        return nullptr;
    }
    if (shader_set.vert.empty()) {
        log_error("Series_renderer: shader set has no vertex stage; cannot compile program");
        return nullptr;
    }

    const shader_set_t normalized = normalize_shader_set(shader_set);
    if (auto found = m_shaders.find(normalized); found != m_shaders.end()) {
        return found->second;
    }

    auto vert_src = m_asset_loader->load(normalized.vert);
    auto frag_src = m_asset_loader->load(normalized.frag);

    if (!vert_src || !frag_src) {
        log_error("Failed to load shader sources: " + normalized.vert);
        return nullptr;
    }

    std::string vert_str(vert_src->begin(), vert_src->end());
    std::string frag_str(frag_src->begin(), frag_src->end());

    auto log_error_fn = config ? config->log_error : std::function<void(const std::string&)>();
    auto sp = create_gl_program(vert_str, frag_str, log_error_fn);

    if (!sp) {
        log_error("Shader program creation failed for: " + normalized.vert);
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
    QRhiResourceUpdateBatch* rhi_updates)
{
#ifndef VNM_PLOT_HAS_QRHI
    (void)rhi;
    (void)rhi_updates;
#endif
    // The RHI upload path runs whenever a QRhi/batch is bound. skip_gl is a
    // "do not issue gl* calls" knob, not a "do not render" knob; under any
    // RHI backend the host sets skip_gl so the GL fallback stays silent, but
    // the RHI staging upload must still run or series primitives draw
    // against an empty vertex buffer.
    const bool use_rhi_uploads = (rhi != nullptr) && (rhi_updates != nullptr);
    view_render_result_t result;
    const auto& get_timestamp = access.get_timestamp;

    if (scales.empty() || t_max_ns <= t_min_ns || width_px <= 0.0) {
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
    const auto load_cached_result = [&](view_render_result_t& r, std::size_t level) {
        r.can_draw = true;
        r.first = view_state.last_first;
        r.count = view_state.last_count;
        r.applied_level = level;
        r.applied_pps = view_state.last_applied_pps;
    };
    const auto try_stale_fallback = [&](view_render_result_t& r) -> bool {
        const void* current_identity = data_source.identity();
        // The cached VBO holds samples rebased against uploaded_t_origin_ns;
        // reusing it under a moved origin would draw at the wrong x positions
        // because set_common_uniforms feeds the new view_origin_ns regardless.
        const bool identity_ok =
            (view_state.cached_data_identity != nullptr) &&
            (view_state.cached_data_identity == current_identity) &&
            (view_state.active_vbo != UINT_MAX) &&
            (view_state.last_count > 0) &&
            (view_state.last_empty_window_behavior == empty_window_behavior) &&
            (view_state.uploaded_t_origin_ns == t_origin_ns);
        if (!identity_ok) {
            return false;
        }
        load_cached_result(r, view_state.last_lod_level);
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
        bool hold_last_forward = false;

        const uint64_t current_seq = data_source.current_sequence(applied_level);
        if (current_seq != 0 &&
            current_seq == view_state.last_sequence &&
            applied_level == view_state.last_lod_level &&
            view_state.active_vbo != UINT_MAX &&
            view_state.last_count > 0 &&
            view_state.cached_data_identity == data_source.identity() &&
            view_state.last_t_min == t_min_ns &&
            view_state.last_t_max == t_max_ns &&
            view_state.last_width_px == width_px &&
            view_state.last_empty_window_behavior == empty_window_behavior &&
            view_state.uploaded_t_origin_ns == t_origin_ns)
        {
            load_cached_result(result, applied_level);
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
            if (try_stale_fallback(result)) {
                break;
            }
            if (applied_level > 0) {
                target_level = applied_level - 1;
                continue;
            }
            break;
        }

        const auto& snapshot = snapshot_result.snapshot;
        bool timestamps_monotonic = true;
        if (get_timestamp) {
            const void* current_identity = data_source.identity();
            const bool need_monotonicity_scan =
                view_state.last_timestamp_order_sequence != snapshot.sequence ||
                view_state.last_timestamp_order_identity != current_identity;
            if (need_monotonicity_scan) {
                bool is_monotonic = true;
                const void* first_sample = snapshot.at(0);
                if (!first_sample) {
                    is_monotonic = false;
                }
                else {
                    std::int64_t prev_ts = get_timestamp(first_sample);
                    for (std::size_t i = 1; i < snapshot.count; ++i) {
                        const void* sample = snapshot.at(i);
                        if (!sample) {
                            is_monotonic = false;
                            break;
                        }
                        const std::int64_t ts = get_timestamp(sample);
                        if (ts < prev_ts) {
                            is_monotonic = false;
                            break;
                        }
                        prev_ts = ts;
                    }
                }
                view_state.last_timestamp_order_sequence = snapshot.sequence;
                view_state.last_timestamp_order_identity = current_identity;
                view_state.last_timestamps_monotonic = is_monotonic;
            }
            timestamps_monotonic = view_state.last_timestamps_monotonic;
        }

        // Find visible range using binary search
        std::size_t first_idx = 0;
        std::size_t last_idx = snapshot.count;
        std::int64_t last_ts = 0;
        bool have_last_ts = false;
        if (get_timestamp) {
            const void* last_sample = snapshot.at(snapshot.count - 1);
            if (last_sample) {
                last_ts = get_timestamp(last_sample);
                have_last_ts = true;
            }
            if (!timestamps_monotonic) {
                // Non-monotonic timestamps invalidate binary-search assumptions.
                // Fall back to a linear scan for correctness.
                VNM_PLOT_PROFILE_SCOPE(profiler, "process_view.linear_fallback");
                std::size_t match_first = snapshot.count;
                std::size_t match_last = 0;
                for (std::size_t i = 0; i < snapshot.count; ++i) {
                    const void* sample = snapshot.at(i);
                    if (!sample) {
                        continue;
                    }
                    const std::int64_t ts = get_timestamp(sample);
                    if (ts < t_min_ns || ts > t_max_ns) {
                        continue;
                    }
                    if (match_first == snapshot.count) {
                        match_first = i;
                    }
                    match_last = i + 1;
                }
                if (match_first < match_last) {
                    first_idx = (match_first > 0) ? (match_first - 1) : 0;
                    last_idx = std::min(match_last + 2, snapshot.count);
                }
                else {
                    first_idx = snapshot.count;
                    last_idx = snapshot.count;
                }
            }
            else {
                VNM_PLOT_PROFILE_SCOPE(profiler, "process_view.binary_search");
                first_idx = lower_bound_timestamp(snapshot, get_timestamp, t_min_ns);
                if (first_idx > 0) {
                    --first_idx;
                }
                last_idx = upper_bound_timestamp(snapshot, get_timestamp, t_max_ns);
                last_idx = std::min(last_idx + 2, snapshot.count);
            }
        }

        const bool can_hold_last_forward =
            empty_window_behavior == Empty_window_behavior::HOLD_LAST_FORWARD &&
            access.clone_with_timestamp &&
            have_last_ts &&
            last_ts < t_max_ns;

        if (first_idx >= last_idx) {
            if (can_hold_last_forward) {
                first_idx = snapshot.count - 1;
                last_idx = snapshot.count;
                hold_last_forward = true;
            }
            else
            if (applied_level > 0 && !was_tried(applied_level - 1)) {
                target_level = applied_level - 1;
                continue;
            }
            else {
                break;
            }
        }
        else
        if (can_hold_last_forward && last_idx == snapshot.count) {
            hold_last_forward = true;
        }

        GLsizei count = static_cast<GLsizei>(last_idx - first_idx);
        if (hold_last_forward) {
            ++count;
        }
        const std::size_t base_samples = (count > 0)
            ? static_cast<std::size_t>(count) * applied_scale : 0;
        const double base_pps = (base_samples > 0)
            ? width_px / static_cast<double>(base_samples) : 0.0;

        const std::size_t desired_level = choose_lod_level(scales, base_pps);
        if (desired_level != applied_level) {
            if (!was_tried(desired_level)) {
                target_level = desired_level;
                continue;
            }
        }

        {
            VNM_PLOT_PROFILE_SCOPE(profiler, "process_view.cpu_prepare");
            // GL fallback owns its own buffer handle; the RHI path lives
            // entirely in view_state.rhi->vbo (allocated lazily below).
            if (!skip_gl && !use_rhi_uploads && view_state.id == UINT_MAX) {
                glGenBuffers(1, &view_state.id);
            }

            const std::size_t needed_elements = snapshot.count + (hold_last_forward ? 1 : 0);
            const std::size_t needed_bytes = needed_elements * sizeof(gpu_sample_t);
            const void* current_identity = data_source.identity();
            const bool region_changed = (view_state.last_ring_size < needed_bytes);
            const bool origin_changed = (view_state.uploaded_t_origin_ns != t_origin_ns);
            const bool hold_changed =
                (view_state.last_hold_last_forward != hold_last_forward) ||
                (hold_last_forward && view_state.last_t_max != t_max_ns);
            const bool must_upload = region_changed
                || origin_changed
                || (snapshot.sequence != view_state.last_sequence)
                || (applied_level != view_state.last_lod_level)
                || (current_identity != view_state.cached_data_identity)
                || (snapshot.count != view_state.last_snapshot_elements);
            const bool needs_hold_upload = hold_last_forward && (must_upload || hold_changed);

            // Per-sample staging produces a fixed gpu_sample_t value rebased
            // against t_origin_ns so the GPU never sees absolute nanosecond
            // timestamps. The renderer reads each sample through
            // access.get_timestamp / get_value / get_range (fp32 boundary)
            // and emits one gpu_sample_t per snapshot entry plus an optional
            // hold-last-forward synthetic sample.
            const auto stage_one_sample = [&](gpu_sample_t& dst, const void* src) {
                const std::int64_t ts_ns = access.get_timestamp(src);
                dst.t_rel = static_cast<float>(ts_ns - t_origin_ns) * 1.0e-9f;
                dst.y     = access.get_value ? access.get_value(src) : 0.0f;
                const auto range = access.get_range
                    ? access.get_range(src)
                    : std::make_pair(dst.y, dst.y);
                dst.y_min = range.first;
                dst.y_max = range.second;
            };

            // Two write surfaces gate this block:
            //   - use_rhi_uploads: the RHI upload path runs whenever a QRhi
            //     and batch are bound, even when the host set skip_gl to
            //     silence the GL fallback.
            //   - !skip_gl: the GL upload path runs only when gl* calls are
            //     allowed (no RHI bound, and the host did not set skip_gl
            //     for CPU profiling).
            // The staging vector itself is a CPU-side rebased copy of the
            // snapshot; it is always built when either upload path runs and
            // is consumed downstream by RHI primitive preparation when
            // building the LINE per-frame window buffer.
            if (must_upload && (use_rhi_uploads || !skip_gl)) {
                auto& staging = view_state.staging;
                staging.resize(snapshot.count + (hold_last_forward ? 1 : 0));

                for (std::size_t i = 0; i < snapshot.count; ++i) {
                    const void* src = snapshot.at(i);
                    if (!src) {
                        staging[i] = gpu_sample_t{};
                        continue;
                    }
                    stage_one_sample(staging[i], src);
                }

                if (hold_last_forward) {
                    const void* source_sample = (snapshot.count > 0)
                        ? snapshot.at(snapshot.count - 1) : nullptr;
                    if (source_sample) {
                        // Build the synthetic sample in the user struct layout
                        // first so types whose timestamp is stored as a float
                        // (function_sample_t::x) round-trip through the
                        // float-seconds <-> ns conversion the access policy
                        // already encodes.
                        std::vector<unsigned char> user_sample(snapshot.stride);
                        access.clone_with_timestamp(
                            user_sample.data(),
                            source_sample,
                            t_max_ns);
                        stage_one_sample(staging.back(), user_sample.data());
                    }
                    else {
                        staging.back() = gpu_sample_t{};
                    }
                }

#ifdef VNM_PLOT_HAS_QRHI
                if (use_rhi_uploads) {
                    if (!view_state.rhi) {
                        view_state.rhi =
                            std::make_unique<vbo_view_state_t::rhi_buffers_t>();
                    }
                    // Allocate or grow the RHI vertex/storage buffer to fit the
                    // current sample window. Like the GL path we add a 25%
                    // headroom so subsequent appends do not trigger a
                    // reallocate every frame.
                    //
                    // uploadStaticBuffer accepts repeat uploads on Static
                    // buffers; the renderer calls it once per frame.
                    const std::size_t alloc_bytes = needed_bytes + needed_bytes / 4;
                    if (!view_state.rhi->vbo
                        || view_state.rhi_vbo_capacity_bytes < alloc_bytes)
                    {
                        view_state.rhi->vbo.reset(rhi->newBuffer(
                            QRhiBuffer::Static,
                            QRhiBuffer::VertexBuffer,
                            static_cast<quint32>(alloc_bytes)));
                        if (view_state.rhi->vbo && view_state.rhi->vbo->create()) {
                            view_state.rhi_vbo_capacity_bytes = alloc_bytes;
                            view_state.last_ring_size = alloc_bytes;
                        }
                    }
                    if (view_state.rhi->vbo) {
                        rhi_updates->uploadStaticBuffer(
                            view_state.rhi->vbo.get(),
                            0,
                            static_cast<quint32>(staging.size() * sizeof(gpu_sample_t)),
                            staging.data());
                    }
                }
                else
#endif
                if (!skip_gl) {
                    glBindBuffer(GL_ARRAY_BUFFER, view_state.id);
                    if (region_changed) {
                        const std::size_t alloc_size = needed_bytes + needed_bytes / 4;
                        glBufferData(GL_ARRAY_BUFFER,
                            static_cast<GLsizeiptr>(alloc_size), nullptr, GL_DYNAMIC_DRAW);
                        view_state.last_ring_size = alloc_size;
                    }
                    glBufferSubData(GL_ARRAY_BUFFER, 0,
                        static_cast<GLsizeiptr>(staging.size() * sizeof(gpu_sample_t)),
                        staging.data());
                }
            }
            else
            if (needs_hold_upload && (use_rhi_uploads || !skip_gl)) {
                // Data unchanged, only the synthetic last sample's timestamp
                // moved with t_max_ns. Stage just that one gpu_sample_t and
                // patch the VBO at its trailing offset.
                const void* source_sample = (snapshot.count > 0)
                    ? snapshot.at(snapshot.count - 1) : nullptr;
                if (source_sample) {
                    std::vector<unsigned char> user_sample(snapshot.stride);
                    access.clone_with_timestamp(
                        user_sample.data(), source_sample, t_max_ns);
                    gpu_sample_t hold_sample{};
                    stage_one_sample(hold_sample, user_sample.data());
                    // Mirror the patched sample into the CPU-side staging
                    // vector so the LINE per-frame window buffer sees the
                    // updated hold value alongside the GPU-side patch.
                    auto& staging = view_state.staging;
                    if (!staging.empty()) {
                        staging.back() = hold_sample;
                    }
#ifdef VNM_PLOT_HAS_QRHI
                    if (use_rhi_uploads && view_state.rhi && view_state.rhi->vbo) {
                        rhi_updates->uploadStaticBuffer(
                            view_state.rhi->vbo.get(),
                            static_cast<quint32>(snapshot.count * sizeof(gpu_sample_t)),
                            static_cast<quint32>(sizeof(gpu_sample_t)),
                            &hold_sample);
                    }
                    else
#endif
                    if (!skip_gl) {
                        glBindBuffer(GL_ARRAY_BUFFER, view_state.id);
                        glBufferSubData(
                            GL_ARRAY_BUFFER,
                            static_cast<GLintptr>(snapshot.count * sizeof(gpu_sample_t)),
                            static_cast<GLsizeiptr>(sizeof(gpu_sample_t)),
                            &hold_sample);
                    }
                }
            }
            if (must_upload) {
                view_state.last_sequence = snapshot.sequence;
                view_state.cached_data_identity = current_identity;
                view_state.uploaded_t_origin_ns = t_origin_ns;
            }
            view_state.last_snapshot_elements = snapshot.count;
        }

        // active_vbo is the contract field both the GL fast-path and the
        // skip_gl tests inspect. Under RHI it is set to a non-zero sentinel
        // so the cache predicates in the next frame still observe "we have
        // bytes ready". The actual buffer is view_state.rhi->vbo on RHI
        // builds.
#ifdef VNM_PLOT_HAS_QRHI
        if (use_rhi_uploads && view_state.rhi && view_state.rhi->vbo) {
            view_state.active_vbo = 1u;
        }
        else
#endif
        view_state.active_vbo = view_state.id;
        view_state.last_first = static_cast<GLint>(first_idx);
        view_state.last_count = count;

        view_state.last_lod_level = applied_level;
        view_state.last_t_min = t_min_ns;
        view_state.last_t_max = t_max_ns;
        view_state.last_width_px = width_px;
        view_state.last_empty_window_behavior = empty_window_behavior;

        result.can_draw = true;
        result.first = view_state.last_first;
        result.count = view_state.last_count;
        result.applied_level = applied_level;
        result.applied_pps = base_pps * static_cast<double>(applied_scale);
        view_state.last_applied_pps = result.applied_pps;
        view_state.last_hold_last_forward = hold_last_forward;
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
    const frame_context_t& ctx,
    std::int64_t origin_ns)
{
    glUniformMatrix4fv(program.uniform_location("pmv"), 1, GL_FALSE, glm::value_ptr(pmv));

    const auto& layout = ctx.layout;
    glUniform1f(program.uniform_location("width"),  static_cast<float>(layout.usable_width));
    glUniform1f(program.uniform_location("height"), static_cast<float>(layout.usable_height));
    glUniform1f(program.uniform_location("y_offset"), 0.0f);
    glUniform1f(program.uniform_location("win_h"), static_cast<float>(ctx.win_h));
    // Sample timestamps are rebased against origin_ns on upload; the time
    // window uniforms must use the same origin so axes and samples agree on
    // one fp32 seconds-from-origin domain.
    const float t_min_rel = static_cast<float>(ctx.t0 - origin_ns) * 1.0e-9f;
    const float t_max_rel = static_cast<float>(ctx.t1 - origin_ns) * 1.0e-9f;
    glUniform1f(program.uniform_location("t_min"), t_min_rel);
    glUniform1f(program.uniform_location("t_max"), t_max_rel);
    glUniform1f(program.uniform_location("v_min"), ctx.v0);
    glUniform1f(program.uniform_location("v_max"), ctx.v1);

    // Line rendering options
    const bool snap = ctx.config ? ctx.config->snap_lines_to_pixels : false;
    glUniform1i(program.uniform_location("snap_to_pixels"), snap ? 1 : 0);

    // Zero-axis color (same as grid lines)
    const bool dark_mode = ctx.dark_mode;
    const Color_palette palette = dark_mode ? Color_palette::dark() : Color_palette::light();
    glUniform4fv(program.uniform_location("zero_axis_color"), 1, glm::value_ptr(palette.grid_line));
}

void Series_renderer::modify_uniforms_for_preview(
    GL_program& program,
    const frame_context_t& ctx,
    std::int64_t origin_ns)
{
    const auto& layout = ctx.layout;
    const double preview_top =
        layout.usable_height + std::max(0.0, layout.h_bar_height - double(k_scissor_pad_px));
    const float preview_y = static_cast<float>(preview_top);
    const float preview_height = static_cast<float>(ctx.adjusted_preview_height);

    glUniform1f(program.uniform_location("y_offset"), preview_y);
    glUniform1f(program.uniform_location("width"),  static_cast<float>(ctx.win_w));
    glUniform1f(program.uniform_location("height"), preview_height);
    glUniform1f(program.uniform_location("v_min"), ctx.preview_v0);
    glUniform1f(program.uniform_location("v_max"), ctx.preview_v1);
    const float t_min_rel = static_cast<float>(ctx.t_available_min - origin_ns) * 1.0e-9f;
    const float t_max_rel = static_cast<float>(ctx.t_available_max - origin_ns) * 1.0e-9f;
    glUniform1f(program.uniform_location("t_min"), t_min_rel);
    glUniform1f(program.uniform_location("t_max"), t_max_rel);
}

void Series_renderer::prepare(
    const frame_context_t& ctx,
    const std::map<int, std::shared_ptr<const series_data_t>>& series)
{
#ifndef VNM_PLOT_HAS_QRHI
    (void)ctx;
    (void)series;
#else
    // GL fallback drives everything from render() in a single call. The
    // record-draws split only matters when a QRhi is bound, because that's
    // the path where the host wraps draws in beginPass(batch) and
    // beginPass consumes its 4th argument before render() would otherwise
    // get to fill it.
    if (!ctx.rhi) {
        return;
    }

    if (series.empty() || !m_asset_loader) {
        return;
    }

    const auto& layout = ctx.layout;
    if (layout.usable_width <= 0.0 || layout.usable_height <= 0.0) {
        return;
    }

    ++m_frame_id;

    vnm::plot::Profiler* profiler = ctx.config ? ctx.config->profiler.get() : nullptr;
    VNM_PLOT_PROFILE_SCOPE(profiler,
        "renderer.frame.execute_passes.render_data_series.prepare");

    const bool skip_gl = ctx.skip_gl;
    QRhi*                    rhi         = ctx.rhi;
    QRhiResourceUpdateBatch* rhi_updates = ctx.rhi_updates;
    m_rhi_state->last_rhi = rhi;
    m_rhi_state->pending_updates = rhi_updates;

    const float line_width = ctx.config ? static_cast<float>(ctx.config->line_width_px) : 1.0f;
    const float point_diameter = ctx.config
        ? static_cast<float>(ctx.config->point_diameter_px) : 1.0f;
    const float area_fill_alpha = ctx.config
        ? static_cast<float>(ctx.config->area_fill_alpha) : 0.3f;

    // Cleanup stale VBO states for series no longer in the map. Under RHI
    // the GL handles are never allocated (skip_gl is set by the host), so
    // the glDelete* paths are dormant; the QRhiBuffer destructors run when
    // the entry is erased.
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

    auto& draw_states = m_rhi_state->frame_draw_states;
    draw_states.clear();
    draw_states.reserve(series.size());

    const double preview_visibility = ctx.config ? ctx.config->preview_visibility : 1.0;
    const bool preview_visible = ctx.adjusted_preview_height > 0.0 && preview_visibility > 0.0;
    m_rhi_state->frame_preview_visible = preview_visible;

    const std::int64_t main_origin_ns = choose_origin_ns(ctx.t0, ctx.t1 - ctx.t0);
    const std::int64_t preview_origin_ns = preview_visible
        ? choose_origin_ns(ctx.t_available_min, ctx.t_available_max - ctx.t_available_min)
        : main_origin_ns;

    enum class Error_cat : uint32_t {
        MISSING_SIGNAL, MISSING_SIGNAL_PREVIEW,
        PREVIEW_MISSING_SOURCE,
        MISSING_SHADER,
        COLORMAP_LINE_DEFAULT_NOT_SUPPORTED,
        COLORMAP_LINE_DEFAULT_NOT_SUPPORTED_PREVIEW
    };

    const auto uses_default_colormap_line_shader = [](const series_data_t& s) {
        auto it = s.shaders.find(Display_style::COLORMAP_LINE);
        if (it != s.shaders.end() && !it->second.empty()) {
            return false;
        }
        return s.shader_set.empty();
    };
    const auto log_error_once = [&](Error_cat cat, int series_id,
                                    const std::string& message) {
        if (!ctx.config || !ctx.config->log_error) {
            return;
        }
        const uint64_t key = (static_cast<uint64_t>(cat) << 32)
            | static_cast<uint64_t>(static_cast<uint32_t>(series_id));
        if (m_logged_errors.insert(key).second) {
            ctx.config->log_error(message);
        }
    };

    for (const auto& [id, s] : series) {
        VNM_PLOT_PROFILE_SCOPE(profiler,
            "renderer.frame.execute_passes.render_data_series.series");
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
        if (!!(main_style & Display_style::COLORMAP_LINE) && uses_default_colormap_line_shader(*s)) {
            log_error_once(Error_cat::COLORMAP_LINE_DEFAULT_NOT_SUPPORTED, id,
                "COLORMAP_LINE with the default shader is currently disabled: "
                "the renderer does not yet upload per-sample signal data to "
                "the signal SSBO. Provide a custom shader_set / per-style "
                "shader override and bind_uniforms, or wait for the feature. "
                "Series " + std::to_string(id) + " falls back to LINE if "
                "requested or is otherwise skipped.");
            main_style = static_cast<Display_style>(
                static_cast<int>(main_style) & ~static_cast<int>(Display_style::COLORMAP_LINE));
        }
        if (!main_style) {
            continue;
        }

        const bool has_preview_config = s->has_preview_config();
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

            if (!!(preview_style & Display_style::COLORMAP_LINE) &&
                !(preview_access && preview_access->get_signal))
            {
                log_error_once(Error_cat::MISSING_SIGNAL_PREVIEW, id,
                    "COLORMAP_LINE requires Data_access_policy::get_signal (preview series "
                        + std::to_string(id) + ")");
                preview_style = static_cast<Display_style>(
                    static_cast<int>(preview_style) & ~static_cast<int>(Display_style::COLORMAP_LINE));
            }
            if (!!(preview_style & Display_style::COLORMAP_LINE) &&
                uses_default_colormap_line_shader(*s))
            {
                log_error_once(Error_cat::COLORMAP_LINE_DEFAULT_NOT_SUPPORTED_PREVIEW, id,
                    "COLORMAP_LINE preview with the default shader is "
                    "currently disabled (signal-data upload not implemented). "
                    "Series " + std::to_string(id) + " preview will not render "
                    "in colormap-line mode.");
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

        const std::size_t prev_lod_level = vbo_state.main_view.last_lod_level;
        auto main_result = process_view(
            vbo_state.main_view, vbo_state, m_frame_id, *main_source,
            main_access, main_scales,
            ctx.t0, ctx.t1, main_origin_ns,
            layout.usable_width, s->empty_window_behavior, profiler, skip_gl,
            rhi, rhi_updates);
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
                *preview_access, preview_scales,
                ctx.t_available_min, ctx.t_available_max, preview_origin_ns,
                ctx.win_w, s->empty_window_behavior, profiler, skip_gl,
                rhi, rhi_updates);
        }

        series_draw_state_t draw_state;
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

    // Iterate the plan and run rhi_prepare_series_primitive for every
    // RHI-capable view that will be drawn. This fills the per-primitive UBOs
    // and (for LINE) the per-frame line_window_vbo. COLORMAP_* don't reach a
    // draw under RHI (skip_gl is set), so no prepare call is needed for them.
    for (auto& draw_state : draw_states) {
        if (!draw_state.series || !draw_state.vbo_state) {
            continue;
        }

        auto run_prepare = [&](Display_style primitive_style,
                               vbo_view_state_t& view_state,
                               const view_render_result_t& view_result,
                               bool is_preview)
        {
            if (!view_result.can_draw || view_result.count <= 0) {
                return;
            }
            rhi_prepare_series_primitive(
                ctx, draw_state.series.get(),
                primitive_style, view_state, view_result, is_preview,
                line_width, point_diameter, area_fill_alpha,
                is_preview ? preview_origin_ns : main_origin_ns);
        };

        if (!!(draw_state.main_style & Display_style::AREA)) {
            run_prepare(Display_style::AREA,
                draw_state.vbo_state->main_view, draw_state.main_result,
                false);
        }
        if (!!(draw_state.main_style & Display_style::LINE)) {
            run_prepare(Display_style::LINE,
                draw_state.vbo_state->main_view, draw_state.main_result,
                false);
        }
        if (!!(draw_state.main_style & Display_style::DOTS)) {
            run_prepare(Display_style::DOTS,
                draw_state.vbo_state->main_view, draw_state.main_result,
                false);
        }
        if (draw_state.has_preview) {
            if (!!(draw_state.preview_style & Display_style::AREA)) {
                run_prepare(Display_style::AREA,
                    draw_state.vbo_state->preview_view,
                    draw_state.preview_result, true);
            }
            if (!!(draw_state.preview_style & Display_style::LINE)) {
                run_prepare(Display_style::LINE,
                    draw_state.vbo_state->preview_view,
                    draw_state.preview_result, true);
            }
            if (!!(draw_state.preview_style & Display_style::DOTS)) {
                run_prepare(Display_style::DOTS,
                    draw_state.vbo_state->preview_view,
                    draw_state.preview_result, true);
            }
        }
    }

    m_rhi_state->frame_plan_ready = true;
#endif // VNM_PLOT_HAS_QRHI
}

void Series_renderer::render(
    const frame_context_t& ctx,
    const std::map<int, std::shared_ptr<const series_data_t>>& series)
{
#ifdef VNM_PLOT_HAS_QRHI
    // Fast path: the host called prepare() before opening the render pass
    // (RHI two-phase shape). Replay the cached plan as draw commands only.
    if (ctx.rhi && m_rhi_state->frame_plan_ready) {
        // Reset the flag up-front so an unmatched prepare() / render() pair
        // can never silently replay an old plan against a different frame.
        m_rhi_state->frame_plan_ready = false;
        // Snapshot cache cleanup matches the standalone path: the per-view
        // snapshot holds released here let upstream sources mark samples
        // evictable for the next snapshot.
        struct Snapshot_cache_scope {
            Series_renderer& renderer;
            ~Snapshot_cache_scope() { renderer.clear_frame_snapshot_caches(); }
        } cache_scope{*this};

        // The origin values that prepare() snapped against drove the UBO
        // contents already written into the resource-update batch. The
        // record-draws phase only emits cb->* commands, so it does not
        // need those origins itself.
        auto record_one = [&](Display_style primitive_style,
                              vbo_view_state_t& view_state,
                              const view_render_result_t& view_result,
                              bool is_preview)
        {
            if (!view_result.can_draw || view_result.count <= 0) {
                return;
            }
            rhi_record_series_primitive(
                ctx, primitive_style, view_state, view_result, is_preview);
        };

        // Group order matches the GL flow so series stack identically: the
        // main passes paint behind any preview.
        for (auto& draw_state : m_rhi_state->frame_draw_states) {
            if (!draw_state.series || !draw_state.vbo_state) {
                continue;
            }
            if (!!(draw_state.main_style & Display_style::AREA)) {
                record_one(Display_style::AREA,
                    draw_state.vbo_state->main_view, draw_state.main_result,
                    false);
            }
            if (!!(draw_state.main_style & Display_style::LINE)) {
                record_one(Display_style::LINE,
                    draw_state.vbo_state->main_view, draw_state.main_result,
                    false);
            }
            if (!!(draw_state.main_style & Display_style::DOTS)) {
                record_one(Display_style::DOTS,
                    draw_state.vbo_state->main_view, draw_state.main_result,
                    false);
            }
        }
        if (m_rhi_state->frame_preview_visible) {
            for (auto& draw_state : m_rhi_state->frame_draw_states) {
                if (!draw_state.series || !draw_state.vbo_state ||
                    !draw_state.has_preview)
                {
                    continue;
                }
                if (!!(draw_state.preview_style & Display_style::AREA)) {
                    record_one(Display_style::AREA,
                        draw_state.vbo_state->preview_view,
                        draw_state.preview_result, true);
                }
                if (!!(draw_state.preview_style & Display_style::LINE)) {
                    record_one(Display_style::LINE,
                        draw_state.vbo_state->preview_view,
                        draw_state.preview_result, true);
                }
                if (!!(draw_state.preview_style & Display_style::DOTS)) {
                    record_one(Display_style::DOTS,
                        draw_state.vbo_state->preview_view,
                        draw_state.preview_result, true);
                }
            }
        }

        m_rhi_state->frame_draw_states.clear();
        return;
    }
#endif

    // Standalone path: GL fallback (tests, headless) and the case where
    // ctx.rhi is null. Runs prep + draws in a single call. The full
    // per-series loop and draw_pass / draw_group lambdas live below.
    clear_frame_snapshot_caches();

    if (series.empty() || !m_asset_loader) {
        return;
    }

    const auto& layout = ctx.layout;
    if (layout.usable_width <= 0.0 || layout.usable_height <= 0.0) {
        return;
    }

    // Increment frame counter for snapshot caching
    ++m_frame_id;

    struct Frame_snapshot_cache_scope {
        Series_renderer& renderer;
        ~Frame_snapshot_cache_scope() { renderer.clear_frame_snapshot_caches(); }
    } frame_snapshot_cache_scope{*this};

    vnm::plot::Profiler* profiler = ctx.config ? ctx.config->profiler.get() : nullptr;
    VNM_PLOT_PROFILE_SCOPE(profiler, "renderer.frame.execute_passes.render_data_series");

    // Skip all GL calls if configured (for pure CPU profiling)
    const bool skip_gl = ctx.skip_gl;

    // Resource uploads ride the host-owned batch handed in via the frame
    // context. The host opens that batch BEFORE the render pass and submits
    // it as the 4th argument to beginPass; calling cb->resourceUpdate inside
    // an open pass is a hard error on D3D11.
    QRhi*                    rhi         = ctx.rhi;
    QRhiResourceUpdateBatch* rhi_updates = ctx.rhi_updates;
#ifdef VNM_PLOT_HAS_QRHI
    if (rhi) {
        m_rhi_state->last_rhi = rhi;
        m_rhi_state->pending_updates = rhi_updates;
    }
#endif

    const bool dark_mode = ctx.dark_mode;
    const float line_width = ctx.config ? static_cast<float>(ctx.config->line_width_px) : 1.0f;
    const float point_diameter = ctx.config
        ? static_cast<float>(ctx.config->point_diameter_px) : 1.0f;
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

    // The RHI path bakes blend / scissor / MSAA into pipeline state objects
    // and submits scissors per draw via cb->setScissor; the global GL state
    // setup below is inert under RHI rendering.
    if (!skip_gl && !rhi) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        // Set line width once for all draw passes to avoid repeated glLineWidth calls
        glLineWidth(line_width);
        // Enable scissor test once - we'll only update the rectangle per draw pass
        glEnable(GL_SCISSOR_TEST);
    }

    // Per-frame draw plan vector. Lives on the renderer so prepare() (when
    // called separately by the RHI host) can hand the plan off to render()
    // through m_rhi_state->frame_draw_states. Tests that call render()
    // standalone build a local vector and never touch the cached plan.
    std::vector<series_draw_state_t> local_draw_states;
    local_draw_states.reserve(series.size());
    std::vector<series_draw_state_t>& draw_states = local_draw_states;

    const double preview_visibility = ctx.config ? ctx.config->preview_visibility : 1.0;
    const bool preview_visible = ctx.adjusted_preview_height > 0.0 && preview_visibility > 0.0;

    // Per-view fp32 rebase origins. Main and preview cover different visible
    // windows, so each gets its own snap-aligned origin; uniforms and
    // staging for that view all see the same origin.
    const std::int64_t main_origin_ns = choose_origin_ns(ctx.t0, ctx.t1 - ctx.t0);
    const std::int64_t preview_origin_ns = preview_visible
        ? choose_origin_ns(ctx.t_available_min, ctx.t_available_max - ctx.t_available_min)
        : main_origin_ns;

      enum class Error_cat : uint32_t {
          MISSING_SIGNAL, MISSING_SIGNAL_PREVIEW,
          PREVIEW_MISSING_SOURCE,
          MISSING_SHADER,
          COLORMAP_LINE_DEFAULT_NOT_SUPPORTED,
          COLORMAP_LINE_DEFAULT_NOT_SUPPORTED_PREVIEW
      };

    // The default plot_colormap_line.vert shader pulls per-sample signal
    // floats from SSBO binding 2, but the renderer does not yet upload that
    // data. Detect whether a series relies on the default colormap-line
    // shader so we can disable the mode on those series instead of letting
    // them render with constant signal=0.0 (which collapses every sample to
    // the first colormap entry). Series that supply a custom shader_set or
    // a per-style override are presumed to handle signal upload themselves
    // through bind_uniforms.
    const auto uses_default_colormap_line_shader = [](const series_data_t& s) {
        auto it = s.shaders.find(Display_style::COLORMAP_LINE);
        if (it != s.shaders.end() && !it->second.empty()) {
            return false;
        }
        return s.shader_set.empty();
    };
    const auto log_error_once = [&](Error_cat cat, int series_id,
                                    const std::string& message) {
        if (!ctx.config || !ctx.config->log_error) {
            return;
        }
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
        if (!!(main_style & Display_style::COLORMAP_LINE) && uses_default_colormap_line_shader(*s)) {
            log_error_once(Error_cat::COLORMAP_LINE_DEFAULT_NOT_SUPPORTED, id,
                "COLORMAP_LINE with the default shader is currently disabled: "
                "the renderer does not yet upload per-sample signal data to "
                "the signal SSBO. Provide a custom shader_set / per-style "
                "shader override and bind_uniforms, or wait for the feature. "
                "Series " + std::to_string(id) + " falls back to LINE if "
                "requested or is otherwise skipped.");
            main_style = static_cast<Display_style>(
                static_cast<int>(main_style) & ~static_cast<int>(Display_style::COLORMAP_LINE));
        }
        if (!main_style) {
            continue;
        }

        const bool has_preview_config = s->has_preview_config();
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

            if (!!(preview_style & Display_style::COLORMAP_LINE) &&
                !(preview_access && preview_access->get_signal))
            {
                log_error_once(Error_cat::MISSING_SIGNAL_PREVIEW, id,
                    "COLORMAP_LINE requires Data_access_policy::get_signal (preview series "
                        + std::to_string(id) + ")");
                preview_style = static_cast<Display_style>(
                    static_cast<int>(preview_style) & ~static_cast<int>(Display_style::COLORMAP_LINE));
            }
            if (!!(preview_style & Display_style::COLORMAP_LINE) &&
                uses_default_colormap_line_shader(*s))
            {
                log_error_once(Error_cat::COLORMAP_LINE_DEFAULT_NOT_SUPPORTED_PREVIEW, id,
                    "COLORMAP_LINE preview with the default shader is "
                    "currently disabled (signal-data upload not implemented). "
                    "Series " + std::to_string(id) + " preview will not render "
                    "in colormap-line mode.");
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
            main_access, main_scales,
            ctx.t0, ctx.t1, main_origin_ns,
            layout.usable_width, s->empty_window_behavior, profiler, skip_gl,
            rhi, rhi_updates);
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
                *preview_access, preview_scales,
                ctx.t_available_min, ctx.t_available_max, preview_origin_ns,
                ctx.win_w, s->empty_window_behavior, profiler, skip_gl,
                rhi, rhi_updates);
        }

        series_draw_state_t draw_state;
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

    auto draw_pass = [&](series_draw_state_t& draw_state,
                         Display_style primitive_style,
                         vbo_view_state_t& view_state,
                         const view_render_result_t& view_result,
                         bool is_preview) {
        const GLsizei count = view_result.count;
        if (count <= 0) {
            return;
        }

        // LINE, DOTS, and AREA go through the RHI record helper. COLORMAP_*
        // fall through to the GL fallback below, which is gated by skip_gl;
        // under any RHI backend the host sets skip_gl so those styles render
        // nothing here. The matching prepare phase that populates UBOs /
        // line_window_vbo already ran before beginPass when render() was
        // called via prepare()+render() (the RHI host path).
#ifdef VNM_PLOT_HAS_QRHI
        const bool rhi_capable_style =
            (primitive_style == Display_style::LINE) ||
            (primitive_style == Display_style::DOTS) ||
            (primitive_style == Display_style::AREA);
        if (rhi && rhi_capable_style) {
            rhi_record_series_primitive(
                ctx, primitive_style, view_state, view_result,
                is_preview);
            return;
        }
#else
        (void)main_origin_ns;
        (void)preview_origin_ns;
#endif

        // Styles that consume neighbour samples need the adjacency index
        // buffer. DOTS and AREA pull their samples directly via instanced
        // vertex attributes (one sample for DOTS, sample i and i+1 for
        // AREA), so they bypass the SSBO path entirely.
        const bool use_adjacency =
            (primitive_style == Display_style::LINE) ||
            (primitive_style == Display_style::COLORMAP_AREA) ||
            (primitive_style == Display_style::COLORMAP_LINE);
        // Per-instance vertex count for the triangle-strip expansion.
        // LINE / COLORMAP_LINE: a single thickened quad (4 verts).
        // AREA: drawn in two passes - 6-vert fill + 4-vert zero-axis bar.
        //   Splitting the passes keeps every triangle either real or close
        //   to it, instead of stitching the fill and emphasis quad together
        //   through a degenerate-connector chain.
        // COLORMAP_AREA: a fill region plus emphasis quad stitched together
        //   via degenerate connectors (13 verts), still on the SSBO path.
        // DOTS: a screen-aligned quad (4 verts).
        GLsizei verts_per_instance = 4;
        if (primitive_style == Display_style::AREA) {
            verts_per_instance = 6;
        }
        if (primitive_style == Display_style::COLORMAP_AREA) {
            verts_per_instance = 13;
        }
        if (primitive_style != Display_style::DOTS && count < 2) {
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
                                }
                                else {
                                    // We need LOD 0 sequence but have a different LOD snapshot.
                                    // Try to get LOD 0 snapshot for correct sequence tracking.
                                    auto lod0_snapshot = data_source->try_snapshot(0);
                                    if (lod0_snapshot) {
                                        cache_sequence = lod0_snapshot.snapshot.sequence;
                                    }
                                    else {
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
        const std::int64_t view_origin_ns = is_preview ? preview_origin_ns : main_origin_ns;
        set_common_uniforms(*pass_shader, ctx.pmv, ctx, view_origin_ns);
        if (is_preview) {
            modify_uniforms_for_preview(*pass_shader, ctx, view_origin_ns);
        }

        glUniform4fv(pass_shader->uniform_location("color"), 1, glm::value_ptr(draw_color));
        if (const GLint loc = pass_shader->uniform_location("u_line_px"); loc >= 0) {
            glUniform1f(loc, line_width);
        }
        if (const GLint loc = pass_shader->uniform_location("u_point_diameter_px"); loc >= 0) {
            glUniform1f(loc, point_diameter);
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

        glBindVertexArray(ensure_gl_series_vao(primitive_style, view_state.active_vbo));

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

            // Adjacency buffer is consumed via SSBO by the new vertex
            // shaders; binding it as SHADER_STORAGE_BUFFER also makes the
            // upload path explicit.
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, view_state.adjacency_ebo);

            if (view_state.adjacency_ebo_capacity < required_indices) {
                glBufferData(
                    GL_SHADER_STORAGE_BUFFER,
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
                    GL_SHADER_STORAGE_BUFFER,
                    0,
                    static_cast<GLsizeiptr>(required_indices * sizeof(GLuint)),
                    indices.data());
                view_state.adjacency_last_first = view_result.first;
                view_state.adjacency_last_count = count;
            }

            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, view_state.adjacency_ebo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, view_state.active_vbo);

            // SSBO binding 2 is reserved for the colormap-line shader's
            // optional per-sample signal channel. The renderer does not
            // bind it: series using the default plot_colormap_line.vert
            // are rejected upstream (signal-data upload is not yet
            // implemented), and series with a custom shader_set are
            // responsible for binding their own buffer and setting any
            // companion uniforms via Data_access_policy::bind_uniforms.
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
                GLint scissor_y = 0;
                GLsizei scissor_w = 0;
                GLsizei scissor_h = 0;
                if (!to_glint_rounded(double(to_gl_scissor_y(preview_top, preview_height)), scissor_y) ||
                    !to_positive_glsizei(double(ctx.win_w), scissor_w) ||
                    !to_positive_glsizei(preview_height, scissor_h)) {
                    do_draw = false;
                }
                else {
                    glScissor(0, scissor_y, scissor_w, scissor_h);
                }
            }
        }
        else {
            GLint scissor_y = 0;
            GLsizei scissor_w = 0;
            GLsizei scissor_h = 0;
            if (!to_glint_rounded(double(to_gl_scissor_y(0.0, layout.usable_height)), scissor_y) ||
                !to_positive_glsizei(layout.usable_width, scissor_w) ||
                !to_positive_glsizei(layout.usable_height, scissor_h)) {
                do_draw = false;
            }
            else {
                glScissor(0, scissor_y, scissor_w, scissor_h);
            }
        }

        if (do_draw) {
            // Geometry-shader expansion has been folded into the vertex
            // shaders, which generate triangle-strip primitives directly.
            // DOTS and AREA pull samples through instanced vertex attributes
            // and use the base-instance offset to start at the visible
            // window. The remaining SSBO-backed styles read absolute sample
            // indices from the adjacency buffer, so their base instance
            // stays zero.
            if (primitive_style == Display_style::DOTS) {
                if (count > 0) {
                    glDrawArraysInstancedBaseInstance(
                        GL_TRIANGLE_STRIP,
                        0,
                        verts_per_instance,
                        count,
                        static_cast<GLuint>(view_result.first));
                }
            }
            else
            if (primitive_style == Display_style::AREA) {
                const GLsizei instance_count = count - 1;
                if (instance_count > 0) {
                    const GLint axis_pass_loc =
                        pass_shader->uniform_location("u_axis_pass");
                    if (axis_pass_loc >= 0) {
                        glUniform1ui(axis_pass_loc, 0u);
                    }
                    glDrawArraysInstancedBaseInstance(
                        GL_TRIANGLE_STRIP,
                        0,
                        verts_per_instance,
                        instance_count,
                        static_cast<GLuint>(view_result.first));
                    if (axis_pass_loc >= 0) {
                        glUniform1ui(axis_pass_loc, 1u);
                    }
                    glDrawArraysInstancedBaseInstance(
                        GL_TRIANGLE_STRIP,
                        0,
                        4,
                        instance_count,
                        static_cast<GLuint>(view_result.first));
                }
            }
            else {
                const GLsizei instance_count = count - 1;
                if (instance_count > 0) {
                    glDrawArraysInstanced(
                        GL_TRIANGLE_STRIP,
                        0,
                        verts_per_instance,
                        instance_count);
                }
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

    if (!skip_gl && !rhi) {
        glUseProgram(0);
        glBindVertexArray(0);
        // Restore default line width
        glLineWidth(1.0f);
        // Disable scissor test; leave blend enabled to restore previous state.
        // Assumption: caller had GL_BLEND enabled before calling render().
        glDisable(GL_SCISSOR_TEST);
    }

#ifdef VNM_PLOT_HAS_QRHI
    // The host submits rhi_updates via beginPass's 4th argument before any
    // draw in this pass; the renderer only fills the batch and never calls
    // cb->resourceUpdate on it. Drop the cached pointer so a stale batch
    // can't be touched between frames.
    m_rhi_state->pending_updates = nullptr;
#endif
}

#ifdef VNM_PLOT_HAS_QRHI
void Series_renderer::rhi_prepare_series_primitive(
    const frame_context_t& ctx,
    const series_data_t* series,
    Display_style primitive_style,
    vbo_view_state_t& view_state,
    const view_render_result_t& view_result,
    bool is_preview,
    float line_width_px,
    float point_diameter_px,
    float area_fill_alpha,
    std::int64_t origin_ns)
{
    if (!series) {
        return;
    }
    QRhi* rhi = ctx.rhi;
    QRhiResourceUpdateBatch* updates = ctx.rhi_updates;

    const bool is_dots = (primitive_style == Display_style::DOTS);
    const bool is_area = (primitive_style == Display_style::AREA);
    const GLsizei count = view_result.count;
    if (count <= 0) {
        return;
    }
    if (!is_dots && count < 2) {
        return;
    }

    QRhiRenderTarget* rt = ctx.render_target;

    // Lazy QShader load.
    if (!m_rhi_state->shaders_loaded) {
        m_rhi_state->cached_dot_vert  = load_qsb("plot_dot_quad.vert.qsb");
        m_rhi_state->cached_dot_frag  = load_qsb("plot_dot_quad.frag.qsb");
        m_rhi_state->cached_line_vert = load_qsb("plot_line.vert.qsb");
        m_rhi_state->cached_line_frag = load_qsb("plot_line.frag.qsb");
        m_rhi_state->cached_area_vert = load_qsb("plot_area.vert.qsb");
        m_rhi_state->cached_area_frag = load_qsb("plot_area.frag.qsb");
        m_rhi_state->shaders_loaded   = true;
    }

    const QShader& vert = is_dots
        ? m_rhi_state->cached_dot_vert
        : (is_area ? m_rhi_state->cached_area_vert : m_rhi_state->cached_line_vert);
    const QShader& frag = is_dots
        ? m_rhi_state->cached_dot_frag
        : (is_area ? m_rhi_state->cached_area_frag : m_rhi_state->cached_line_frag);
    if (!vert.isValid() || !frag.isValid()) {
        return;
    }

    if (!view_state.rhi) {
        view_state.rhi = std::make_unique<vbo_view_state_t::rhi_buffers_t>();
    }

    auto ensure_ubo = [&](vbo_view_state_t::rhi_buffers_t::srb_entry_t& entry) -> bool {
        if (!entry.ubo || entry.ubo_capacity_bytes < k_series_ubo_bytes) {
            entry.ubo.reset(rhi->newBuffer(
                QRhiBuffer::Dynamic,
                QRhiBuffer::UniformBuffer,
                k_series_ubo_bytes));
            if (entry.ubo && entry.ubo->create()) {
                entry.ubo_capacity_bytes = k_series_ubo_bytes;
                entry.srb.reset();
                entry.last_ubo = nullptr;
            }
            else {
                return false;
            }
        }
        return true;
    };

    auto& primary_srb_entry = is_dots
        ? view_state.rhi->dots_srb
        : (is_area ? view_state.rhi->area_fill_srb : view_state.rhi->line_srb);
    if (!ensure_ubo(primary_srb_entry)) {
        return;
    }
    if (is_area && !ensure_ubo(view_state.rhi->area_axis_srb)) {
        return;
    }

    // LINE under RHI feeds the vertex shader through four per-instance
    // vertex attributes (prev, p0, p1, next) sourced from a dedicated
    // per-frame buffer with the active sample window padded by leading and
    // trailing duplicates: [s[first], s[first], s[first+1], ..., s[last],
    // s[last]]. The buffer is bound four times at offsets 0, 16, 32, 48
    // with stride 16, so instance i sees (padded[i], padded[i+1],
    // padded[i+2], padded[i+3]) which collapses to the desired clamping at
    // the window edges. SSBOs are unusable here because SPIRV-Cross emits
    // them as RWByteAddressBuffer UAVs and D3D11 SM 5.0 vertex shaders
    // accept zero UAVs; QRhi requires HLSL 5.0 bytecode for D3D11, so any
    // storage-buffer access in the vertex stage fails to compile.
    if (!is_dots && !is_area) {
        const std::size_t padded_count = static_cast<std::size_t>(count) + 2;
        const std::size_t needed_bytes = padded_count * sizeof(gpu_sample_t);
        const std::size_t alloc_bytes = needed_bytes + needed_bytes / 4;
        if (!view_state.rhi->line_window_vbo
            || view_state.rhi_line_window_vbo_capacity_bytes < needed_bytes)
        {
            view_state.rhi->line_window_vbo.reset(rhi->newBuffer(
                QRhiBuffer::Static, QRhiBuffer::VertexBuffer,
                static_cast<quint32>(alloc_bytes)));
            if (view_state.rhi->line_window_vbo
                && view_state.rhi->line_window_vbo->create())
            {
                view_state.rhi_line_window_vbo_capacity_bytes = alloc_bytes;
            }
            else {
                return;
            }
        }
        if (updates) {
            std::vector<gpu_sample_t> padded(padded_count);
            const std::size_t first_idx = static_cast<std::size_t>(view_result.first);
            const std::size_t last_idx = first_idx + static_cast<std::size_t>(count - 1);
            padded[0] = view_state.staging[first_idx];
            for (GLsizei i = 0; i < count; ++i) {
                padded[1 + static_cast<std::size_t>(i)] =
                    view_state.staging[first_idx + static_cast<std::size_t>(i)];
            }
            padded[padded_count - 1] = view_state.staging[last_idx];
            updates->uploadStaticBuffer(
                view_state.rhi->line_window_vbo.get(),
                0,
                static_cast<quint32>(needed_bytes),
                padded.data());
        }
    }

    // Pipeline cache key: only kind. The pipeline descriptor depends on the
    // shader-resource-binding LAYOUT (which slots, which stages), not on the
    // concrete buffer handles. Per-series binding handles ride the SRB,
    // which is rebuilt per view below.
    rhi_state_t::pipeline_key_t key{
        is_dots
            ? rhi_state_t::pipeline_kind_t::DOTS
            : (is_area ? rhi_state_t::pipeline_kind_t::AREA
                       : rhi_state_t::pipeline_kind_t::LINE)
    };
    auto& cached = m_rhi_state->pipelines[key];

    // The pipeline state object captures the render-pass descriptor and
    // sample count of the target it was built for. A resize that recreates
    // the FBO can swap the descriptor out from under us, leaving the cached
    // pipeline incompatible with the new pass; rebuild when the descriptor
    // or sample count moves.
    QRhiRenderPassDescriptor* current_rpd = rt->renderPassDescriptor();
    const int current_samples = rt->sampleCount();
    if (cached.pipeline
        && (cached.last_rpd != current_rpd
            || cached.last_sample_count != current_samples))
    {
        cached.pipeline.reset();
    }

    if (!cached.pipeline) {
        cached.vert = vert;
        cached.frag = frag;

        // The pipeline only needs an SRB whose LAYOUT matches the real one;
        // a dummy SRB built from view_state.rhi's buffers is fine because
        // the actual draws bind a per-view SRB (built below) that shares
        // that layout. Qt validates layout, not handles, when the pipeline
        // is created, then re-validates per setShaderResources call.
        std::unique_ptr<QRhiShaderResourceBindings> layout_srb(
            rhi->newShaderResourceBindings());
        QRhiShaderResourceBinding ubo_binding =
            QRhiShaderResourceBinding::uniformBuffer(
                0,
                QRhiShaderResourceBinding::VertexStage
                    | QRhiShaderResourceBinding::FragmentStage,
                primary_srb_entry.ubo.get(),
                0,
                k_series_ubo_bytes);
        // RHI series primitives bind only a UBO through the SRB. Sample data
        // rides vertex attributes instead of SSBOs so the D3D11 backend's
        // SM 5.0 vertex shader has zero UAVs.
        layout_srb->setBindings({ubo_binding});
        layout_srb->create();

        cached.pipeline.reset(rhi->newGraphicsPipeline());
        cached.pipeline->setShaderStages({
            { QRhiShaderStage::Vertex,   cached.vert },
            { QRhiShaderStage::Fragment, cached.frag }
        });

        QRhiVertexInputLayout vlayout;
        if (is_dots) {
            QRhiVertexInputBinding ib0(
                static_cast<quint32>(sizeof(gpu_sample_t)),
                QRhiVertexInputBinding::PerInstance,
                1);
            vlayout.setBindings({ib0});
            QRhiVertexInputAttribute a0(
                0, 0, QRhiVertexInputAttribute::Float,
                static_cast<quint32>(offsetof(gpu_sample_t, t_rel)));
            QRhiVertexInputAttribute a1(
                0, 1, QRhiVertexInputAttribute::Float,
                static_cast<quint32>(offsetof(gpu_sample_t, y)));
            vlayout.setAttributes({a0, a1});
        }
        else
        if (is_area) {
            const quint32 stride = static_cast<quint32>(sizeof(gpu_sample_t));
            QRhiVertexInputBinding ib_p0(stride, QRhiVertexInputBinding::PerInstance, 1);
            QRhiVertexInputBinding ib_p1(stride, QRhiVertexInputBinding::PerInstance, 1);
            vlayout.setBindings({ib_p0, ib_p1});

            const quint32 t_offset =
                static_cast<quint32>(offsetof(gpu_sample_t, t_rel));
            const quint32 y_offset =
                static_cast<quint32>(offsetof(gpu_sample_t, y));
            QRhiVertexInputAttribute a_x0(0, 0, QRhiVertexInputAttribute::Float, t_offset);
            QRhiVertexInputAttribute a_y0(0, 1, QRhiVertexInputAttribute::Float, y_offset);
            QRhiVertexInputAttribute a_x1(1, 4, QRhiVertexInputAttribute::Float, t_offset);
            QRhiVertexInputAttribute a_y1(1, 5, QRhiVertexInputAttribute::Float, y_offset);
            vlayout.setAttributes({a_x0, a_y0, a_x1, a_y1});
        }
        else {
            // LINE binds the line_window_vbo four times at offsets 0,
            // 16, 32, 48. Each binding has stride sizeof(gpu_sample_t)
            // and steps once per instance, so the bindings form a
            // sliding (prev, p0, p1, next) window over the padded
            // sample array. Only the (t_rel, y) pair is consumed by
            // the LINE vertex shader; the (y_min, y_max) lanes are
            // left unbound.
            const quint32 line_stride =
                static_cast<quint32>(sizeof(gpu_sample_t));
            QRhiVertexInputBinding ib_prev(
                line_stride, QRhiVertexInputBinding::PerInstance, 1);
            QRhiVertexInputBinding ib_p0(
                line_stride, QRhiVertexInputBinding::PerInstance, 1);
            QRhiVertexInputBinding ib_p1(
                line_stride, QRhiVertexInputBinding::PerInstance, 1);
            QRhiVertexInputBinding ib_next(
                line_stride, QRhiVertexInputBinding::PerInstance, 1);
            vlayout.setBindings({ib_prev, ib_p0, ib_p1, ib_next});

            const quint32 ty_offset =
                static_cast<quint32>(offsetof(gpu_sample_t, t_rel));
            QRhiVertexInputAttribute a_prev(
                0, 0, QRhiVertexInputAttribute::Float2, ty_offset);
            QRhiVertexInputAttribute a_p0(
                1, 1, QRhiVertexInputAttribute::Float2, ty_offset);
            QRhiVertexInputAttribute a_p1(
                2, 2, QRhiVertexInputAttribute::Float2, ty_offset);
            QRhiVertexInputAttribute a_next(
                3, 3, QRhiVertexInputAttribute::Float2, ty_offset);
            vlayout.setAttributes({a_prev, a_p0, a_p1, a_next});
        }
        cached.pipeline->setVertexInputLayout(vlayout);
        cached.pipeline->setShaderResourceBindings(layout_srb.get());
        cached.pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);

        QRhiGraphicsPipeline::TargetBlend blend;
        blend.enable = true;
        blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
        blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        blend.srcAlpha = QRhiGraphicsPipeline::One;
        blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        cached.pipeline->setTargetBlends({blend});

        cached.pipeline->setFlags(QRhiGraphicsPipeline::UsesScissor);
        cached.pipeline->setRenderPassDescriptor(current_rpd);
        cached.pipeline->setSampleCount(current_samples);

        if (!cached.pipeline->create()) {
            cached.pipeline.reset();
            return;
        }
        cached.last_rpd = current_rpd;
        cached.last_sample_count = current_samples;
    }

    // Per-view SRBs. Rebuild whenever the bound UBO pointer has changed since
    // last frame (capacity growth replaces QRhiBuffer outright). The SRB
    // captures concrete handles, so reusing one whose buffers were freed is a
    // use-after-free.
    auto ensure_srb = [&](vbo_view_state_t::rhi_buffers_t::srb_entry_t& entry) {
        QRhiBuffer* current_ubo = entry.ubo.get();
        const bool srb_handles_match = entry.srb
            && entry.last_ubo == current_ubo;
        if (srb_handles_match) {
            return;
        }

        entry.srb.reset(rhi->newShaderResourceBindings());
        QRhiShaderResourceBinding ubo_binding =
            QRhiShaderResourceBinding::uniformBuffer(
                0,
                QRhiShaderResourceBinding::VertexStage
                    | QRhiShaderResourceBinding::FragmentStage,
                current_ubo,
                0,
                k_series_ubo_bytes);
        entry.srb->setBindings({ubo_binding});
        entry.srb->create();
        entry.last_ubo = current_ubo;
    };

    ensure_srb(primary_srb_entry);
    if (is_area) {
        ensure_srb(view_state.rhi->area_axis_srb);
    }

    // Build and upload the per-view UBO.
    const auto& layout = ctx.layout;

    Series_view_std140 view_block{};
    std::memcpy(view_block.pmv, glm::value_ptr(ctx.pmv), sizeof(float) * 16);

    glm::vec4 draw_color = series->color;
    if (is_area) {
        draw_color.w *= area_fill_alpha;
        if (ctx.dark_mode && is_default_series_color(draw_color)) {
            draw_color = k_default_series_color_dark;
        }
    }
    if (is_preview) {
        draw_color.w *= static_cast<float>(
            ctx.config ? ctx.config->preview_visibility : 1.0);
    }
    view_block.color[0] = draw_color.r;
    view_block.color[1] = draw_color.g;
    view_block.color[2] = draw_color.b;
    view_block.color[3] = draw_color.a;

    const float t_min_rel = static_cast<float>(
        (is_preview ? ctx.t_available_min : ctx.t0) - origin_ns) * 1.0e-9f;
    const float t_max_rel = static_cast<float>(
        (is_preview ? ctx.t_available_max : ctx.t1) - origin_ns) * 1.0e-9f;
    view_block.t_min = t_min_rel;
    view_block.t_max = t_max_rel;
    view_block.v_min = is_preview ? ctx.preview_v0 : ctx.v0;
    view_block.v_max = is_preview ? ctx.preview_v1 : ctx.v1;

    if (is_preview) {
        const double preview_top =
            double(ctx.win_h) - ctx.adjusted_preview_height;
        view_block.y_offset = static_cast<float>(preview_top);
        view_block.width    = static_cast<float>(ctx.win_w);
        view_block.height   = static_cast<float>(ctx.adjusted_preview_height);
    }
    else {
        view_block.y_offset = 0.0f;
        view_block.width    = static_cast<float>(layout.usable_width);
        view_block.height   = static_cast<float>(layout.usable_height);
    }
    view_block.win_h = static_cast<float>(ctx.win_h);
    view_block.framebuffer_y_up =
        (ctx.rhi && ctx.rhi->isYUpInFramebuffer()) ? 1 : 0;

    if (is_dots) {
        Dot_block_std140 block{};
        block.view              = view_block;
        block.point_diameter_px = point_diameter_px;
        if (updates) {
            updates->updateDynamicBuffer(
                primary_srb_entry.ubo.get(), 0, sizeof(block), &block);
        }
    }
    else
    if (is_area) {
        const Color_palette palette = ctx.dark_mode
            ? Color_palette::dark()
            : Color_palette::light();

        Area_block_std140 fill_block{};
        fill_block.view = view_block;
        fill_block.zero_axis_color[0] = palette.grid_line.r;
        fill_block.zero_axis_color[1] = palette.grid_line.g;
        fill_block.zero_axis_color[2] = palette.grid_line.b;
        fill_block.zero_axis_color[3] = palette.grid_line.a;
        fill_block.axis_pass = 0;

        Area_block_std140 axis_block = fill_block;
        axis_block.axis_pass = 1;

        if (updates) {
            updates->updateDynamicBuffer(
                view_state.rhi->area_fill_srb.ubo.get(), 0,
                sizeof(fill_block), &fill_block);
            updates->updateDynamicBuffer(
                view_state.rhi->area_axis_srb.ubo.get(), 0,
                sizeof(axis_block), &axis_block);
        }
    }
    else {
        Line_block_std140 block{};
        block.view = view_block;
        block.line_px        = line_width_px;
        block.snap_to_pixels = (ctx.config && ctx.config->snap_lines_to_pixels)
            ? 1 : 0;
        if (updates) {
            updates->updateDynamicBuffer(
                primary_srb_entry.ubo.get(), 0, sizeof(block), &block);
        }
    }
}

void Series_renderer::rhi_record_series_primitive(
    const frame_context_t& ctx,
    Display_style primitive_style,
    vbo_view_state_t& view_state,
    const view_render_result_t& view_result,
    bool is_preview)
{
    QRhiCommandBuffer* cb = ctx.cb;
    if (!cb) {
        return;
    }

    const bool is_dots = (primitive_style == Display_style::DOTS);
    const bool is_area = (primitive_style == Display_style::AREA);
    const GLsizei count = view_result.count;
    if (count <= 0) {
        return;
    }
    if (!is_dots && count < 2) {
        return;
    }
    if (!view_state.rhi) {
        return;
    }

    rhi_state_t::pipeline_key_t key{
        is_dots
            ? rhi_state_t::pipeline_kind_t::DOTS
            : (is_area ? rhi_state_t::pipeline_kind_t::AREA
                       : rhi_state_t::pipeline_kind_t::LINE)
    };
    auto pipe_it = m_rhi_state->pipelines.find(key);
    if (pipe_it == m_rhi_state->pipelines.end() || !pipe_it->second.pipeline) {
        return;
    }
    auto& cached = pipe_it->second;

    auto& srb_entry = is_dots
        ? view_state.rhi->dots_srb
        : (is_area ? view_state.rhi->area_fill_srb : view_state.rhi->line_srb);
    if (!srb_entry.srb) {
        return;
    }
    if (is_area && !view_state.rhi->area_axis_srb.srb) {
        return;
    }

    cb->setGraphicsPipeline(cached.pipeline.get());

    const auto& layout = ctx.layout;
    const auto to_scissor_y = [&](double top, double height) -> int {
        return static_cast<int>(std::lround(double(ctx.win_h) - (top + height)));
    };
    QRhiScissor scissor;
    if (is_preview) {
        const double preview_top =
            double(ctx.win_h) - ctx.adjusted_preview_height;
        scissor = QRhiScissor(
            0,
            to_scissor_y(preview_top, ctx.adjusted_preview_height),
            ctx.win_w,
            static_cast<int>(std::max(1.0, ctx.adjusted_preview_height)));
    }
    else {
        scissor = QRhiScissor(
            0,
            to_scissor_y(0.0, layout.usable_height),
            static_cast<int>(std::max(1.0, layout.usable_width)),
            static_cast<int>(std::max(1.0, layout.usable_height)));
    }
    cb->setScissor(scissor);

    if (is_dots) {
        cb->setShaderResources(srb_entry.srb.get());
        if (view_state.rhi->vbo) {
            // Encode the per-instance "skip first N samples" by offsetting the
            // vertex-buffer binding rather than passing a non-zero
            // firstInstance. D3D11 does not support firstInstance > 0 without
            // BaseInstance semantics, and emulating it on other backends can
            // be slow. The buffer itself is bound at sample[first], and the
            // shader reads samples relative to that origin.
            const quint32 vbo_offset =
                static_cast<quint32>(view_result.first) *
                static_cast<quint32>(sizeof(gpu_sample_t));
            QRhiCommandBuffer::VertexInput input{
                view_state.rhi->vbo.get(), vbo_offset};
            cb->setVertexInput(0, 1, &input);
        }
        cb->draw(4, static_cast<quint32>(count));
    }
    else
    if (is_area) {
        const quint32 instance_count = static_cast<quint32>(count - 1);
        if (instance_count > 0 && view_state.rhi->vbo) {
            const quint32 stride =
                static_cast<quint32>(sizeof(gpu_sample_t));
            const quint32 vbo_offset =
                static_cast<quint32>(view_result.first) * stride;
            QRhiBuffer* const vbo = view_state.rhi->vbo.get();
            const QRhiCommandBuffer::VertexInput inputs[2] = {
                { vbo, vbo_offset },
                { vbo, vbo_offset + stride }
            };
            cb->setVertexInput(0, 2, inputs);

            cb->setShaderResources(srb_entry.srb.get());
            cb->draw(6, instance_count);

            cb->setShaderResources(view_state.rhi->area_axis_srb.srb.get());
            cb->draw(4, instance_count);
        }
    }
    else {
        cb->setShaderResources(srb_entry.srb.get());
        // LINE: triangle-strip quads per segment, one instance per segment.
        // The four vertex inputs all reference the line_window_vbo at
        // increasing element offsets so each instance reads a sliding
        // (prev, p0, p1, next) window across the padded sample array.
        const quint32 instance_count = static_cast<quint32>(count - 1);
        if (instance_count > 0 && view_state.rhi->line_window_vbo) {
            const quint32 stride =
                static_cast<quint32>(sizeof(gpu_sample_t));
            QRhiBuffer* const win = view_state.rhi->line_window_vbo.get();
            const QRhiCommandBuffer::VertexInput inputs[4] = {
                { win, 0u },
                { win, 1u * stride },
                { win, 2u * stride },
                { win, 3u * stride }
            };
            cb->setVertexInput(0, 4, inputs);
            cb->draw(4, instance_count);
        }
    }
}
#endif // VNM_PLOT_HAS_QRHI

} // namespace vnm::plot
