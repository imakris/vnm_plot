#version 430
#extension GL_ARB_gpu_shader_int64 : require

// Instance = one segment of the line strip.
// Per instance, the four triangle-strip corners (gl_VertexID 0..3) form a
// thickened screen-space quad spanning samples p0 -> p1, with prev and next
// passed flat to the fragment shader for rounded segment joins.
//
// Sample data is pulled from an SSBO mirroring the function_sample_t layout
// (packed: double x, float y, float y_min, float y_max -> 20 bytes / 5 uints).
// The neighbour-index buffer mirrors the adjacency EBO produced on the host:
// it contains [first, first, first+1, ..., first+count-1, first+count-1] so
// boundary segments naturally clamp prev/next to the endpoint sample.

layout(std430, binding = 0) readonly buffer Sample_buffer
{
    uint raw[];
} u_samples;

layout(std430, binding = 1) readonly buffer Adjacency_index_buffer
{
    uint indices[];
} u_adjacency;

layout(location =  0) uniform mat4    pmv;
layout(location =  1) uniform double  t_min;
layout(location =  2) uniform double  t_max;
layout(location =  3) uniform float   v_min;
layout(location =  4) uniform float   v_max;
layout(location =  5) uniform double  width;
layout(location =  6) uniform double  height;
layout(location =  7) uniform float   y_offset;
layout(location =  9) uniform bool    snap_to_pixels;
layout(location = 21) uniform float   u_line_px;

flat out vec2 fs_p_prev;
flat out vec2 fs_p0;
flat out vec2 fs_p1;
flat out vec2 fs_p_next;

const uint k_sample_uint_stride = 5u; // 20 bytes / 4

double sample_x(uint idx)
{
    uint base = idx * k_sample_uint_stride;
    return packDouble2x32(uvec2(u_samples.raw[base], u_samples.raw[base + 1u]));
}

float sample_y(uint idx)
{
    return uintBitsToFloat(u_samples.raw[idx * k_sample_uint_stride + 2u]);
}

vec2 sample_to_pos(uint idx)
{
    double rt = max(t_max - t_min, 1e-30lf);
    double rv = max(double(v_max - v_min), 1e-30lf);

    double x = width  *       (sample_x(idx) - t_min) / rt;
    double y = height * (1.0lf - (double(sample_y(idx)) - double(v_min)) / rv) + double(y_offset);

    if (snap_to_pixels) {
        x = floor(x) + 0.5lf;
        y = floor(y) + 0.5lf;
    }
    return vec2(float(x), float(y));
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

    // Degenerate segment: collapse the quad to a single point at p0 so
    // subsequent triangles cull/overlap. The fragment-shader rounded-end
    // logic still produces correct visuals from the prev/next varyings.
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
    float half_px = max(u_line_px * 0.5, 0.5);

    vec2 p0_ext = p0 - dir * half_px;
    vec2 p1_ext = p1 + dir * half_px;

    // Triangle strip vertex order: v0 v1 v2 v3 = TL TR-of-p0, TL TR-of-p1
    //   gl_VertexID 0 -> p0_ext + n
    //   gl_VertexID 1 -> p0_ext - n
    //   gl_VertexID 2 -> p1_ext + n
    //   gl_VertexID 3 -> p1_ext - n
    vec2 base = (gl_VertexID < 2) ? p0_ext : p1_ext;
    float n_sign = (gl_VertexID & 1) == 0 ? 1.0 : -1.0;
    vec2 pos = base + n * (half_px * n_sign);

    fs_p_prev = p_prev;
    fs_p0     = p0;
    fs_p1     = p1;
    fs_p_next = p_next;
    gl_Position = pmv * vec4(pos, 0.0, 1.0);
}
