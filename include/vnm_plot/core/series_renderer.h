#pragma once

// VNM Plot Library - Core Series Renderer
// QRhi series data rendering with LOD support.

#include "series_window.h"
#include "types.h"

#include <cstddef>
#include <cstdint>
#include <limits>
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
    class Builtin_series_layer;
    class Builtin_series_layer_state;

    struct vbo_view_state_t
    {
        bool has_uploaded_vbo = false;
        std::size_t last_snapshot_elements = 0;
        uint64_t last_sequence = 0;
        const void* cached_data_identity = nullptr;
        uint64_t last_timestamp_order_sequence = 0;
        const void* last_timestamp_order_identity = nullptr;
        bool last_timestamps_monotonic = true;

        std::int32_t last_first = 0;
        std::int32_t last_count = 0;
        std::size_t last_lod_level = 0;
        // Timestamps are int64_t nanoseconds; sentinel SENTINEL_NONE means "no
        // valid value yet" so the first frame always invalidates cached state.
        static constexpr std::int64_t SENTINEL_NONE = std::numeric_limits<std::int64_t>::min();
        std::int64_t last_t_min = SENTINEL_NONE;
        std::int64_t last_t_max = SENTINEL_NONE;
        double last_width_px = std::numeric_limits<double>::quiet_NaN();
        Empty_window_behavior last_empty_window_behavior = Empty_window_behavior::DRAW_NOTHING;
        double last_applied_pps = 0.0;
        bool last_hold_last_forward = false;
        Series_interpolation last_interpolation = Series_interpolation::LINEAR;
        // Origin (ns) that produced the bytes currently in the VBO. Used to
        // invalidate the upload when the view's chosen origin moves to a new
        // snap bucket. SENTINEL_NONE forces the first frame to upload.
        std::int64_t uploaded_t_origin_ns = SENTINEL_NONE;
        // Renderer-owned scratch buffer for VBO uploads. Holds gpu_sample_t
        // values rebased against the active origin: the full snapshot followed
        // by an optional hold-last-forward synthetic sample. Reused across
        // uploads to avoid reallocation.
        std::vector<gpu_sample_t> staging;

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
        // Frame-scoped snapshot cache: shared between main_view and preview_view
        // to avoid redundant try_snapshot() calls within the same frame.
        uint64_t cached_snapshot_frame_id = 0;
        std::size_t cached_snapshot_level = SIZE_MAX;
        const Data_source* cached_snapshot_source = nullptr;
        data_snapshot_t cached_snapshot;
        std::shared_ptr<void> cached_snapshot_hold;
    };

    struct view_render_result_t
    {
        bool can_draw = false;
        std::int32_t first = 0;
        std::int32_t count = 0;
        std::size_t applied_level = 0;
        double applied_pps = 0.0;
        data_snapshot_t cached_snapshot;
        std::shared_ptr<void> cached_snapshot_hold;   // Keep snapshot alive
        std::uint64_t sample_sequence = 0;
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

    enum class Snapshot_requirement
    {
        Optional,
        Frame_snapshot_required
    };

    // Per-(series, view) draw plan computed in prepare() and consumed in
    // render(). Holds the raw policy/source pointers and the LOD-resolved
    // view results so the record-draws phase can replay decisions without
    // re-running process_view. Pointer fields stay valid because the host
    // passes the same series_map snapshot to both prepare() and render(),
    // and the renderer's own state (vbo_state) is owned by m_vbo_states.
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

    // The full implementation sits in series_renderer.cpp where the QRhi
    // types are complete.
    struct rhi_state_t;
    std::unique_ptr<rhi_state_t> m_rhi_state;

    uint64_t m_frame_id = 0;  // Monotonic frame counter for snapshot caching

    void clear_frame_snapshot_caches();

    view_render_result_t plan_view(
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
        vnm::plot::Profiler* profiler);

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
    bool rhi_prepare_series_primitive(
        const frame_context_t& ctx,
        const series_data_t* series,
        const Data_access_policy* access,
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
