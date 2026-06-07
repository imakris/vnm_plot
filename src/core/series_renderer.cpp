#include <vnm_plot/core/series_renderer.h>
#include <vnm_plot/core/algo.h>
#include <vnm_plot/core/asset_loader.h>
#include <vnm_plot/core/constants.h>
#include <vnm_plot/core/plot_config.h>
#include <vnm_plot/core/series_window.h>
#include <vnm_plot/core/time_units.h>
#include <vnm_plot/qt/qrhi_series_layer.h>
#include "rhi_helpers.h"
#include "series_window_planner.h"

#include <glm/gtc/type_ptr.hpp>
#include <rhi/qrhi.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_set>

namespace vnm::plot {
using detail::choose_origin_ns;
using detail::compute_lod_scales;
using detail::k_scissor_pad_px;
using detail::positive_span_ns_for_signed_api;

namespace {

using detail::load_qsb;

constexpr glm::vec4 k_default_series_color(0.16f, 0.45f, 0.64f, 1.0f);
constexpr glm::vec4 k_default_series_color_dark(0.30f, 0.63f, 0.88f, 1.0f);
constexpr float k_default_color_epsilon = 0.01f;

std::vector<std::size_t> source_lod_scales(const Data_source& source)
{
    const std::size_t level_count = source.lod_levels();
    std::vector<std::size_t> scales = source.lod_scales();
    if (scales.size() != level_count) {
        scales = compute_lod_scales(source);
    }
    for (std::size_t& scale : scales) {
        scale = std::max<std::size_t>(1, scale);
    }
    return scales;
}

bool is_default_series_color(const glm::vec4& color)
{
    return glm::all(glm::lessThan(
        glm::abs(color - k_default_series_color),
        glm::vec4(k_default_color_epsilon)));
}

bool line_window_sample_count(
    std::size_t           source_count,
    Series_interpolation  interpolation,
    std::size_t&          out_count)
{
    out_count = 0;
    if (source_count == 0) {
        return true;
    }
    if (interpolation == Series_interpolation::STEP_AFTER) {
        std::size_t doubled = 0;
        if (!detail::checked_size_product(source_count, 2u, doubled)) {
            return false;
        }
        out_count = doubled - 1u;
        return true;
    }
    out_count = source_count;
    return true;
}

int builtin_primitive_z_order(Display_style primitive_style)
{
    switch (primitive_style) {
        case Display_style::AREA: return -10;
        case Display_style::DOTS: return  10;
        case Display_style::LINE:
        default:                  return   0;
    }
}

bool is_builtin_primitive_drawable(
    Display_style primitive_style,
    std::size_t gpu_count)
{
    if (gpu_count == 0) {
        return false;
    }
    return primitive_style == Display_style::DOTS || gpu_count >= 2;
}

void hash_combine(std::size_t& seed, std::size_t value) noexcept
{
    seed ^= value + 0x9e3779b9u + (seed << 6) + (seed >> 2);
}

std::size_t hash_access_policy_cache_key(
    const detail::access_policy_cache_key_t& key) noexcept
{
    std::size_t h = std::hash<const Data_access_policy*>{}(key.identity);
    hash_combine(h, std::hash<std::uint64_t>{}(key.layout_key));
    hash_combine(h, std::hash<std::uint64_t>{}(key.revision));
    hash_combine(h, std::hash<int>{}(static_cast<int>(key.dispatch_kind)));
    hash_combine(h, std::hash<bool>{}(key.has_timestamp));
    hash_combine(h, std::hash<bool>{}(key.has_value));
    hash_combine(h, std::hash<bool>{}(key.has_range));
    return h;
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

Series_renderer::vbo_view_state_t::vbo_view_state_t()
:
    planner(std::make_unique<detail::series_window_planner_state_t>())
{}
Series_renderer::vbo_view_state_t::~vbo_view_state_t() = default;
Series_renderer::vbo_view_state_t::vbo_view_state_t(vbo_view_state_t&&) noexcept = default;
Series_renderer::vbo_view_state_t&
Series_renderer::vbo_view_state_t::operator=(vbo_view_state_t&&) noexcept = default;

void Series_renderer::vbo_view_state_t::reset()
{
    *this = vbo_view_state_t{};
}

Series_renderer::vbo_state_t::vbo_state_t()
:
    snapshot_cache(std::make_unique<detail::Series_window_snapshot_cache>())
{}
Series_renderer::vbo_state_t::~vbo_state_t() = default;
Series_renderer::vbo_state_t::vbo_state_t(vbo_state_t&&) noexcept = default;
Series_renderer::vbo_state_t&
Series_renderer::vbo_state_t::operator=(vbo_state_t&&) noexcept = default;

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
        detail::access_policy_cache_key_t access_key;
        QRhi* rhi = nullptr;

        bool operator==(const qrhi_layer_program_key_t& o) const noexcept
        {
            return series_id == o.series_id
                && view_kind == o.view_kind
                && layer_id == o.layer_id
                && layer_revision == o.layer_revision
                && data_identity == o.data_identity
                && layout_key == o.layout_key
                && access_key == o.access_key
                && rhi == o.rhi;
        }
    };

    struct qrhi_layer_program_key_hash_t
    {
        std::size_t operator()(const qrhi_layer_program_key_t& k) const noexcept
        {
            std::size_t h = std::hash<int>{}(k.series_id);
            hash_combine(h, std::hash<int>{}(static_cast<int>(k.view_kind)));
            hash_combine(h, std::hash<std::string>{}(k.layer_id));
            hash_combine(h, std::hash<std::uint64_t>{}(k.layer_revision));
            hash_combine(h, std::hash<const void*>{}(k.data_identity));
            hash_combine(h, std::hash<std::uint64_t>{}(k.layout_key));
            hash_combine(h, hash_access_policy_cache_key(k.access_key));
            hash_combine(h, std::hash<QRhi*>{}(k.rhi));
            return h;
        }
    };

    struct qrhi_layer_data_key_t
    {
        std::size_t lod_level = 0;
        std::uint64_t sample_sequence = 0;
        std::int64_t t_origin_ns = 0;
        std::size_t source_first = 0;
        std::size_t source_count = 0;
        std::size_t synthetic_hold_count = 0;
        std::size_t gpu_count = 0;
        bool hold_last_forward = false;
        std::int64_t hold_timestamp_ns = 0;
        Series_interpolation interpolation = Series_interpolation::LINEAR;
        detail::access_policy_cache_key_t access_key;

        bool operator==(const qrhi_layer_data_key_t& o) const noexcept
        {
            return lod_level == o.lod_level
                && sample_sequence == o.sample_sequence
                && t_origin_ns == o.t_origin_ns
                && source_first == o.source_first
                && source_count == o.source_count
                && synthetic_hold_count == o.synthetic_hold_count
                && gpu_count == o.gpu_count
                && hold_last_forward == o.hold_last_forward
                && hold_timestamp_ns == o.hold_timestamp_ns
                && interpolation == o.interpolation
                && access_key == o.access_key;
        }
    };

    struct qrhi_layer_cache_entry_t
    {
        std::unique_ptr<Qrhi_series_layer_state> state;
        qrhi_layer_data_key_t data_key;
        bool has_data_key = false;
        std::uint64_t last_frame_used = 0;
    };

    struct prepared_draw_command_t
    {
        enum class kind_t
        {
            BUILTIN,
            CUSTOM
        };

        kind_t kind = kind_t::CUSTOM;
        int z_order = 0;
        Qrhi_series_layer_state* state = nullptr;
        const series_data_t* series = nullptr;
        sample_window_t window;
        QRhiBuffer* view_ubo = nullptr;
        Display_style primitive_style = Display_style::LINE;
        vbo_view_state_t* view_state = nullptr;
    };

    std::unordered_map<pipeline_key_t, rhi_pipeline_t, pipeline_key_hash_t> pipelines;
    std::unordered_map<view_ubo_key_t, view_ubo_state_t, view_ubo_key_hash_t> view_ubos;
    std::unordered_map<
        qrhi_layer_program_key_t,
        qrhi_layer_cache_entry_t,
        qrhi_layer_program_key_hash_t> qrhi_layer_cache;
    std::vector<prepared_draw_command_t> prepared_draws;
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
// Whole-block layout: Series_view + LINE trailing (line_px, snap_to_pixels).
// Padded out to a 16-byte multiple so the host-side struct mirrors the
// vec4-aligned trailing element rule std140 enforces on the GLSL block.
struct Line_block_std140
{
    series_view_uniform_std140_t view;           // offset 0
    float                       line_px;         // offset 128
    int                         snap_to_pixels;  // offset 132
    float                       _pad0;           // offset 136
    float                       _pad1;           // offset 140
};
static_assert(sizeof(Line_block_std140) == 144, "Line_block_std140 must be a multiple of 16");
static_assert(offsetof(Line_block_std140, line_px)        == 128, "Line_block line_px offset");
static_assert(offsetof(Line_block_std140, snap_to_pixels) == 132, "Line_block snap_to_pixels offset");

// Whole-block layout: Series_view + DOTS trailing (point_diameter_px).
// Padded out to 144 bytes for the same reason as Line_block_std140.
struct Dot_block_std140
{
    series_view_uniform_std140_t view;               // offset 0
    float                       point_diameter_px;   // offset 128
    float                       _pad0;               // offset 132
    float                       _pad1;               // offset 136
    float                       _pad2;               // offset 140
};
static_assert(sizeof(Dot_block_std140) == 144, "Dot_block_std140 must be a multiple of 16");
static_assert(offsetof(Dot_block_std140, point_diameter_px) == 128, "Dot_block point_diameter_px offset");

// Whole-block layout: Series_view + AREA trailing values. Padded out to a
// 16-byte multiple.
struct Area_block_std140
{
    series_view_uniform_std140_t view;           // offset 0
    int                         interpolation;   // offset 128
    int                         _pad0;           // offset 132
    int                         _pad1;           // offset 136
    int                         _pad2;           // offset 140
};
static_assert(sizeof(Area_block_std140) == 144, "Area_block_std140 must be a multiple of 16");
static_assert(offsetof(Area_block_std140, interpolation) == 128, "Area_block interpolation offset");

constexpr std::uint32_t k_series_ubo_bytes = 144;
static_assert(sizeof(Line_block_std140) <= k_series_ubo_bytes, "ubo bytes fit LINE block");
static_assert(sizeof(Dot_block_std140)  <= k_series_ubo_bytes, "ubo bytes fit DOTS block");
static_assert(sizeof(Area_block_std140) == k_series_ubo_bytes, "ubo bytes match AREA block");

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
    m_rhi_state->prepared_draws.clear();
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
    m_rhi_state->prepared_draws.clear();
    m_rhi_state->frame_plan_ready = false;
    m_last_recorded_draw_z_orders.clear();
    m_last_recorded_draw_styles.clear();
    m_last_qrhi_layer_cache_size = 0;
}

void Series_renderer::clear_frame_snapshot_caches()
{
    for (auto& [_, state] : m_vbo_states) {
        if (state.snapshot_cache) {
            *state.snapshot_cache = detail::Series_window_snapshot_cache{};
        }
    }
}

void Series_renderer::prepare(
    const frame_context_t& ctx,
    const std::map<int, std::shared_ptr<const series_data_t>>& series)
{
    m_rhi_state->frame_draw_states.clear();
    m_rhi_state->prepared_draws.clear();
    m_rhi_state->frame_plan_ready = false;
    m_last_recorded_draw_z_orders.clear();
    m_last_recorded_draw_styles.clear();
    m_last_qrhi_layer_cache_size = m_rhi_state->qrhi_layer_cache.size();

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
        m_rhi_state->prepared_draws.clear();
        m_last_qrhi_layer_cache_size = 0;
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
        m_rhi_state->prepared_draws.clear();
        m_last_qrhi_layer_cache_size = 0;
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

        std::vector<std::size_t> main_scales = source_lod_scales(*main_source);
        std::vector<std::size_t> preview_scales;
        if (preview_valid) {
            if (preview_matches_main) {
                preview_scales = main_scales;
            }
            else {
                preview_scales = source_lod_scales(*preview_source);
            }
        }

        const auto plan_series_view =
            [&](Series_view_kind view_kind,
                vbo_view_state_t& view_state,
                Data_source& data_source,
                const Data_access_policy& access,
                const std::vector<std::size_t>& scales,
                std::int64_t t_min_ns,
                std::int64_t t_max_ns,
                std::int64_t t_origin_ns,
                double width_px,
                Display_style style,
                Series_interpolation interpolation,
                detail::Snapshot_requirement snapshot_requirement)
        {
            detail::series_window_plan_request_t request;
            request.series_id = id;
            request.view_kind = view_kind;
            request.planner_state = view_state.planner.get();
            request.snapshot_cache = vbo_state.snapshot_cache.get();
            request.frame_id = m_frame_id;
            request.data_source = &data_source;
            request.access = &access;
            request.scales = &scales;
            request.t_min_ns = t_min_ns;
            request.t_max_ns = t_max_ns;
            request.t_origin_ns = t_origin_ns;
            request.width_px = width_px;
            request.empty_window_behavior = s->empty_window_behavior;
            request.style = style;
            request.interpolation = interpolation;
            request.snapshot_requirement = snapshot_requirement;
            request.has_uploaded_vbo = view_state.has_uploaded_vbo;
            request.profiler = profiler;
            return detail::plan_series_window(request);
        };

        const std::size_t prev_lod_level = vbo_state.main_view.planner
            ? vbo_state.main_view.planner->last_lod_level
            : 0;
        auto main_plan = plan_series_view(
            Series_view_kind::MAIN,
            vbo_state.main_view,
            *main_source,
            main_access,
            main_scales,
            ctx.t0,
            ctx.t1,
            main_origin_ns,
            layout.usable_width,
            main_style,
            main_interpolation,
            has_main_layer
                ? detail::Snapshot_requirement::Frame_snapshot_required
                : detail::Snapshot_requirement::Optional);
        main_plan.v_min = ctx.v0;
        main_plan.v_max = ctx.v1;
        main_plan.height_px = static_cast<float>(layout.usable_height);
        main_plan.y_offset_px = 0.0f;
        main_plan.window_alpha = 1.0f;
        if (ctx.config && ctx.config->log_debug &&
            main_plan.gpu_count > 0 &&
            main_plan.lod_level != prev_lod_level)
        {
            std::string message = "LOD selection: series=" + std::to_string(id)
                + " level=" + std::to_string(main_plan.lod_level)
                + " pps=" + std::to_string(main_plan.pixels_per_sample);
            ctx.config->log_debug(message);
        }

        Series_view_plan preview_plan;
        preview_plan.series_id = id;
        preview_plan.view_kind = Series_view_kind::PREVIEW;
        preview_plan.source = preview_source;
        preview_plan.access = preview_access;
        preview_plan.empty_window_behavior = s->empty_window_behavior;
        preview_plan.style = preview_style;
        preview_plan.interpolation = preview_interpolation;
        if (preview_visible && preview_valid) {
            preview_plan = plan_series_view(
                Series_view_kind::PREVIEW,
                vbo_state.preview_view,
                *preview_source,
                *preview_access,
                preview_scales,
                ctx.t_available_min,
                ctx.t_available_max,
                preview_origin_ns,
                ctx.win_w,
                preview_style,
                preview_interpolation,
                has_preview_layer
                    ? detail::Snapshot_requirement::Frame_snapshot_required
                    : detail::Snapshot_requirement::Optional);
            const double preview_top =
                double(ctx.win_h) - ctx.adjusted_preview_height;
            preview_plan.v_min = ctx.preview_v0;
            preview_plan.v_max = ctx.preview_v1;
            preview_plan.height_px = static_cast<float>(ctx.adjusted_preview_height);
            preview_plan.y_offset_px = static_cast<float>(preview_top);
            preview_plan.window_alpha = static_cast<float>(preview_visibility);
        }

        series_draw_state_t draw_state;
        draw_state.id = id;
        draw_state.series = s;
        draw_state.vbo_state = &vbo_state;
        draw_state.main_plan = std::move(main_plan);
        draw_state.preview_plan = std::move(preview_plan);
        draw_state.has_preview = preview_visible && preview_valid;
        draw_states.push_back(std::move(draw_state));
    }

    if (!rhi || !rhi_updates || !ctx.render_target) {
        m_rhi_state->frame_plan_ready = false;
        return;
    }

    const auto make_window = [](const Series_view_plan& plan) {
        sample_window_t window;
        window.view_kind = plan.view_kind;
        window.snapshot = plan.snapshot.snapshot;
        window.access = plan.access;
        window.source_first = plan.source_first;
        window.source_count = plan.source_count;
        window.synthetic_hold_count = plan.synthetic_hold_count;
        window.gpu_count = plan.gpu_count;
        window.lod_level = plan.lod_level;
        window.pixels_per_sample = plan.pixels_per_sample;
        window.sample_sequence = plan.snapshot.sequence;
        window.interpolation = plan.interpolation;
        window.t_min_ns = plan.t_min_ns;
        window.t_max_ns = plan.t_max_ns;
        window.t_origin_ns = plan.t_origin_ns;
        window.hold_last_forward = plan.hold_last_forward;
        window.hold_timestamp_ns = plan.hold_timestamp_ns;
        window.v_min = plan.v_min;
        window.v_max = plan.v_max;
        window.width_px = plan.width_px;
        window.height_px = plan.height_px;
        window.y_offset_px = plan.y_offset_px;
        window.window_alpha = plan.window_alpha;
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
            const Series_view_plan& plan,
            vbo_view_state_t& view_state)
    {
        view_state.last_sample_upload_count = 0;
        view_state.last_primitive_prepare_count = 0;
        view_state.last_line_window_sample_count = 0;
        view_state.last_sample_access_dispatch_kind =
            detail::access_dispatch_kind_t::NONE;

        if (!draw_state.series || plan.gpu_count == 0) {
            return;
        }

        struct planned_draw_t
        {
            std::shared_ptr<const Qrhi_series_layer> layer;
            bool is_builtin = false;
            Display_style primitive_style = Display_style::NONE;
            int z_order = 0;
        };

        std::vector<planned_draw_t> planned_draws;
        if (!!(plan.style & Display_style::AREA)) {
            planned_draws.push_back({
                nullptr,
                true,
                Display_style::AREA,
                builtin_primitive_z_order(Display_style::AREA)});
        }
        if (!!(plan.style & Display_style::LINE)) {
            planned_draws.push_back({
                nullptr,
                true,
                Display_style::LINE,
                builtin_primitive_z_order(Display_style::LINE)});
        }
        if (!!(plan.style & Display_style::DOTS)) {
            planned_draws.push_back({
                nullptr,
                true,
                Display_style::DOTS,
                builtin_primitive_z_order(Display_style::DOTS)});
        }
        for (const auto& layer : draw_state.series->qrhi_layers) {
            if (layer && layer->draws_view(plan.view_kind)) {
                planned_draws.push_back({
                    layer,
                    false,
                    Display_style::NONE,
                    layer->z_order()});
            }
        }
        std::stable_sort(
            planned_draws.begin(),
            planned_draws.end(),
            [](const auto& a, const auto& b) {
                return a.z_order < b.z_order;
            });
        if (planned_draws.empty()) {
            return;
        }

        sample_window_t window = make_window(plan);
        const bool has_drawable_builtin_layer = std::any_of(
            planned_draws.begin(),
            planned_draws.end(),
            [&](const planned_draw_t& draw) {
                return draw.is_builtin &&
                    is_builtin_primitive_drawable(
                        draw.primitive_style,
                        window.gpu_count);
            });
        bool builtin_samples_ready = true;
        if (has_drawable_builtin_layer) {
            builtin_samples_ready = rhi_prepare_series_view_samples(
                ctx,
                view_state,
                window);
        }
        const bool needs_view_ubo = std::any_of(
            planned_draws.begin(),
            planned_draws.end(),
            [](const planned_draw_t& draw) {
                return !draw.is_builtin;
            });
        series_view_uniform_std140_t uniform{};
        const series_view_uniform_std140_t* view_uniform = nullptr;
        QRhiBuffer* view_ubo = nullptr;
        if (needs_view_ubo) {
            uniform = make_series_view_uniform(ctx, *draw_state.series, window);
            view_uniform = &uniform;
            view_ubo = ensure_view_ubo(
                draw_state.id,
                plan.view_kind,
                *draw_state.series,
                window);
        }

        const float line_width_px = ctx.config
            ? static_cast<float>(ctx.config->line_width_px) : 1.0f;
        const float point_diameter_px = ctx.config
            ? static_cast<float>(ctx.config->point_diameter_px) : 1.0f;
        const float area_fill_alpha = ctx.config
            ? static_cast<float>(ctx.config->area_fill_alpha) : 0.3f;

        for (const auto& planned_draw : planned_draws) {
            if (planned_draw.is_builtin) {
                if (!builtin_samples_ready ||
                    !is_builtin_primitive_drawable(
                        planned_draw.primitive_style,
                        window.gpu_count))
                {
                    continue;
                }

                if (rhi_prepare_series_primitive(
                        ctx,
                        draw_state.series.get(),
                        planned_draw.primitive_style,
                        view_state,
                        window,
                        line_width_px,
                        point_diameter_px,
                        area_fill_alpha))
                {
                    rhi_state_t::prepared_draw_command_t command;
                    command.kind =
                        rhi_state_t::prepared_draw_command_t::kind_t::BUILTIN;
                    command.z_order = planned_draw.z_order;
                    command.series = draw_state.series.get();
                    command.window = window;
                    command.primitive_style = planned_draw.primitive_style;
                    command.view_state = &view_state;
                    m_rhi_state->prepared_draws.push_back(std::move(command));
                }
                continue;
            }

            const auto& layer = planned_draw.layer;
            if (!layer) {
                continue;
            }

            const detail::erased_access_policy_t layer_access_view =
                plan.access
                    ? detail::make_erased_access_policy_view(*plan.access)
                    : detail::erased_access_policy_t{};
            const detail::access_policy_cache_key_t layer_access_key =
                detail::make_access_policy_cache_key(
                    plan.access,
                    layer_access_view);

            rhi_state_t::qrhi_layer_program_key_t program_key;
            program_key.series_id = draw_state.id;
            program_key.view_kind = plan.view_kind;
            program_key.layer_id = std::string(layer->id());
            program_key.layer_revision = layer->revision();
            program_key.data_identity = plan.source ? plan.source->identity() : nullptr;
            program_key.layout_key = plan.access ? plan.access->layout_key : 0;
            program_key.access_key = layer_access_key;
            program_key.rhi = rhi;

            if (!window.snapshot) {
                for (auto& [cached_key, cache_entry] : m_rhi_state->qrhi_layer_cache) {
                    if (cached_key.series_id == program_key.series_id &&
                        cached_key.view_kind == program_key.view_kind &&
                        cached_key.layer_id == program_key.layer_id &&
                        cached_key.layer_revision == program_key.layer_revision &&
                        cached_key.layout_key == program_key.layout_key &&
                        cached_key.access_key == program_key.access_key &&
                        cached_key.rhi == program_key.rhi)
                    {
                        cache_entry.last_frame_used = m_frame_id;
                    }
                }
                continue;
            }
            if (!view_ubo) {
                continue;
            }

            rhi_state_t::qrhi_layer_data_key_t data_key;
            data_key.lod_level = window.lod_level;
            data_key.sample_sequence = window.sample_sequence;
            data_key.t_origin_ns = window.t_origin_ns;
            data_key.source_first = window.source_first;
            data_key.source_count = window.source_count;
            data_key.synthetic_hold_count = window.synthetic_hold_count;
            data_key.gpu_count = window.gpu_count;
            data_key.hold_last_forward = window.hold_last_forward;
            data_key.hold_timestamp_ns = window.hold_timestamp_ns;
            data_key.interpolation = window.interpolation;
            data_key.access_key = layer_access_key;

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
                rhi_state_t::prepared_draw_command_t command;
                command.kind =
                    rhi_state_t::prepared_draw_command_t::kind_t::CUSTOM;
                command.z_order = planned_draw.z_order;
                command.state = cache_entry.state.get();
                command.series = draw_state.series.get();
                command.window = window;
                command.view_ubo = view_ubo;
                m_rhi_state->prepared_draws.push_back(std::move(command));
            }
        }
    };

    for (auto& draw_state : draw_states) {
        if (!draw_state.vbo_state) {
            continue;
        }
        prepare_view_layers(
            draw_state,
            draw_state.main_plan,
            draw_state.vbo_state->main_view);
    }

    if (preview_visible) {
        for (auto& draw_state : draw_states) {
            if (!draw_state.vbo_state || !draw_state.has_preview) {
                continue;
            }
            prepare_view_layers(
                draw_state,
                draw_state.preview_plan,
                draw_state.vbo_state->preview_view);
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
            const detail::erased_access_policy_t access_view =
                detail::make_erased_access_policy_view(*access);
            const detail::access_policy_cache_key_t access_key =
                detail::make_access_policy_cache_key(access, access_view);
            if (key.access_key != access_key) {
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
    m_last_qrhi_layer_cache_size = m_rhi_state->qrhi_layer_cache.size();

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
        m_rhi_state->prepared_draws.clear();
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

    for (auto& command : m_rhi_state->prepared_draws) {
        apply_band_scissor(command.window);
        m_last_recorded_draw_z_orders.push_back(command.z_order);
        m_last_recorded_draw_styles.push_back(
            command.kind ==
                    rhi_state_t::prepared_draw_command_t::kind_t::BUILTIN
                ? command.primitive_style
                : Display_style::NONE);

        if (command.kind ==
            rhi_state_t::prepared_draw_command_t::kind_t::BUILTIN)
        {
            if (command.view_state) {
                rhi_record_series_primitive(
                    ctx,
                    command.primitive_style,
                    *command.view_state,
                    command.window);
            }
            continue;
        }

        if (!command.state) {
            continue;
        }
        qrhi_series_record_context_t record_ctx;
        record_ctx.cb = ctx.cb;
        record_ctx.render_target = ctx.render_target;
        record_ctx.frame = &ctx;
        record_ctx.series = command.series;
        record_ctx.window = command.window;
        record_ctx.view_ubo = command.view_ubo;
        command.state->record(record_ctx);
    }

    m_rhi_state->pending_updates = nullptr;
    m_rhi_state->frame_draw_states.clear();
    m_rhi_state->prepared_draws.clear();
    m_rhi_state->frame_plan_ready = false;
    clear_frame_snapshot_caches();
}

bool Series_renderer::rhi_prepare_series_view_samples(
    const frame_context_t& ctx,
    vbo_view_state_t& view_state,
    const sample_window_t& window)
{
    QRhi* rhi = ctx.rhi;
    QRhiResourceUpdateBatch* updates = ctx.rhi_updates;
    if (!rhi || window.gpu_count == 0) {
        return false;
    }

    if (!view_state.rhi) {
        view_state.rhi = std::make_unique<vbo_view_state_t::rhi_buffers_t>();
    }

    const auto invalidate_uploaded_vbo = [&]() {
        view_state.has_uploaded_vbo = false;
        if (view_state.planner) {
            view_state.planner->uploaded_t_origin_ns =
                detail::series_window_planner_state_t::k_no_timestamp;
        }
        view_state.staging.clear();
        view_state.last_staged_sample_count = 0;
        view_state.last_sample_upload_bytes = 0;
        view_state.last_line_window_sample_count = 0;
        view_state.last_prepared_t_max_ns = 0;
    };

    const data_snapshot_t& snapshot = window.snapshot;
    const detail::erased_access_policy_t access_view = window.access
        ? detail::make_erased_access_policy_view(*window.access)
        : detail::erased_access_policy_t{};
    view_state.last_sample_access_dispatch_kind = access_view.dispatch_kind;
    if (snapshot && access_view.has_timestamp() && updates) {
        std::size_t expected_gpu_count = 0;
        if (window.synthetic_hold_count > 1 ||
            (window.synthetic_hold_count == 1 && window.source_count == 0) ||
            !detail::checked_size_add(
                window.source_count,
                window.synthetic_hold_count,
                expected_gpu_count) ||
            expected_gpu_count != window.gpu_count ||
            window.source_first > snapshot.count ||
            window.source_count > snapshot.count - window.source_first)
        {
            invalidate_uploaded_vbo();
            return false;
        }

        std::size_t needed_elements = 0;
        std::size_t needed_bytes = 0;
        quint32 upload_bytes = 0;
        if (!detail::checked_size_add(window.gpu_count, 0u, needed_elements) ||
            !detail::qrhi_byte_size(
                needed_elements, sizeof(gpu_sample_t),
                needed_bytes, upload_bytes))
        {
            invalidate_uploaded_vbo();
            return false;
        }

        auto& staging = view_state.staging;
        staging.resize(needed_elements);

        const auto stage_one_sample =
            [&](gpu_sample_t& dst, const void* src, std::int64_t ts_ns) {
                dst.t_rel = detail::to_view_seconds(ts_ns, window.t_origin_ns);
                dst.y = access_view.has_value() ? access_view.value(src) : 0.0f;
                const auto range = access_view.has_range()
                    ? access_view.range(src)
                    : std::make_pair(dst.y, dst.y);
                dst.y_min = range.first;
                dst.y_max = range.second;
            };

        for (std::size_t i = 0; i < window.source_count; ++i) {
            const void* src = snapshot.at(window.source_first + i);
            if (src) {
                stage_one_sample(staging[i], src, access_view.timestamp(src));
            }
            else {
                staging[i] = gpu_sample_t{};
            }
        }

        if (window.synthetic_hold_count == 1 && !staging.empty()) {
            const std::size_t source_index =
                window.source_first + window.source_count - 1u;
            const void* source_sample = snapshot.at(source_index);
            if (source_sample) {
                stage_one_sample(
                    staging.back(),
                    source_sample,
                    window.hold_timestamp_ns);
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
        if (!view_state.rhi->vbo ||
            view_state.rhi_vbo_capacity_bytes < needed_bytes)
        {
            view_state.rhi->vbo.reset(rhi->newBuffer(
                QRhiBuffer::Static,
                QRhiBuffer::VertexBuffer,
                qrhi_alloc_bytes));
            if (view_state.rhi->vbo && view_state.rhi->vbo->create()) {
                view_state.rhi_vbo_capacity_bytes = alloc_bytes;
                ++view_state.last_vbo_generation;
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
        view_state.last_staged_sample_count = needed_elements;
        view_state.last_sample_upload_bytes = upload_bytes;
        ++view_state.last_sample_upload_count;
        view_state.has_uploaded_vbo = true;
    }

    if (!view_state.rhi->vbo) {
        return false;
    }
    view_state.last_prepared_t_max_ns = window.t_max_ns;
    return true;
}

bool Series_renderer::rhi_prepare_series_primitive(
    const frame_context_t& ctx,
    const series_data_t* series,
    Display_style primitive_style,
    vbo_view_state_t& view_state,
    const sample_window_t& window,
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
    const std::size_t count = window.gpu_count;
    if (count == 0) {
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
        std::size_t window_count = 0;
        if (!line_window_sample_count(
                count, window.interpolation, window_count))
        {
            return false;
        }
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
            if (view_state.staging.size() < count) {
                return false;
            }
            std::vector<gpu_sample_t> padded(padded_count);
            const std::size_t last_idx = count - 1u;
            std::size_t write_idx = 1;

            padded[0] = view_state.staging[0];
            if (window.interpolation == Series_interpolation::STEP_AFTER) {
                padded[write_idx++] = view_state.staging[0];
                for (std::size_t i = 1; i < count; ++i) {
                    const gpu_sample_t previous =
                        view_state.staging[i - 1u];
                    const gpu_sample_t current =
                        view_state.staging[i];
                    gpu_sample_t held = current;
                    held.y = previous.y;
                    held.y_min = previous.y_min;
                    held.y_max = previous.y_max;
                    padded[write_idx++] = held;
                    padded[write_idx++] = current;
                }
            }
            else {
                for (std::size_t i = 0; i < count; ++i) {
                    padded[write_idx++] = view_state.staging[i];
                }
            }
            padded[padded_count - 1] = view_state.staging[last_idx];
            updates->uploadStaticBuffer(
                view_state.rhi->line_window_vbo.get(),
                0,
                upload_bytes,
                padded.data());
            view_state.last_line_window_sample_count = window_count;
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

    series_view_uniform_std140_t view_block{};
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
    draw_color.w *= window.window_alpha;
    view_block.color[0] = draw_color.r;
    view_block.color[1] = draw_color.g;
    view_block.color[2] = draw_color.b;
    view_block.color[3] = draw_color.a;

    view_block.t_min = detail::to_view_seconds(
        window.t_min_ns, window.t_origin_ns);
    view_block.t_max = detail::to_view_seconds(
        window.t_max_ns, window.t_origin_ns);
    view_block.v_min = window.v_min;
    view_block.v_max = window.v_max;
    view_block.y_offset = window.y_offset_px;
    view_block.width    = window.width_px;
    view_block.height   = window.height_px;
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
            window.interpolation == Series_interpolation::STEP_AFTER ? 1 : 0;

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
    ++view_state.last_primitive_prepare_count;
    return true;
}

void Series_renderer::rhi_record_series_primitive(
    const frame_context_t& ctx,
    Display_style primitive_style,
    vbo_view_state_t& view_state,
    const sample_window_t& window)
{
    QRhiCommandBuffer* cb = ctx.cb;
    if (!cb) {
        return;
    }

    const bool is_dots = (primitive_style == Display_style::DOTS);
    const bool is_area = (primitive_style == Display_style::AREA);
    const std::size_t count = window.gpu_count;
    if (count == 0) {
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
    cb->setGraphicsPipeline(cached.pipeline.get());

    if (is_dots) {
        cb->setShaderResources(srb_entry.srb.get());
        if (view_state.rhi->vbo) {
            QRhiCommandBuffer::VertexInput input{
                view_state.rhi->vbo.get(), 0u};
            cb->setVertexInput(0, 1, &input);
        }
        quint32 instance_count = 0;
        if (!detail::to_qrhi_count(count, instance_count)) {
            return;
        }
        cb->draw(4, instance_count);
    }
    else
    if (is_area) {
        quint32 instance_count = 0;
        if (!detail::to_qrhi_count(count - 1u, instance_count))
        {
            return;
        }
        if (instance_count > 0 && view_state.rhi->vbo) {
            const quint32 next_vbo_offset =
                static_cast<quint32>(sizeof(gpu_sample_t));
            QRhiBuffer* const vbo = view_state.rhi->vbo.get();
            const QRhiCommandBuffer::VertexInput inputs[2] = {
                { vbo, 0u },
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
        std::size_t window_count = 0;
        if (!line_window_sample_count(
                count, window.interpolation, window_count))
        {
            return;
        }
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
