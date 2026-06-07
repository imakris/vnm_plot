#pragma once

// VNM Plot Library - Core Series Window Planning Types
// QRhi-independent view/window metadata shared by planning and layer APIs.

#include <vnm_plot/core/types.h>

#include <cstddef>
#include <cstdint>

namespace vnm::plot {

enum class Series_view_kind
{
    MAIN,
    PREVIEW
};

struct sample_window_t
{
    Series_view_kind view_kind = Series_view_kind::MAIN;

    // Consumers receive a valid frame-scoped snapshot for drawable sample
    // windows. If the renderer cannot acquire one, it skips drawable windows
    // instead of passing empty data as drawable.
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

} // namespace vnm::plot
