#version 440
#extension GL_GOOGLE_include_directive : require

// Per-instance segment expansion of the line strip. The four triangle-strip
// corners (gl_VertexIndex 0..3) form a thickened screen-space quad spanning
// samples p0 -> p1, with prev and next passed flat to the fragment shader
// for rounded segment joins.

#include "uniform_blocks.glsl"

struct gpu_sample_t
{
    float t_rel;
    float y;
    float y_min;
    float y_max;
};

layout(std140, binding = 0) uniform Block
{
    Series_view_t view;
    float line_px;
    int   snap_to_pixels;
} u;

layout(std430, binding = 1) readonly buffer Sample_buffer
{
    gpu_sample_t samples[];
} u_samples;

layout(std430, binding = 2) readonly buffer Adjacency_index_buffer
{
    uint indices[];
} u_adjacency;

layout(location = 0) flat out vec2 fs_p_prev;
layout(location = 1) flat out vec2 fs_p0;
layout(location = 2) flat out vec2 fs_p1;
layout(location = 3) flat out vec2 fs_p_next;

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
    // Boundary samples are duplicated on the host so prev/next clamp at the
    // strip endpoints without a shader-side branch.
    uint seg = uint(gl_InstanceIndex);
    vec2 p_prev = sample_to_pos(u_adjacency.indices[seg + 0u]);
    vec2 p0     = sample_to_pos(u_adjacency.indices[seg + 1u]);
    vec2 p1     = sample_to_pos(u_adjacency.indices[seg + 2u]);
    vec2 p_next = sample_to_pos(u_adjacency.indices[seg + 3u]);

    vec2  seg_v   = p1 - p0;
    float seg_len = length(seg_v);
    float half_px = max(u.line_px * 0.5, 0.5);

    vec2 pos;
    if (seg_len <= 1e-6) {
        // Degenerate segment collapses to a point; the rasterizer culls it.
        pos = p0;
    }
    else {
        vec2 dir = seg_v / seg_len;
        vec2 n   = vec2(-dir.y, dir.x);

        vec2 p0_ext = p0 - dir * half_px;
        vec2 p1_ext = p1 + dir * half_px;

        // Triangle-strip vertex order (gl_VertexIndex 0..3): p0+n, p0-n, p1+n, p1-n.
        vec2  base   = (gl_VertexIndex < 2) ? p0_ext : p1_ext;
        float n_sign = (gl_VertexIndex & 1) == 0 ? 1.0 : -1.0;
        pos = base + n * (half_px * n_sign);
    }

    fs_p_prev   = p_prev;
    fs_p0       = p0;
    fs_p1       = p1;
    fs_p_next   = p_next;
    gl_Position = u.view.pmv * vec4(pos, 0.0, 1.0);
}
