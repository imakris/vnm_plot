#pragma once

// VNM Plot Library - GPU Sample Layout
// Fixed fp32 layout that the renderer uploads to GL buffers and that the
// series shaders consume. Decoupled from user sample types so renderer and
// shaders agree on a single 16-byte structure regardless of the source
// sample's layout.

#include <cstdint>
#include <type_traits>

namespace vnm::plot {

// 32-byte fp32 sample shared between the renderer's upload path and the
// series shaders. Time is stored as seconds relative to a per-view origin
// chosen so the rebased value stays inside fp32's usable precision range.
struct gpu_sample_t
{
    float t_rel;
    float y;
    float y_min;
    float y_max;
    float aux_metric;
    float _pad0;
    float _pad1;
    float _pad2;
};

static_assert(sizeof(gpu_sample_t) == 32,
    "gpu_sample_t must be exactly 32 bytes for the fixed VBO layout");
static_assert(alignof(gpu_sample_t) == 4,
    "gpu_sample_t must be 4-byte aligned to match the GL fp32 attribute layout");
static_assert(std::is_standard_layout_v<gpu_sample_t>,
    "gpu_sample_t must be standard-layout for direct GL upload");
static_assert(std::is_trivially_copyable_v<gpu_sample_t>,
    "gpu_sample_t must be trivially copyable for direct GL upload");

} // namespace vnm::plot
