#pragma once

// VNM Plot Library - QRhi Series Layer API
// Extension point for caller-owned QRhi series rendering.

#include <vnm_plot/core/types.h>

#include <cstdint>
#include <memory>
#include <string_view>

class QRhi;
class QRhiBuffer;
class QRhiCommandBuffer;
class QRhiRenderTarget;
class QRhiResourceUpdateBatch;

namespace vnm::plot {

class Asset_loader;

enum class Series_view_kind
{
    MAIN,
    PREVIEW
};

struct sample_window_t
{
    Series_view_kind view_kind = Series_view_kind::MAIN;

    // Caller-owned QRhi layers receive a valid frame-scoped snapshot for
    // drawable sample windows. If the renderer cannot acquire one, it skips
    // those layers instead of passing a drawable window with empty data.
    data_snapshot_t snapshot;
    const Data_access_policy* access = nullptr;

    std::int32_t first = 0;
    std::int32_t count = 0;
    std::size_t lod_level = 0;
    double pixels_per_sample = 0.0;
    std::uint64_t sample_sequence = 0;
    Series_interpolation interpolation = Series_interpolation::LINEAR;

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
};

struct series_view_uniform_std140_t
{
    float pmv[16];
    float color[4];
    float t_min;
    float t_max;
    float v_min;
    float v_max;
    float width;
    float height;
    float y_offset;
    float win_h;
    std::int32_t framebuffer_y_up;
    float pad0[3];
};

struct qrhi_series_prepare_context_t
{
    QRhi* rhi = nullptr;
    QRhiRenderTarget* render_target = nullptr;
    QRhiResourceUpdateBatch* updates = nullptr;
    Asset_loader* asset_loader = nullptr;

    const frame_context_t* frame = nullptr;
    const series_data_t* series = nullptr;
    sample_window_t window;

    const series_view_uniform_std140_t* view_uniform = nullptr;
    QRhiBuffer* view_ubo = nullptr;

    bool resources_changed = false;
};

struct qrhi_series_record_context_t
{
    QRhiCommandBuffer* cb = nullptr;
    QRhiRenderTarget* render_target = nullptr;

    const frame_context_t* frame = nullptr;
    const series_data_t* series = nullptr;
    sample_window_t window;

    QRhiBuffer* view_ubo = nullptr;
};

class Qrhi_series_layer_state
{
public:
    virtual ~Qrhi_series_layer_state() = default;
    virtual void cleanup_qrhi_resources(QRhi* rhi) { (void)rhi; }
    virtual bool prepare(const qrhi_series_prepare_context_t& ctx) = 0;
    virtual void record(const qrhi_series_record_context_t& ctx) = 0;
};

class Qrhi_series_layer
{
public:
    virtual ~Qrhi_series_layer() = default;

    virtual std::string_view id() const = 0;
    virtual std::uint64_t revision() const = 0;
    virtual int z_order() const = 0;
    virtual bool draws_view(Series_view_kind view_kind) const = 0;

    virtual std::unique_ptr<Qrhi_series_layer_state> create_state(QRhi& rhi) const = 0;
};

series_view_uniform_std140_t make_series_view_uniform(
    const frame_context_t& frame,
    const series_data_t& series,
    const sample_window_t& window);

} // namespace vnm::plot
