#pragma once

// VNM Plot Library - Core Series Renderer
// QRhi series data rendering with LOD support.

#include "series_window.h"
#include "types.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class QRhi;
class QRhiBuffer;
class QRhiCommandBuffer;
class QRhiGraphicsPipeline;
class QRhiResourceUpdateBatch;
class QRhiShaderResourceBindings;

namespace vnm::plot {

class Asset_loader;
class Profiler;

namespace detail {
struct series_window_planner_state_t;
struct Series_window_snapshot_cache;
} // namespace detail

// -----------------------------------------------------------------------------
// Series Renderer
// -----------------------------------------------------------------------------
// Renders data series using QRhi. Manages buffers and pipelines internally.
class Series_renderer
{
public:
    Series_renderer();
    ~Series_renderer();

    Series_renderer(const Series_renderer&) = delete;
    Series_renderer& operator=(const Series_renderer&) = delete;

    // Keep parity with the rest of the renderer initialization path. QSB
    // shaders are loaded lazily from Qt resources.
    void initialize(Asset_loader& asset_loader);

    void cleanup_resources();

    // Two-phase rendering. Under RHI, the host opens a resource-update batch,
    // calls prepare() to fill it with sample/UBO/per-frame uploads, calls
    // beginPass(rt, clear, depth, batch) to atomically submit those uploads
    // before any draw, and then calls render() inside the open pass to record
    // the draw commands. The split is forced by D3D11: cb->resourceUpdate is
    // only legal outside an open pass, and beginPass consumes its 4th
    // argument before the renderer's own draws would otherwise have written
    // to it.
    void prepare(const frame_context_t& ctx,
                 const std::map<int, std::shared_ptr<const series_data_t>>& series);

    // Render all series in the frame context. Records draws only when
    // ctx.rhi is non-null; uploads must already have been submitted via
    // prepare() + beginPass(batch).
    void render(const frame_context_t& ctx,
                const std::map<int, std::shared_ptr<const series_data_t>>& series);

private:
    struct gpu_sample_t;

    struct vbo_view_state_t
    {
        bool has_uploaded_vbo = false;
        std::unique_ptr<detail::series_window_planner_state_t> planner;

        // Renderer-owned scratch buffer for VBO uploads. Holds the planned
        // visible gpu_sample_t values rebased against the active origin.
        // Reused across uploads to avoid reallocation.
        std::vector<gpu_sample_t> staging;
        std::size_t last_staged_sample_count = 0;
        std::size_t last_sample_upload_bytes = 0;
        std::size_t last_sample_upload_count = 0;
        std::size_t last_primitive_prepare_count = 0;
        std::size_t last_line_window_sample_count = 0;
        std::int64_t last_prepared_t_max_ns = 0;
        std::size_t last_vbo_generation = 0;

        // Per-view RHI resources. Defined out-of-line in series_renderer.cpp
        // where QRhiBuffer is complete; the public header only sees the
        // forward declaration. unique_ptr-of-incomplete-type forces every
        // special member to be defined out-of-line, hence the explicit
        // declarations below.
        struct rhi_buffers_t;
        std::unique_ptr<rhi_buffers_t> rhi;

        std::size_t  rhi_vbo_capacity_bytes              = 0;
        std::size_t  rhi_line_window_vbo_capacity_bytes  = 0;

        vbo_view_state_t();
        ~vbo_view_state_t();
        vbo_view_state_t(const vbo_view_state_t&) = delete;
        vbo_view_state_t& operator=(const vbo_view_state_t&) = delete;
        vbo_view_state_t(vbo_view_state_t&&) noexcept;
        vbo_view_state_t& operator=(vbo_view_state_t&&) noexcept;

        void reset();
    };

    struct vbo_state_t
    {
        vbo_view_state_t main_view;
        vbo_view_state_t preview_view;
        std::unique_ptr<detail::Series_window_snapshot_cache> snapshot_cache;

        vbo_state_t();
        ~vbo_state_t();
        vbo_state_t(const vbo_state_t&) = delete;
        vbo_state_t& operator=(const vbo_state_t&) = delete;
        vbo_state_t(vbo_state_t&&) noexcept;
        vbo_state_t& operator=(vbo_state_t&&) noexcept;
    };

    struct view_render_result_t
    {
        std::size_t source_first = 0;
        std::size_t source_count = 0;
        std::size_t synthetic_hold_count = 0;
        std::size_t gpu_count = 0;
        data_snapshot_t cached_snapshot;
        std::int64_t t_min_ns = 0;
        std::int64_t t_max_ns = 0;
        std::int64_t t_origin_ns = 0;
        bool hold_last_forward = false;
        std::int64_t hold_timestamp_ns = 0;
        float v_min = 0.0f;
        float v_max = 1.0f;
        float width_px = 0.0f;
        float height_px = 0.0f;
        float y_offset_px = 0.0f;
        float window_alpha = 1.0f;
        Series_interpolation interpolation = Series_interpolation::LINEAR;
    };

    // Per-(series, view) draw plan computed in prepare() and consumed in
    // render(). Pointer fields stay valid because the host passes the same
    // series_map snapshot to both prepare() and render(), and the renderer's
    // own state (vbo_state) is owned by m_vbo_states.
    struct series_draw_state_t
    {
        int id = 0;
        std::shared_ptr<const series_data_t> series;
        vbo_state_t* vbo_state = nullptr;
        Series_view_plan main_plan;
        Series_view_plan preview_plan;
        bool has_preview = false;
    };

    Asset_loader* m_asset_loader = nullptr;
    std::unordered_map<int, vbo_state_t> m_vbo_states;
    // Consolidated once-per-series error log deduplication.
    // Key encodes (series_id, error_category) as uint64_t.
    std::unordered_set<uint64_t> m_logged_errors;
    // Private test instrumentation for the QRhi prepare/render split.
    std::vector<int> m_last_recorded_draw_z_orders;
    std::vector<Display_style> m_last_recorded_draw_styles;
    std::size_t m_last_qrhi_layer_cache_size = 0;

    // The full implementation sits in series_renderer.cpp where the QRhi
    // types are complete.
    struct rhi_state_t;
    std::unique_ptr<rhi_state_t> m_rhi_state;

    uint64_t m_frame_id = 0;  // Monotonic frame counter for snapshot caching

    void clear_frame_snapshot_caches();

    // rhi_prepare_series_view_samples: writes the compact sample VBO for one
    //   planned series/view window. Built-in AREA/LINE/DOTS primitives share
    //   this upload.
    //
    // rhi_prepare_series_primitive: writes to ctx.rhi_updates only. Builds the
    //   per-primitive UBO(s) and (LINE-only) the per-frame line_window_vbo, and
    //   ensures the cached pipeline / SRB are valid. No cb->* draw calls. Must
    //   run before the host calls beginPass(batch) so the upload is submitted
    //   alongside the rest of ctx.rhi_updates.
    //
    // rhi_record_series_primitive: emits cb->setGraphicsPipeline /
    //   setShaderResources / setVertexInput / draw only. Scissor is owned by
    //   the outer layer replay loop. No buffer writes; safe to call inside the
    //   open render pass.
    bool rhi_prepare_series_view_samples(
        const frame_context_t& ctx,
        const Data_access_policy* access,
        vbo_view_state_t& view_state,
        const view_render_result_t& view_result);
    bool rhi_prepare_series_primitive(
        const frame_context_t& ctx,
        const series_data_t* series,
        Display_style primitive_style,
        vbo_view_state_t& view_state,
        const view_render_result_t& view_result,
        float line_width_px,
        float point_diameter_px,
        float area_fill_alpha);
    void rhi_record_series_primitive(
        const frame_context_t& ctx,
        Display_style primitive_style,
        vbo_view_state_t& view_state,
        const view_render_result_t& view_result);
};

} // namespace vnm::plot
