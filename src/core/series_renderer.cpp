#include <vnm_plot/core/series_renderer.h>
#include <vnm_plot/core/algo.h>
#include <vnm_plot/core/asset_loader.h>
#include <vnm_plot/core/constants.h>
#include <vnm_plot/core/plot_config.h>
#include <vnm_plot/core/time_units.h>
#include <vnm_plot/qt/qrhi_series_layer.h>
#include "rhi_helpers.h"

#include <glm/gtc/type_ptr.hpp>
#include <rhi/qrhi.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_set>

namespace vnm::plot {
using detail::choose_lod_level;
using detail::choose_origin_ns;
using detail::compute_lod_scales;
using detail::k_scissor_pad_px;
using detail::lower_bound_timestamp;
using detail::positive_span_ns_for_signed_api;
using detail::upper_bound_timestamp;

namespace {

using detail::load_qsb;

constexpr glm::vec4 k_default_series_color(0.16f, 0.45f, 0.64f, 1.0f);
constexpr glm::vec4 k_default_series_color_dark(0.30f, 0.63f, 0.88f, 1.0f);
constexpr float k_default_color_epsilon = 0.01f;

bool is_default_series_color(const glm::vec4& color)
{
    return glm::all(glm::lessThan(
        glm::abs(color - k_default_series_color),
        glm::vec4(k_default_color_epsilon)));
}

std::size_t line_window_sample_count(
    std::int32_t          source_count,
    Series_interpolation  interpolation)
{
    if (source_count <= 0) {
        return 0;
    }
    if (interpolation == Series_interpolation::STEP_AFTER) {
        return static_cast<std::size_t>(source_count) * 2u - 1u;
    }
    return static_cast<std::size_t>(source_count);
}

} // anonymous namespace

struct Series_renderer::gpu_sample_t
{
    float t_rel;
    float y;
    float y_min;
    float y_max;
};

static_assert(offsetof(series_view_uniform_std140_t, pmv)      ==   0, "Series_view_t std140 pmv offset");
static_assert(offsetof(series_view_uniform_std140_t, color)    ==  64, "Series_view_t std140 color offset");
static_assert(offsetof(series_view_uniform_std140_t, t_min)    ==  80, "Series_view_t std140 t_min offset");
static_assert(offsetof(series_view_uniform_std140_t, t_max)    ==  84, "Series_view_t std140 t_max offset");
static_assert(offsetof(series_view_uniform_std140_t, v_min)    ==  88, "Series_view_t std140 v_min offset");
static_assert(offsetof(series_view_uniform_std140_t, v_max)    ==  92, "Series_view_t std140 v_max offset");
static_assert(offsetof(series_view_uniform_std140_t, width)    ==  96, "Series_view_t std140 width offset");
static_assert(offsetof(series_view_uniform_std140_t, height)   == 100, "Series_view_t std140 height offset");
static_assert(offsetof(series_view_uniform_std140_t, y_offset) == 104, "Series_view_t std140 y_offset offset");
static_assert(offsetof(series_view_uniform_std140_t, win_h)    == 108, "Series_view_t std140 win_h offset");
static_assert(offsetof(series_view_uniform_std140_t, framebuffer_y_up) == 112,
    "Series_view_t framebuffer_y_up offset");
static_assert(sizeof(series_view_uniform_std140_t) == 128, "Series_view_t std140 size");

series_view_uniform_std140_t make_series_view_uniform(
    const frame_context_t& frame,
    const series_data_t& series,
    const sample_window_t& window)
{
    series_view_uniform_std140_t uniform{};
    std::memcpy(uniform.pmv, glm::value_ptr(frame.pmv), sizeof(float) * 16);

    glm::vec4 draw_color = series.color;
    draw_color.w *= window.window_alpha;
    uniform.color[0] = draw_color.r;
    uniform.color[1] = draw_color.g;
    uniform.color[2] = draw_color.b;
    uniform.color[3] = draw_color.a;

    uniform.t_min = detail::to_view_seconds(window.t_min_ns, window.t_origin_ns);
    uniform.t_max = detail::to_view_seconds(window.t_max_ns, window.t_origin_ns);
    uniform.v_min = window.v_min;
    uniform.v_max = window.v_max;
    uniform.width = window.width_px;
    uniform.height = window.height_px;
    uniform.y_offset = window.y_offset_px;
    uniform.win_h = static_cast<float>(frame.win_h);
    uniform.framebuffer_y_up =
        (frame.rhi && frame.rhi->isYUpInFramebuffer()) ? 1 : 0;
    return uniform;
}

// Per-view RHI resources held off-line so the public header forward-declares
// the type. The unique_ptr<rhi_buffers_t> in vbo_view_state_t pulls in QRhiBuffer
// transitively through this struct's members.
//
// The SRB is per-view because it captures concrete buffer handles. The
// pipeline state object stays cached in rhi_state_t (its descriptor only
// depends on layout, not on which buffer the SRB references). When a
// buffer is reallocated to grow capacity, the SRB's last_* pointers no
// longer match and the SRB is rebuilt before the next draw.
struct Series_renderer::vbo_view_state_t::rhi_buffers_t
{
    std::unique_ptr<QRhiBuffer> vbo;
    std::unique_ptr<QRhiBuffer> ubo;
    // LINE-specific per-frame buffer that holds the active sample window
    // padded with leading and trailing duplicates. Bound four times as a
    // vertex buffer at consecutive gpu_sample_t offsets with per-instance
    // stepping, so the vertex shader receives prev/p0/p1/next as plain
    // attributes. This side-steps the SM 5.0 UAV restriction that blocks
    // SSBOs in the D3D11 vertex stage.
    std::unique_ptr<QRhiBuffer> line_window_vbo;

    // Per-(view, primitive_style) UBO + SRB cache. Each drawable primitive
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

};

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

    struct view_ubo_key_t
    {
        int series_id = 0;
        Series_view_kind view_kind = Series_view_kind::MAIN;

        bool operator==(const view_ubo_key_t& o) const noexcept
        {
            return series_id == o.series_id && view_kind == o.view_kind;
        }
    };

    struct view_ubo_key_hash_t
    {
        std::size_t operator()(const view_ubo_key_t& k) const noexcept
        {
            return (static_cast<std::size_t>(k.series_id) << 1)
                ^ static_cast<std::size_t>(k.view_kind);
        }
    };

    struct view_ubo_state_t
    {
        std::unique_ptr<QRhiBuffer> buffer;
        std::uint64_t last_frame_used = 0;
    };

    struct qrhi_layer_program_key_t
    {
        int series_id = 0;
        Series_view_kind view_kind = Series_view_kind::MAIN;
        std::string layer_id;
        std::uint64_t layer_revision = 0;
        const void* data_identity = nullptr;
        std::uint64_t layout_key = 0;
        QRhi* rhi = nullptr;

        bool operator==(const qrhi_layer_program_key_t& o) const noexcept
        {
            return series_id == o.series_id
                && view_kind == o.view_kind
                && layer_id == o.layer_id
                && layer_revision == o.layer_revision
                && data_identity == o.data_identity
                && layout_key == o.layout_key
                && rhi == o.rhi;
        }
    };

    struct qrhi_layer_program_key_hash_t
    {
        std::size_t operator()(const qrhi_layer_program_key_t& k) const noexcept
        {
            std::size_t h = std::hash<int>{}(k.series_id);
            h ^= std::hash<int>{}(static_cast<int>(k.view_kind)) + 0x9e3779b9u + (h << 6) + (h >> 2);
            h ^= std::hash<std::string>{}(k.layer_id) + 0x9e3779b9u + (h << 6) + (h >> 2);
            h ^= std::hash<std::uint64_t>{}(k.layer_revision) + 0x9e3779b9u + (h << 6) + (h >> 2);
            h ^= std::hash<const void*>{}(k.data_identity) + 0x9e3779b9u + (h << 6) + (h >> 2);
            h ^= std::hash<std::uint64_t>{}(k.layout_key) + 0x9e3779b9u + (h << 6) + (h >> 2);
            h ^= std::hash<QRhi*>{}(k.rhi) + 0x9e3779b9u + (h << 6) + (h >> 2);
            return h;
        }
    };

    struct qrhi_layer_data_key_t
    {
        std::size_t lod_level = 0;
        std::uint64_t sample_sequence = 0;
        std::int64_t t_origin_ns = 0;
        std::int32_t first = 0;
        std::int32_t count = 0;
        bool hold_last_forward = false;
        Series_interpolation interpolation = Series_interpolation::LINEAR;

        bool operator==(const qrhi_layer_data_key_t& o) const noexcept
        {
            return lod_level == o.lod_level
                && sample_sequence == o.sample_sequence
                && t_origin_ns == o.t_origin_ns
                && first == o.first
                && count == o.count
                && hold_last_forward == o.hold_last_forward
                && interpolation == o.interpolation;
        }
    };

    struct qrhi_layer_cache_entry_t
    {
        std::unique_ptr<Qrhi_series_layer_state> state;
        qrhi_layer_data_key_t data_key;
        bool has_data_key = false;
        std::uint64_t last_frame_used = 0;
    };

    struct prepared_layer_record_t
    {
        Qrhi_series_layer_state* state = nullptr;
        const series_data_t* series = nullptr;
        sample_window_t window;
        QRhiBuffer* view_ubo = nullptr;
    };

    std::unordered_map<pipeline_key_t, rhi_pipeline_t, pipeline_key_hash_t> pipelines;
    std::unordered_map<view_ubo_key_t, view_ubo_state_t, view_ubo_key_hash_t> view_ubos;
    std::unordered_map<
        qrhi_layer_program_key_t,
        qrhi_layer_cache_entry_t,
        qrhi_layer_program_key_hash_t> qrhi_layer_cache;
    std::vector<prepared_layer_record_t> prepared_layers;
    QShader cached_dot_vert;
    QShader cached_dot_frag;
    QShader cached_line_vert;
    QShader cached_line_frag;
    QShader cached_area_vert;
    QShader cached_area_frag;
    bool    shaders_loaded = false;

    QRhi*                last_rhi             = nullptr;
    QRhiResourceUpdateBatch* pending_updates  = nullptr;

    // Per-frame draw plan computed in prepare() and replayed in render().
    // The vector lives on the renderer rather than in a stack
    // frame because the prepare() / render() split happens across two host
    // calls, with cb->beginPass(batch) sandwiched in between. The fp32
    // origins computed in prepare() ride inside the per-view UBO/staging
    // bytes already submitted to the resource-update batch, so they do
    // not need to be cached here for render() to read back.
    std::vector<series_draw_state_t> frame_draw_states;
    bool         frame_preview_visible   = false;
    // True if prepare() filled this plan. Reset after render() consumes it
    // so a stray render() without a matching prepare() is a no-op.
    bool         frame_plan_ready        = false;
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
//         Series_view_t view;             // 128 bytes
//         float         line_px;          // for LINE
//         int           snap;             // for LINE
//         float         dot_px;           // for DOTS
//         int           interpolation;    // for AREA
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

// Whole-block layout: Series_view + AREA trailing values. Padded out to a
// 16-byte multiple.
struct Area_block_std140
{
    Series_view_std140 view;                // offset 0
    int                interpolation;       // offset 128
    int                _pad0;               // offset 132
    int                _pad1;               // offset 136
    int                _pad2;               // offset 140
};
static_assert(sizeof(Area_block_std140) == 144, "Area_block_std140 must be a multiple of 16");
static_assert(offsetof(Area_block_std140, interpolation) == 128, "Area_block interpolation offset");

constexpr std::uint32_t k_series_ubo_bytes = 144;
static_assert(sizeof(Line_block_std140) <= k_series_ubo_bytes, "ubo bytes fit LINE block");
static_assert(sizeof(Dot_block_std140)  <= k_series_ubo_bytes, "ubo bytes fit DOTS block");
static_assert(sizeof(Area_block_std140) == k_series_ubo_bytes, "ubo bytes match AREA block");

class Series_renderer::Builtin_series_layer_state final : public Qrhi_series_layer_state
{
public:
    Builtin_series_layer_state(
        Series_renderer& renderer,
        Display_style primitive_style,
        vbo_view_state_t& view_state)
    :
        m_renderer(renderer),
        m_primitive_style(primitive_style),
        m_view_state(view_state)
    {}

    bool prepare(const qrhi_series_prepare_context_t& ctx) override
    {
        if (!ctx.frame || !ctx.series) {
            return false;
        }

        const float line_width_px = ctx.frame->config
            ? static_cast<float>(ctx.frame->config->line_width_px) : 1.0f;
        const float point_diameter_px = ctx.frame->config
            ? static_cast<float>(ctx.frame->config->point_diameter_px) : 1.0f;
        const float area_fill_alpha = ctx.frame->config
            ? static_cast<float>(ctx.frame->config->area_fill_alpha) : 0.3f;

        return m_renderer.rhi_prepare_series_primitive(
            *ctx.frame,
            ctx.series,
            ctx.window.access,
            m_primitive_style,
            m_view_state,
            view_result_from_window(ctx.window),
            line_width_px,
            point_diameter_px,
            area_fill_alpha);
    }

    void record(const qrhi_series_record_context_t& ctx) override
    {
        if (!ctx.frame) {
            return;
        }

        m_renderer.rhi_record_series_primitive(
            *ctx.frame,
            m_primitive_style,
            m_view_state,
            view_result_from_window(ctx.window));
    }

private:
    static view_render_result_t view_result_from_window(const sample_window_t& window)
    {
        view_render_result_t result;
        result.can_draw           = window.count > 0;
        result.first              = window.first;
        result.count              = window.count;
        result.applied_level      = window.lod_level;
        result.applied_pps        = window.pixels_per_sample;
        result.cached_snapshot    = window.snapshot;
        result.sample_sequence    = window.sample_sequence;
        result.t_min_ns           = window.t_min_ns;
        result.t_max_ns           = window.t_max_ns;
        result.t_origin_ns        = window.t_origin_ns;
        result.hold_last_forward  = window.hold_last_forward;
        result.hold_timestamp_ns  = window.hold_timestamp_ns;
        result.v_min              = window.v_min;
        result.v_max              = window.v_max;
        result.width_px           = window.width_px;
        result.height_px          = window.height_px;
        result.y_offset_px        = window.y_offset_px;
        result.window_alpha       = window.window_alpha;
        result.interpolation      = window.interpolation;
        return result;
    }

    Series_renderer& m_renderer;
    Display_style    m_primitive_style = Display_style::LINE;
    vbo_view_state_t& m_view_state;
};

class Series_renderer::Builtin_series_layer final : public Qrhi_series_layer
{
public:
    Builtin_series_layer(
        Series_renderer& renderer,
        Display_style primitive_style,
        vbo_view_state_t& view_state)
    :
        m_renderer(renderer),
        m_primitive_style(primitive_style),
        m_view_state(view_state)
    {}

    std::string_view id() const override
    {
        switch (m_primitive_style) {
            case Display_style::AREA: return "vnm_plot.builtin.area";
            case Display_style::DOTS: return "vnm_plot.builtin.dots";
            case Display_style::LINE:
            default:                  return "vnm_plot.builtin.line";
        }
    }

    std::uint64_t revision() const override { return 1; }

    int z_order() const override
    {
        switch (m_primitive_style) {
            case Display_style::AREA: return -10;
            case Display_style::DOTS: return  10;
            case Display_style::LINE:
            default:                  return   0;
        }
    }

    bool draws_view(Series_view_kind view_kind) const override
    {
        (void)view_kind;
        return true;
    }

    std::unique_ptr<Qrhi_series_layer_state> create_state(QRhi& rhi) const override
    {
        (void)rhi;
        return std::make_unique<Builtin_series_layer_state>(
            m_renderer,
            m_primitive_style,
            m_view_state);
    }

private:
    Series_renderer& m_renderer;
    Display_style    m_primitive_style = Display_style::LINE;
    vbo_view_state_t& m_view_state;
};

Series_renderer::Series_renderer()
:
    m_rhi_state(std::make_unique<rhi_state_t>())
{}

Series_renderer::~Series_renderer() = default;

void Series_renderer::initialize(Asset_loader& asset_loader)
{
    m_asset_loader = &asset_loader;
}

void Series_renderer::cleanup_resources()
{
    clear_frame_snapshot_caches();
    for (auto& [_, state] : m_vbo_states) {
        state.main_view.reset();
        state.preview_view.reset();
    }
    m_vbo_states.clear();
    m_logged_errors.clear();

    m_rhi_state->pipelines.clear();
    for (auto& [key, entry] : m_rhi_state->qrhi_layer_cache) {
        if (entry.state) {
            entry.state->cleanup_qrhi_resources(key.rhi);
        }
    }
    m_rhi_state->qrhi_layer_cache.clear();
    m_rhi_state->view_ubos.clear();
    m_rhi_state->prepared_layers.clear();
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
    m_rhi_state->prepared_layers.clear();
    m_rhi_state->frame_plan_ready = false;
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

Series_renderer::view_render_result_t Series_renderer::plan_view(
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
    Series_interpolation interpolation,
    Snapshot_requirement snapshot_requirement,
    vnm::plot::Profiler* profiler)
{
    view_render_result_t result;
    result.interpolation = interpolation;
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

    const auto acquire_frame_snapshot = [&](std::size_t level) {
        VNM_PLOT_PROFILE_SCOPE(profiler, "process_view.try_snapshot");
        snapshot_result_t snapshot_result;
        if (shared_state.cached_snapshot_frame_id == frame_id &&
            shared_state.cached_snapshot_level == level &&
            shared_state.cached_snapshot_source == &data_source &&
            shared_state.cached_snapshot)
        {
            snapshot_result.snapshot = shared_state.cached_snapshot;
            snapshot_result.status = snapshot_result_t::Snapshot_status::READY;
            return snapshot_result;
        }

        snapshot_result = data_source.try_snapshot(level);
        if (snapshot_result) {
            shared_state.cached_snapshot_frame_id = frame_id;
            shared_state.cached_snapshot_level = level;
            shared_state.cached_snapshot_source = &data_source;
            shared_state.cached_snapshot = snapshot_result.snapshot;
            shared_state.cached_snapshot_hold = snapshot_result.snapshot.hold;
        }
        return snapshot_result;
    };

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
        r.sample_sequence = view_state.last_sequence;
        r.t_min_ns = t_min_ns;
        r.t_max_ns = t_max_ns;
        r.t_origin_ns = t_origin_ns;
        r.hold_last_forward = view_state.last_hold_last_forward;
        r.hold_timestamp_ns = view_state.last_hold_last_forward ? t_max_ns : 0;
        r.width_px = static_cast<float>(width_px);
        r.interpolation = interpolation;
    };
    const auto try_stale_fallback = [&](view_render_result_t& r) -> bool {
        const void* current_identity = data_source.identity();
        // The cached VBO holds samples rebased against uploaded_t_origin_ns;
        // reusing it under a moved origin would draw at the wrong x positions
        // because set_common_uniforms feeds the new view_origin_ns regardless.
        const bool identity_ok =
            (view_state.cached_data_identity != nullptr) &&
            (view_state.cached_data_identity == current_identity) &&
            view_state.has_uploaded_vbo &&
            (view_state.last_count > 0) &&
            (view_state.last_empty_window_behavior == empty_window_behavior) &&
            (view_state.last_interpolation == interpolation) &&
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
            view_state.has_uploaded_vbo &&
            view_state.last_count > 0 &&
            view_state.cached_data_identity == data_source.identity() &&
            view_state.last_t_min == t_min_ns &&
            view_state.last_t_max == t_max_ns &&
            view_state.last_width_px == width_px &&
            view_state.last_empty_window_behavior == empty_window_behavior &&
            view_state.last_interpolation == interpolation &&
            view_state.uploaded_t_origin_ns == t_origin_ns)
        {
            if (snapshot_requirement == Snapshot_requirement::Frame_snapshot_required) {
                snapshot_result_t snapshot_result = acquire_frame_snapshot(applied_level);
                if (snapshot_result) {
                    if (snapshot_result.snapshot.sequence == current_seq) {
                        load_cached_result(result, applied_level);
                        result.cached_snapshot = snapshot_result.snapshot;
                        result.cached_snapshot_hold = snapshot_result.snapshot.hold;
                        return result;
                    }
                    // The source advanced between current_sequence() and
                    // try_snapshot(); fall through to replan against the
                    // newer frame-scoped snapshot instead of pairing cached
                    // first/count metadata with a different snapshot.
                }
                else {
                    load_cached_result(result, applied_level);
                    return result;
                }
            }
            else {
                load_cached_result(result, applied_level);
                return result;
            }
        }

        snapshot_result_t snapshot_result = acquire_frame_snapshot(applied_level);

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

        std::int32_t count = static_cast<std::int32_t>(last_idx - first_idx);
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

        view_state.last_sequence = snapshot.sequence;
        view_state.cached_data_identity = data_source.identity();
        view_state.uploaded_t_origin_ns = t_origin_ns;
        view_state.last_snapshot_elements = snapshot.count;
        view_state.last_first = static_cast<std::int32_t>(first_idx);
        view_state.last_count = count;

        view_state.last_lod_level = applied_level;
        view_state.last_t_min = t_min_ns;
        view_state.last_t_max = t_max_ns;
        view_state.last_width_px = width_px;
        view_state.last_empty_window_behavior = empty_window_behavior;
        view_state.last_interpolation = interpolation;

        result.can_draw = true;
        result.first = view_state.last_first;
        result.count = view_state.last_count;
        result.applied_level = applied_level;
        result.applied_pps = base_pps * static_cast<double>(applied_scale);
        view_state.last_applied_pps = result.applied_pps;
        view_state.last_hold_last_forward = hold_last_forward;
        result.cached_snapshot = snapshot;
        result.cached_snapshot_hold = snapshot.hold;
        result.sample_sequence = snapshot.sequence;
        result.t_min_ns = t_min_ns;
        result.t_max_ns = t_max_ns;
        result.t_origin_ns = t_origin_ns;
        result.hold_last_forward = hold_last_forward;
        result.hold_timestamp_ns = hold_last_forward ? t_max_ns : 0;
        result.width_px = static_cast<float>(width_px);
        result.interpolation = interpolation;
        break;
    }

    return result;
}

void Series_renderer::prepare(
    const frame_context_t& ctx,
    const std::map<int, std::shared_ptr<const series_data_t>>& series)
{
    m_rhi_state->frame_draw_states.clear();
    m_rhi_state->prepared_layers.clear();
    m_rhi_state->frame_plan_ready = false;

    const auto clear_retired_series_resources = [&]() {
        clear_frame_snapshot_caches();
        m_vbo_states.clear();
        for (auto& [key, entry] : m_rhi_state->qrhi_layer_cache) {
            if (entry.state) {
                entry.state->cleanup_qrhi_resources(key.rhi);
            }
        }
        m_rhi_state->qrhi_layer_cache.clear();
        m_rhi_state->view_ubos.clear();
        m_rhi_state->prepared_layers.clear();
    };

    if (series.empty()) {
        clear_retired_series_resources();
        return;
    }
    if (!m_asset_loader) {
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

    QRhi*                    rhi         = ctx.rhi;
    QRhiResourceUpdateBatch* rhi_updates = ctx.rhi_updates;
    if (m_rhi_state->last_rhi && m_rhi_state->last_rhi != rhi) {
        for (auto& [key, entry] : m_rhi_state->qrhi_layer_cache) {
            if (entry.state) {
                entry.state->cleanup_qrhi_resources(key.rhi);
            }
        }
        m_rhi_state->qrhi_layer_cache.clear();
        m_rhi_state->view_ubos.clear();
        m_rhi_state->prepared_layers.clear();
    }
    m_rhi_state->last_rhi = rhi;
    m_rhi_state->pending_updates = rhi_updates;

    for (auto it = m_vbo_states.begin(); it != m_vbo_states.end(); ) {
        if (series.find(it->first) == series.end()) {
            it = m_vbo_states.erase(it);
        }
        else {
            ++it;
        }
    }

    auto& draw_states = m_rhi_state->frame_draw_states;
    draw_states.reserve(series.size());

    const double preview_visibility = ctx.config ? ctx.config->preview_visibility : 1.0;
    const bool preview_visible = ctx.adjusted_preview_height > 0.0 && preview_visibility > 0.0;
    m_rhi_state->frame_preview_visible = preview_visible;

    const std::int64_t main_span_ns = positive_span_ns_for_signed_api(ctx.t0, ctx.t1);
    const std::int64_t preview_span_ns = positive_span_ns_for_signed_api(
        ctx.t_available_min,
        ctx.t_available_max);
    const std::int64_t main_origin_ns = choose_origin_ns(ctx.t0, main_span_ns);
    const std::int64_t preview_origin_ns = preview_visible
        ? choose_origin_ns(ctx.t_available_min, preview_span_ns)
        : main_origin_ns;

    enum class Error_cat : uint32_t {
        PREVIEW_MISSING_SOURCE,
        MISSING_SHADER
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
        Series_interpolation main_interpolation = s->interpolation;
        const auto has_layer_for_view = [&](Series_view_kind view_kind) {
            return std::any_of(
                s->qrhi_layers.begin(),
                s->qrhi_layers.end(),
                [view_kind](const auto& layer) {
                    return layer && layer->draws_view(view_kind);
                });
        };
        const bool has_main_layer = has_layer_for_view(Series_view_kind::MAIN);
        if (!main_style && !has_main_layer) {
            continue;
        }

        const bool has_preview_config = s->has_preview_config();
        Data_source* preview_source = nullptr;
        const Data_access_policy* preview_access = nullptr;
        Display_style preview_style = Display_style::NONE;
        Series_interpolation preview_interpolation = Series_interpolation::LINEAR;
        bool preview_matches_main = false;
        bool preview_valid = false;
        bool has_preview_layer = false;

        if (preview_visible) {
            preview_source = s->preview_source();
            preview_access = &s->preview_access();
            preview_style = s->effective_preview_style();
            preview_interpolation = s->effective_preview_interpolation();
            preview_matches_main = s->preview_matches_main();
            has_preview_layer = has_layer_for_view(Series_view_kind::PREVIEW);

            if (has_preview_config && !preview_source) {
                log_error_once(Error_cat::PREVIEW_MISSING_SOURCE, id,
                    "Preview config set but preview data_source is null (series "
                        + std::to_string(id) + ")");
                preview_style = Display_style::NONE;
            }

            if (preview_source &&
                preview_access &&
                (!!preview_style || has_preview_layer))
            {
                preview_valid = true;
            }
            else {
                preview_source = nullptr;
            }
        }

        auto& vbo_state = m_vbo_states[id];

        std::vector<std::size_t> main_scales = compute_lod_scales(*main_source);
        std::vector<std::size_t> preview_scales;
        if (preview_valid) {
            if (preview_matches_main) {
                preview_scales = main_scales;
            }
            else {
                preview_scales = compute_lod_scales(*preview_source);
            }
        }

        const std::size_t prev_lod_level = vbo_state.main_view.last_lod_level;
        auto main_result = plan_view(
            vbo_state.main_view, vbo_state, m_frame_id, *main_source,
            main_access, main_scales,
            ctx.t0, ctx.t1, main_origin_ns,
            layout.usable_width, s->empty_window_behavior, main_interpolation,
            has_main_layer
                ? Snapshot_requirement::Frame_snapshot_required
                : Snapshot_requirement::Optional,
            profiler);
        main_result.v_min = ctx.v0;
        main_result.v_max = ctx.v1;
        main_result.height_px = static_cast<float>(layout.usable_height);
        main_result.y_offset_px = 0.0f;
        main_result.window_alpha = 1.0f;
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
            preview_result = plan_view(
                vbo_state.preview_view, vbo_state, m_frame_id, *preview_source,
                *preview_access, preview_scales,
                ctx.t_available_min, ctx.t_available_max, preview_origin_ns,
                ctx.win_w, s->empty_window_behavior, preview_interpolation,
                has_preview_layer
                    ? Snapshot_requirement::Frame_snapshot_required
                    : Snapshot_requirement::Optional,
                profiler);
            const double preview_top =
                double(ctx.win_h) - ctx.adjusted_preview_height;
            preview_result.v_min = ctx.preview_v0;
            preview_result.v_max = ctx.preview_v1;
            preview_result.height_px = static_cast<float>(ctx.adjusted_preview_height);
            preview_result.y_offset_px = static_cast<float>(preview_top);
            preview_result.window_alpha = static_cast<float>(preview_visibility);
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
        draw_state.main_interpolation = main_interpolation;
        draw_state.preview_interpolation = preview_interpolation;
        draw_state.vbo_state = &vbo_state;
        draw_state.main_scales = std::move(main_scales);
        draw_state.preview_scales = std::move(preview_scales);
        draw_state.main_result = main_result;
        draw_state.preview_result = preview_result;
        draw_state.has_preview = preview_visible && preview_valid;
        draw_state.preview_matches_main = preview_matches_main;
        draw_states.push_back(std::move(draw_state));
    }

    if (!rhi || !rhi_updates || !ctx.render_target) {
        m_rhi_state->frame_plan_ready = false;
        return;
    }

    const auto make_window = [&](const series_draw_state_t& draw_state,
                                 Series_view_kind view_kind,
                                 const view_render_result_t& view_result,
                                 const Data_access_policy* access) {
        sample_window_t window;
        window.view_kind = view_kind;
        window.snapshot = view_result.cached_snapshot;
        window.access = access;
        window.first = view_result.first;
        window.count = view_result.count;
        window.lod_level = view_result.applied_level;
        window.pixels_per_sample = view_result.applied_pps;
        window.sample_sequence = view_result.sample_sequence;
        window.interpolation = view_result.interpolation;
        window.t_min_ns = view_result.t_min_ns;
        window.t_max_ns = view_result.t_max_ns;
        window.t_origin_ns = view_result.t_origin_ns;
        window.hold_last_forward = view_result.hold_last_forward;
        window.hold_timestamp_ns = view_result.hold_timestamp_ns;
        window.v_min = view_result.v_min;
        window.v_max = view_result.v_max;
        window.width_px = view_result.width_px;
        window.height_px = view_result.height_px;
        window.y_offset_px = view_result.y_offset_px;
        window.window_alpha = view_result.window_alpha;
        (void)draw_state;
        return window;
    };

    const auto ensure_view_ubo =
        [&](int series_id,
            Series_view_kind view_kind,
            const series_data_t& series_data,
            const sample_window_t& window) -> QRhiBuffer*
    {
        rhi_state_t::view_ubo_key_t key{series_id, view_kind};
        auto& state = m_rhi_state->view_ubos[key];
        if (!state.buffer) {
            state.buffer.reset(rhi->newBuffer(
                QRhiBuffer::Dynamic,
                QRhiBuffer::UniformBuffer,
                static_cast<quint32>(sizeof(series_view_uniform_std140_t))));
            if (!state.buffer || !state.buffer->create()) {
                state.buffer.reset();
                return nullptr;
            }
        }

        const series_view_uniform_std140_t uniform =
            make_series_view_uniform(ctx, series_data, window);
        rhi_updates->updateDynamicBuffer(
            state.buffer.get(),
            0,
            sizeof(uniform),
            &uniform);
        state.last_frame_used = m_frame_id;
        return state.buffer.get();
    };

    const auto prepare_view_layers =
        [&](series_draw_state_t& draw_state,
            Series_view_kind view_kind,
            Display_style style,
            vbo_view_state_t& view_state,
            const view_render_result_t& view_result,
            const Data_access_policy* access,
            Data_source* data_source)
    {
        if (!draw_state.series || !view_result.can_draw || view_result.count <= 0) {
            return;
        }

        struct planned_layer_t
        {
            std::shared_ptr<const Qrhi_series_layer> layer;
            bool needs_view_ubo = true;
            bool requires_snapshot = true;
        };

        std::vector<planned_layer_t> layers;
        if (!!(style & Display_style::AREA)) {
            layers.push_back({
                std::make_shared<Builtin_series_layer>(
                    *this,
                    Display_style::AREA,
                    view_state),
                false,
                false});
        }
        if (!!(style & Display_style::LINE)) {
            layers.push_back({
                std::make_shared<Builtin_series_layer>(
                    *this,
                    Display_style::LINE,
                    view_state),
                false,
                false});
        }
        if (!!(style & Display_style::DOTS)) {
            layers.push_back({
                std::make_shared<Builtin_series_layer>(
                    *this,
                    Display_style::DOTS,
                    view_state),
                false,
                false});
        }
        for (const auto& layer : draw_state.series->qrhi_layers) {
            if (layer && layer->draws_view(view_kind)) {
                layers.push_back({layer, true, true});
            }
        }
        std::stable_sort(
            layers.begin(),
            layers.end(),
            [](const auto& a, const auto& b) {
                return a.layer->z_order() < b.layer->z_order();
            });
        if (layers.empty()) {
            return;
        }

        sample_window_t window = make_window(draw_state, view_kind, view_result, access);
        const bool needs_view_ubo = std::any_of(
            layers.begin(),
            layers.end(),
            [](const planned_layer_t& layer) {
                return layer.needs_view_ubo;
            });
        series_view_uniform_std140_t uniform{};
        const series_view_uniform_std140_t* view_uniform = nullptr;
        QRhiBuffer* view_ubo = nullptr;
        if (needs_view_ubo) {
            uniform = make_series_view_uniform(ctx, *draw_state.series, window);
            view_uniform = &uniform;
            view_ubo = ensure_view_ubo(
                draw_state.id,
                view_kind,
                *draw_state.series,
                window);
        }

        for (const auto& planned_layer : layers) {
            const auto& layer = planned_layer.layer;
            if (!layer) {
                continue;
            }
            rhi_state_t::qrhi_layer_program_key_t program_key;
            program_key.series_id = draw_state.id;
            program_key.view_kind = view_kind;
            program_key.layer_id = std::string(layer->id());
            program_key.layer_revision = layer->revision();
            program_key.data_identity = data_source ? data_source->identity() : nullptr;
            program_key.layout_key = access ? access->layout_key : 0;
            program_key.rhi = rhi;

            if (planned_layer.requires_snapshot && !window.snapshot) {
                for (auto& [cached_key, cache_entry] : m_rhi_state->qrhi_layer_cache) {
                    if (cached_key.series_id == program_key.series_id &&
                        cached_key.view_kind == program_key.view_kind &&
                        cached_key.layer_id == program_key.layer_id &&
                        cached_key.layer_revision == program_key.layer_revision &&
                        cached_key.layout_key == program_key.layout_key &&
                        cached_key.rhi == program_key.rhi)
                    {
                        cache_entry.last_frame_used = m_frame_id;
                    }
                }
                continue;
            }
            if (planned_layer.needs_view_ubo && !view_ubo) {
                continue;
            }

            rhi_state_t::qrhi_layer_data_key_t data_key;
            data_key.lod_level = window.lod_level;
            data_key.sample_sequence = window.sample_sequence;
            data_key.t_origin_ns = window.t_origin_ns;
            data_key.first = window.first;
            data_key.count = window.count;
            data_key.hold_last_forward = window.hold_last_forward;
            data_key.interpolation = window.interpolation;

            auto& cache_entry = m_rhi_state->qrhi_layer_cache[program_key];
            bool resources_changed = false;
            if (!cache_entry.state) {
                cache_entry.state = layer->create_state(*rhi);
                resources_changed = true;
            }
            if (!cache_entry.has_data_key || !(cache_entry.data_key == data_key)) {
                resources_changed = true;
            }
            cache_entry.data_key = data_key;
            cache_entry.has_data_key = true;
            cache_entry.last_frame_used = m_frame_id;

            if (!cache_entry.state) {
                continue;
            }

            qrhi_series_prepare_context_t prepare_ctx;
            prepare_ctx.rhi = rhi;
            prepare_ctx.render_target = ctx.render_target;
            prepare_ctx.updates = rhi_updates;
            prepare_ctx.asset_loader = m_asset_loader;
            prepare_ctx.frame = &ctx;
            prepare_ctx.series = draw_state.series.get();
            prepare_ctx.window = window;
            prepare_ctx.view_uniform = view_uniform;
            prepare_ctx.view_ubo = view_ubo;
            prepare_ctx.resources_changed = resources_changed;

            if (cache_entry.state->prepare(prepare_ctx)) {
                m_rhi_state->prepared_layers.push_back({
                    cache_entry.state.get(),
                    draw_state.series.get(),
                    window,
                    view_ubo});
            }
        }
    };

    for (auto& draw_state : draw_states) {
        if (!draw_state.vbo_state) {
            continue;
        }
        prepare_view_layers(
            draw_state,
            Series_view_kind::MAIN,
            draw_state.main_style,
            draw_state.vbo_state->main_view,
            draw_state.main_result,
            draw_state.main_access,
            draw_state.main_source);
    }

    if (preview_visible) {
        for (auto& draw_state : draw_states) {
            if (!draw_state.vbo_state || !draw_state.has_preview) {
                continue;
            }
            prepare_view_layers(
                draw_state,
                Series_view_kind::PREVIEW,
                draw_state.preview_style,
                draw_state.vbo_state->preview_view,
                draw_state.preview_result,
                draw_state.preview_access,
                draw_state.preview_source);
        }
    }

    const auto qrhi_layer_still_configured =
        [&](const rhi_state_t::qrhi_layer_program_key_t& key) {
            const auto series_it = series.find(key.series_id);
            if (series_it == series.end() || !series_it->second || !series_it->second->enabled) {
                return false;
            }

            const series_data_t& series_data = *series_it->second;
            const Data_source* source = nullptr;
            const Data_access_policy* access = nullptr;
            if (key.view_kind == Series_view_kind::MAIN) {
                source = series_data.main_source();
                access = &series_data.main_access();
            }
            else {
                if (!preview_visible) {
                    return false;
                }
                source = series_data.preview_source();
                access = &series_data.preview_access();
            }
            if (!source || !access ||
                key.data_identity != source->identity() ||
                key.layout_key != access->layout_key ||
                key.rhi != rhi)
            {
                return false;
            }

            return std::any_of(
                series_data.qrhi_layers.begin(),
                series_data.qrhi_layers.end(),
                [&](const auto& layer) {
                    return layer &&
                        layer->draws_view(key.view_kind) &&
                        layer->id() == key.layer_id &&
                        layer->revision() == key.layer_revision;
                });
        };

    for (auto it = m_rhi_state->qrhi_layer_cache.begin();
         it != m_rhi_state->qrhi_layer_cache.end(); )
    {
        if (it->second.last_frame_used == m_frame_id ||
            qrhi_layer_still_configured(it->first))
        {
            ++it;
            continue;
        }
        if (it->second.state) {
            it->second.state->cleanup_qrhi_resources(it->first.rhi);
        }
        it = m_rhi_state->qrhi_layer_cache.erase(it);
    }

    for (auto it = m_rhi_state->view_ubos.begin();
         it != m_rhi_state->view_ubos.end(); )
    {
        if (it->second.last_frame_used == m_frame_id) {
            ++it;
            continue;
        }
        it = m_rhi_state->view_ubos.erase(it);
    }

    m_rhi_state->frame_plan_ready = true;
}

void Series_renderer::render(
    const frame_context_t& ctx,
    const std::map<int, std::shared_ptr<const series_data_t>>& series)
{
    if (!ctx.rhi) {
        prepare(ctx, series);
        m_rhi_state->pending_updates = nullptr;
        m_rhi_state->frame_draw_states.clear();
        m_rhi_state->prepared_layers.clear();
        m_rhi_state->frame_plan_ready = false;
        clear_frame_snapshot_caches();
        return;
    }

    if (!ctx.rhi || !ctx.cb || !m_rhi_state->frame_plan_ready) {
        return;
    }

    const auto apply_band_scissor = [&](const sample_window_t& window) {
        const auto to_scissor_y = [&](double top, double height) -> int {
            return static_cast<int>(std::lround(double(ctx.win_h) - (top + height)));
        };
        if (window.view_kind == Series_view_kind::PREVIEW) {
            ctx.cb->setScissor(QRhiScissor(
                0,
                to_scissor_y(window.y_offset_px, window.height_px),
                ctx.win_w,
                static_cast<int>(std::max(1.0f, window.height_px))));
            return;
        }
        ctx.cb->setScissor(QRhiScissor(
            0,
            to_scissor_y(0.0, ctx.layout.usable_height),
            static_cast<int>(std::max(1.0, ctx.layout.usable_width)),
            static_cast<int>(std::max(1.0, ctx.layout.usable_height))));
    };

    for (auto& layer_record : m_rhi_state->prepared_layers) {
        if (!layer_record.state) {
            continue;
        }
        apply_band_scissor(layer_record.window);
        qrhi_series_record_context_t record_ctx;
        record_ctx.cb = ctx.cb;
        record_ctx.render_target = ctx.render_target;
        record_ctx.frame = &ctx;
        record_ctx.series = layer_record.series;
        record_ctx.window = layer_record.window;
        record_ctx.view_ubo = layer_record.view_ubo;
        layer_record.state->record(record_ctx);
    }

    m_rhi_state->pending_updates = nullptr;
    m_rhi_state->frame_draw_states.clear();
    m_rhi_state->prepared_layers.clear();
    m_rhi_state->frame_plan_ready = false;
    clear_frame_snapshot_caches();
}

bool Series_renderer::rhi_prepare_series_primitive(
    const frame_context_t& ctx,
    const series_data_t* series,
    const Data_access_policy* access,
    Display_style primitive_style,
    vbo_view_state_t& view_state,
    const view_render_result_t& view_result,
    float line_width_px,
    float point_diameter_px,
    float area_fill_alpha)
{
    if (!series) {
        return false;
    }
    QRhi* rhi = ctx.rhi;
    QRhiResourceUpdateBatch* updates = ctx.rhi_updates;

    const bool is_dots = (primitive_style == Display_style::DOTS);
    const bool is_area = (primitive_style == Display_style::AREA);
    const std::int32_t count = view_result.count;
    if (count <= 0) {
        return false;
    }
    if (!is_dots && count < 2) {
        return false;
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
        return false;
    }

    if (!view_state.rhi) {
        view_state.rhi = std::make_unique<vbo_view_state_t::rhi_buffers_t>();
    }

    auto ensure_ubo = [&](vbo_view_state_t::rhi_buffers_t::srb_entry_t& entry) -> bool {
        const bool already_sized =
            entry.ubo && entry.ubo_capacity_bytes >= k_series_ubo_bytes;
        if (!detail::ensure_dynamic_ubo(
                rhi, entry.ubo, entry.ubo_capacity_bytes, k_series_ubo_bytes))
        {
            return false;
        }
        if (!already_sized) {
            // ensure_dynamic_ubo replaced the QRhiBuffer; the SRB still holds
            // the old handle. Invalidate so the per-view SRB rebuild below
            // captures the new pointer.
            entry.srb.reset();
            entry.last_ubo = nullptr;
        }
        return true;
    };

    auto& primary_srb_entry = is_dots
        ? view_state.rhi->dots_srb
        : (is_area ? view_state.rhi->area_fill_srb : view_state.rhi->line_srb);
    if (!ensure_ubo(primary_srb_entry)) {
        return false;
    }

    const auto invalidate_uploaded_vbo = [&]() {
        view_state.has_uploaded_vbo = false;
        view_state.uploaded_t_origin_ns = vbo_view_state_t::SENTINEL_NONE;
        view_state.staging.clear();
    };

    const data_snapshot_t& snapshot = view_result.cached_snapshot;
    if (snapshot && access && access->get_timestamp && updates) {
        std::size_t needed_elements = 0;
        std::size_t needed_bytes = 0;
        quint32 upload_bytes = 0;
        if (!detail::checked_size_add(
                snapshot.count,
                view_result.hold_last_forward ? 1u : 0u,
                needed_elements) ||
            !detail::qrhi_byte_size(
                needed_elements, sizeof(gpu_sample_t),
                needed_bytes, upload_bytes))
        {
            invalidate_uploaded_vbo();
            return false;
        }

        auto& staging = view_state.staging;
        staging.resize(needed_elements);

        const auto stage_one_sample = [&](gpu_sample_t& dst, const void* src) {
            const std::int64_t ts_ns = access->get_timestamp(src);
            dst.t_rel = detail::to_view_seconds(ts_ns, view_result.t_origin_ns);
            dst.y = access->get_value ? access->get_value(src) : 0.0f;
            const auto range = access->get_range
                ? access->get_range(src)
                : std::make_pair(dst.y, dst.y);
            dst.y_min = range.first;
            dst.y_max = range.second;
        };

        for (std::size_t i = 0; i < snapshot.count; ++i) {
            const void* src = snapshot.at(i);
            if (src) {
                stage_one_sample(staging[i], src);
            }
            else {
                staging[i] = gpu_sample_t{};
            }
        }

        if (view_result.hold_last_forward && !staging.empty()) {
            const void* source_sample = (snapshot.count > 0)
                ? snapshot.at(snapshot.count - 1) : nullptr;
            if (source_sample && access->clone_with_timestamp) {
                std::vector<unsigned char> user_sample(snapshot.stride);
                access->clone_with_timestamp(
                    user_sample.data(),
                    source_sample,
                    view_result.hold_timestamp_ns);
                stage_one_sample(staging.back(), user_sample.data());
            }
            else {
                staging.back() = gpu_sample_t{};
            }
        }

        std::size_t alloc_bytes = 0;
        quint32 qrhi_alloc_bytes = 0;
        if (!detail::qrhi_grown_capacity_bytes(
                needed_bytes, alloc_bytes, qrhi_alloc_bytes))
        {
            invalidate_uploaded_vbo();
            return false;
        }
        if (!view_state.rhi->vbo || view_state.rhi_vbo_capacity_bytes < alloc_bytes) {
            view_state.rhi->vbo.reset(rhi->newBuffer(
                QRhiBuffer::Static,
                QRhiBuffer::VertexBuffer,
                qrhi_alloc_bytes));
            if (view_state.rhi->vbo && view_state.rhi->vbo->create()) {
                view_state.rhi_vbo_capacity_bytes = alloc_bytes;
            }
            else {
                view_state.rhi->vbo.reset();
                view_state.rhi_vbo_capacity_bytes = 0;
                invalidate_uploaded_vbo();
                return false;
            }
        }
        updates->uploadStaticBuffer(
            view_state.rhi->vbo.get(),
            0,
            upload_bytes,
            staging.data());
        view_state.has_uploaded_vbo = true;
    }

    if (!view_state.rhi->vbo) {
        return false;
    }

    // LINE under RHI feeds the vertex shader through four per-instance
    // vertex attributes (prev, p0, p1, next) sourced from a dedicated
    // per-frame buffer with the active sample window padded by leading and
    // trailing duplicates: [s[first], s[first], s[first+1], ..., s[last],
    // s[last]]. The buffer is bound four times at element offsets 0, 1, 2, 3,
    // so instance i sees (padded[i], padded[i+1],
    // padded[i+2], padded[i+3]) which collapses to the desired clamping at
    // the window edges. SSBOs are unusable here because SPIRV-Cross emits
    // them as RWByteAddressBuffer UAVs and D3D11 SM 5.0 vertex shaders
    // accept zero UAVs; QRhi requires HLSL 5.0 bytecode for D3D11, so any
    // storage-buffer access in the vertex stage fails to compile.
    if (!is_dots && !is_area) {
        const std::size_t window_count =
            line_window_sample_count(count, view_result.interpolation);
        std::size_t padded_count = 0;
        std::size_t needed_bytes = 0;
        quint32 upload_bytes = 0;
        if (!detail::checked_size_add(window_count, 2u, padded_count) ||
            !detail::qrhi_byte_size(
                padded_count, sizeof(gpu_sample_t),
                needed_bytes, upload_bytes))
        {
            return false;
        }
        std::size_t alloc_bytes = 0;
        quint32 qrhi_alloc_bytes = 0;
        if (!detail::qrhi_grown_capacity_bytes(
                needed_bytes, alloc_bytes, qrhi_alloc_bytes))
        {
            return false;
        }
        if (!view_state.rhi->line_window_vbo
            || view_state.rhi_line_window_vbo_capacity_bytes < needed_bytes)
        {
            view_state.rhi->line_window_vbo.reset(rhi->newBuffer(
                QRhiBuffer::Static, QRhiBuffer::VertexBuffer,
                qrhi_alloc_bytes));
            if (view_state.rhi->line_window_vbo
                && view_state.rhi->line_window_vbo->create())
            {
                view_state.rhi_line_window_vbo_capacity_bytes = alloc_bytes;
            }
            else {
                view_state.rhi->line_window_vbo.reset();
                view_state.rhi_line_window_vbo_capacity_bytes = 0;
                return false;
            }
        }
        if (updates) {
            std::vector<gpu_sample_t> padded(padded_count);
            const std::size_t first_idx = static_cast<std::size_t>(view_result.first);
            const std::size_t last_idx = first_idx + static_cast<std::size_t>(count - 1);
            std::size_t write_idx = 1;

            padded[0] = view_state.staging[first_idx];
            if (view_result.interpolation == Series_interpolation::STEP_AFTER) {
                padded[write_idx++] = view_state.staging[first_idx];
                for (std::int32_t i = 1; i < count; ++i) {
                    const gpu_sample_t previous =
                        view_state.staging[first_idx + static_cast<std::size_t>(i - 1)];
                    const gpu_sample_t current =
                        view_state.staging[first_idx + static_cast<std::size_t>(i)];
                    gpu_sample_t held = current;
                    held.y = previous.y;
                    held.y_min = previous.y_min;
                    held.y_max = previous.y_max;
                    padded[write_idx++] = held;
                    padded[write_idx++] = current;
                }
            }
            else {
                for (std::int32_t i = 0; i < count; ++i) {
                    padded[write_idx++] =
                        view_state.staging[first_idx + static_cast<std::size_t>(i)];
                }
            }
            padded[padded_count - 1] = view_state.staging[last_idx];
            updates->uploadStaticBuffer(
                view_state.rhi->line_window_vbo.get(),
                0,
                upload_bytes,
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
            // LINE binds the line_window_vbo four times at element offsets 0,
            // 1, 2, 3. Each binding has stride sizeof(gpu_sample_t)
            // and steps once per instance, so the bindings form a
            // sliding (prev, p0, p1, next) window over the padded
            // sample array. Only the (t_rel, y) pair is consumed by
            // the LINE vertex shader; the range lanes are left unbound.
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

        // RHI series primitives bind only a UBO through the SRB. Sample data
        // rides vertex attributes instead of SSBOs so the D3D11 backend's
        // SM 5.0 vertex shader has zero UAVs.
        detail::alpha_blended_pipeline_desc_t desc;
        desc.vert = cached.vert;
        desc.frag = cached.frag;
        desc.vlayout = vlayout;
        desc.ubo_bytes = k_series_ubo_bytes;
        desc.ubo_stages = QRhiShaderResourceBinding::VertexStage
                        | QRhiShaderResourceBinding::FragmentStage;
        desc.flags = QRhiGraphicsPipeline::UsesScissor;
        cached.pipeline = detail::build_alpha_blended_pipeline(rhi, rt, desc);
        if (!cached.pipeline) {
            return false;
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

        detail::rebuild_single_ubo_srb(
            rhi, entry.srb, current_ubo, k_series_ubo_bytes,
            QRhiShaderResourceBinding::VertexStage
                | QRhiShaderResourceBinding::FragmentStage);
        entry.last_ubo = current_ubo;
    };

    ensure_srb(primary_srb_entry);

    Series_view_std140 view_block{};
    std::memcpy(view_block.pmv, glm::value_ptr(ctx.pmv), sizeof(float) * 16);

    glm::vec4 draw_color = series->color;
    if (is_area) {
        const bool use_dark_default_color =
            ctx.dark_mode && is_default_series_color(draw_color);
        draw_color.w *= area_fill_alpha;
        if (use_dark_default_color) {
            draw_color = k_default_series_color_dark;
            draw_color.w *= area_fill_alpha;
        }
    }
    draw_color.w *= view_result.window_alpha;
    view_block.color[0] = draw_color.r;
    view_block.color[1] = draw_color.g;
    view_block.color[2] = draw_color.b;
    view_block.color[3] = draw_color.a;

    view_block.t_min = detail::to_view_seconds(
        view_result.t_min_ns, view_result.t_origin_ns);
    view_block.t_max = detail::to_view_seconds(
        view_result.t_max_ns, view_result.t_origin_ns);
    view_block.v_min = view_result.v_min;
    view_block.v_max = view_result.v_max;
    view_block.y_offset = view_result.y_offset_px;
    view_block.width    = view_result.width_px;
    view_block.height   = view_result.height_px;
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
        Area_block_std140 block{};
        block.view = view_block;
        block.interpolation =
            view_result.interpolation == Series_interpolation::STEP_AFTER ? 1 : 0;

        if (updates) {
            updates->updateDynamicBuffer(
                view_state.rhi->area_fill_srb.ubo.get(), 0,
                sizeof(block), &block);
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
    return true;
}

void Series_renderer::rhi_record_series_primitive(
    const frame_context_t& ctx,
    Display_style primitive_style,
    vbo_view_state_t& view_state,
    const view_render_result_t& view_result)
{
    QRhiCommandBuffer* cb = ctx.cb;
    if (!cb) {
        return;
    }

    const bool is_dots = (primitive_style == Display_style::DOTS);
    const bool is_area = (primitive_style == Display_style::AREA);
    const std::int32_t count = view_result.count;
    if (count <= 0) {
        return;
    }
    if (!is_dots && count < 2) {
        return;
    }
    if (!view_state.rhi) {
        return;
    }
    if (view_result.first < 0) {
        return;
    }
    const std::size_t first_sample = static_cast<std::size_t>(view_result.first);

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
    cb->setGraphicsPipeline(cached.pipeline.get());

    if (is_dots) {
        cb->setShaderResources(srb_entry.srb.get());
        if (view_state.rhi->vbo) {
            // Encode the per-instance "skip first N samples" by offsetting the
            // vertex-buffer binding rather than passing a non-zero
            // firstInstance. D3D11 does not support firstInstance > 0 without
            // BaseInstance semantics, and emulating it on other backends can
            // be slow. The buffer itself is bound at sample[first], and the
            // shader reads samples relative to that origin.
            quint32 vbo_offset = 0;
            if (!detail::qrhi_buffer_offset(
                    first_sample, sizeof(gpu_sample_t), vbo_offset))
            {
                return;
            }
            QRhiCommandBuffer::VertexInput input{
                view_state.rhi->vbo.get(), vbo_offset};
            cb->setVertexInput(0, 1, &input);
        }
        quint32 instance_count = 0;
        if (!detail::to_qrhi_count(static_cast<std::size_t>(count), instance_count)) {
            return;
        }
        cb->draw(4, instance_count);
    }
    else
    if (is_area) {
        quint32 instance_count = 0;
        if (!detail::to_qrhi_count(
                static_cast<std::size_t>(count - 1), instance_count))
        {
            return;
        }
        if (instance_count > 0 && view_state.rhi->vbo) {
            quint32 vbo_offset = 0;
            quint32 next_vbo_offset = 0;
            std::size_t next_sample = 0;
            if (!detail::qrhi_buffer_offset(
                    first_sample, sizeof(gpu_sample_t), vbo_offset) ||
                !detail::checked_size_add(first_sample, 1u, next_sample) ||
                !detail::qrhi_buffer_offset(
                    next_sample, sizeof(gpu_sample_t), next_vbo_offset))
            {
                return;
            }
            QRhiBuffer* const vbo = view_state.rhi->vbo.get();
            const QRhiCommandBuffer::VertexInput inputs[2] = {
                { vbo, vbo_offset },
                { vbo, next_vbo_offset }
            };
            cb->setVertexInput(0, 2, inputs);

            cb->setShaderResources(srb_entry.srb.get());
            cb->draw(6, instance_count);
        }
    }
    else {
        cb->setShaderResources(srb_entry.srb.get());
        // LINE: triangle-strip quads per segment, one instance per segment.
        // The four vertex inputs all reference the line_window_vbo at
        // increasing element offsets so each instance reads a sliding
        // (prev, p0, p1, next) window across the padded sample array.
        const std::size_t window_count =
            line_window_sample_count(count, view_result.interpolation);
        quint32 instance_count = 0;
        if (window_count > 1 &&
            !detail::to_qrhi_count(window_count - 1u, instance_count))
        {
            return;
        }
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

} // namespace vnm::plot
