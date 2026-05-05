#version 440
#extension GL_GOOGLE_include_directive : require

// Mirror of plot_line.vert with an additional per-sample signal channel
// pulled from binding 3. The host sets has_signal to indicate whether a
// real signal buffer is bound; when false the shader returns 0.0 without
// touching the signal SSBO, so unbound-SSBO undefined behaviour cannot
// be triggered.

#include "uniform_blocks.glsl"

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

// UBO layout must be byte-identical to plot_colormap_line.frag's Block;
// color_multiplier is consumed only by the fragment stage but lives here
// so std140 offsets line up across stages bound to the same descriptor.
layout(std140, binding = 0) uniform Block
{
    Series_view_t view;
    vec4  color_multiplier;
    float line_px;
    int   snap_to_pixels;
    int   has_signal;
} u;

layout(std430, binding = 1) readonly buffer Sample_buffer
{
    gpu_sample_t samples[];
} u_samples;

layout(std430, binding = 2) readonly buffer Adjacency_index_buffer
{
    uint indices[];
} u_adjacency;

layout(std430, binding = 3) readonly buffer Signal_buffer
{
    float values[];
} u_signal;

layout(location = 0) flat out vec2  fs_p_prev;
layout(location = 1) flat out vec2  fs_p0;
layout(location = 2) flat out vec2  fs_p1;
layout(location = 3) flat out vec2  fs_p_next;
layout(location = 4) flat out float fs_signal_0;
layout(location = 5) flat out float fs_signal_1;

float sample_signal(uint idx)
{
    if (u.has_signal == 0) {
        return 0.0;
    }
    return u_signal.values[idx];
}

vec2 sample_to_pos(uint idx)
{
    float rt = max(u.view.t_max - u.view.t_min, 1e-30);
    float rv = max(u.view.v_max - u.view.v_min, 1e-30);

    float x = u.view.width  *       (u_samples.samples[idx].t_rel - u.view.t_min) / rt;
    float y = u.view.height * (1.0 - (u_samples.samples[idx].y     - u.view.v_min) / rv) + u.view.y_offset;

    if (u.snap_to_pixels != 0) {
        x = floor(x) + 0.5;
        y = floor(y) + 0.5;
    }
    return vec2(x, y);
}

void main()
{
    uint seg    = uint(gl_InstanceIndex);
    uint i_prev = u_adjacency.indices[seg + 0u];
    uint i_p0   = u_adjacency.indices[seg + 1u];
    uint i_p1   = u_adjacency.indices[seg + 2u];
    uint i_next = u_adjacency.indices[seg + 3u];

    vec2 p_prev = sample_to_pos(i_prev);
    vec2 p0     = sample_to_pos(i_p0);
    vec2 p1     = sample_to_pos(i_p1);
    vec2 p_next = sample_to_pos(i_next);

    vec2  seg_v   = p1 - p0;
    float seg_len = length(seg_v);

    vec2 dir;
    vec2 n;
    if (seg_len <= 1e-6) {
        dir = vec2(1.0, 0.0);
        n   = vec2(0.0, 0.0);
    }
    else {
        dir = seg_v / seg_len;
        n   = vec2(-dir.y, dir.x);
    }
    float half_px = max(u.line_px * 0.5, 0.5);

    vec2 p0_ext = p0 - dir * half_px;
    vec2 p1_ext = p1 + dir * half_px;

    vec2  base   = (gl_VertexIndex < 2) ? p0_ext : p1_ext;
    float n_sign = (gl_VertexIndex & 1) == 0 ? 1.0 : -1.0;
    vec2  pos    = base + n * (half_px * n_sign);

    fs_p_prev   = p_prev;
    fs_p0       = p0;
    fs_p1       = p1;
    fs_p_next   = p_next;
    fs_signal_0 = sample_signal(i_p0);
    fs_signal_1 = sample_signal(i_p1);
    gl_Position = u.view.pmv * vec4(pos, 0.0, 1.0);
}
