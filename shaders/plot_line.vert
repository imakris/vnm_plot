#version 430

// Instance = one segment of the line strip.
// Per instance, the four triangle-strip corners (gl_VertexID 0..3) form a
// thickened screen-space quad spanning samples p0 -> p1, with prev and next
// passed flat to the fragment shader for rounded segment joins.
//
// Sample data is pulled from an SSBO with a fixed fp32 layout that mirrors
// the renderer's gpu_sample_t (t_rel in seconds-from-origin, y, y_min, y_max).
// The neighbour-index buffer mirrors the adjacency EBO produced on the host:
// it contains [first, first, first+1, ..., first+count-1, first+count-1] so
// boundary segments naturally clamp prev/next to the endpoint sample.

struct GpuSample
{
    float t_rel;
    float y;
    float y_min;
    float y_max;
};

layout(std430, binding = 0) readonly buffer Sample_buffer
{
    GpuSample samples[];
} u_samples;

layout(std430, binding = 1) readonly buffer Adjacency_index_buffer
{
    uint indices[];
} u_adjacency;

layout(location =  0) uniform mat4    pmv;
layout(location =  1) uniform float   t_min;
layout(location =  2) uniform float   t_max;
layout(location =  3) uniform float   v_min;
layout(location =  4) uniform float   v_max;
layout(location =  5) uniform float   width;
layout(location =  6) uniform float   height;
layout(location =  7) uniform float   y_offset;
layout(location =  9) uniform bool    snap_to_pixels;
layout(location = 21) uniform float   u_line_px;

flat out vec2 fs_p_prev;
flat out vec2 fs_p0;
flat out vec2 fs_p1;
flat out vec2 fs_p_next;

vec2 sample_to_pos(uint idx)
{
    float rt = max(t_max - t_min, 1e-30);
    float rv = max(v_max - v_min, 1e-30);

    float x = width  *       (u_samples.samples[idx].t_rel - t_min) / rt;
    float y = height * (1.0 - (u_samples.samples[idx].y - v_min) / rv) + y_offset;

    if (snap_to_pixels) {
        x = floor(x) + 0.5;
        y = floor(y) + 0.5;
    }
    return vec2(x, y);
}

void main()
{
    // Adjacency buffer indexes: [seg+0] = prev, [seg+1] = p0, [seg+2] = p1,
    //                           [seg+3] = next. Boundary samples already
    // clamped on the host side via the duplicated endpoints.
    uint seg = uint(gl_InstanceID);
    vec2 p_prev = sample_to_pos(u_adjacency.indices[seg + 0u]);
    vec2 p0     = sample_to_pos(u_adjacency.indices[seg + 1u]);
    vec2 p1     = sample_to_pos(u_adjacency.indices[seg + 2u]);
    vec2 p_next = sample_to_pos(u_adjacency.indices[seg + 3u]);

    vec2 seg_v = p1 - p0;
    float seg_len = length(seg_v);

    float half_px = max(u_line_px * 0.5, 0.5);

    vec2 pos;
    if (seg_len <= 1e-6) {
        // Degenerate: collapse all four vertices onto p0 so the strip
        // contributes nothing. The old GS returned without emitting; here
        // we keep the draw call simple and let the rasterizer cull.
        pos = p0;
    }
    else {
        vec2 dir = seg_v / seg_len;
        vec2 n   = vec2(-dir.y, dir.x);

        vec2 p0_ext = p0 - dir * half_px;
        vec2 p1_ext = p1 + dir * half_px;

        // Triangle strip vertex order: v0 v1 v2 v3 = TL TR-of-p0, TL TR-of-p1
        //   gl_VertexID 0 -> p0_ext + n
        //   gl_VertexID 1 -> p0_ext - n
        //   gl_VertexID 2 -> p1_ext + n
        //   gl_VertexID 3 -> p1_ext - n
        vec2 base = (gl_VertexID < 2) ? p0_ext : p1_ext;
        float n_sign = (gl_VertexID & 1) == 0 ? 1.0 : -1.0;
        pos = base + n * (half_px * n_sign);
    }

    fs_p_prev = p_prev;
    fs_p0     = p0;
    fs_p1     = p1;
    fs_p_next = p_next;
    gl_Position = pmv * vec4(pos, 0.0, 1.0);
}
