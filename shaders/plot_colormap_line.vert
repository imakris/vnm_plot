#version 430
#extension GL_ARB_gpu_shader_int64 : require

// Mirror of plot_line.vert with an additional per-sample signal channel
// pulled from binding 2.  Custom layouts using COLORMAP_LINE must populate
// that buffer; the default function_sample layout has no signal field, so
// the host leaves binding 2 unbound and the buffer-size check picks 0.0.

layout(std430, binding = 0) readonly buffer Sample_buffer
{
    uint raw[];
} u_samples;

layout(std430, binding = 1) readonly buffer Adjacency_index_buffer
{
    uint indices[];
} u_adjacency;

layout(std430, binding = 2) readonly buffer Signal_buffer
{
    float values[];
} u_signal;

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

flat out vec2  fs_p_prev;
flat out vec2  fs_p0;
flat out vec2  fs_p1;
flat out vec2  fs_p_next;
flat out float fs_signal_0;
flat out float fs_signal_1;

uniform uint u_sample_stride_uints;
uniform uint u_sample_x_offset_uints;
uniform uint u_sample_y_offset_uints;

double sample_x(uint idx)
{
    uint base = idx * u_sample_stride_uints + u_sample_x_offset_uints;
    return packDouble2x32(uvec2(u_samples.raw[base], u_samples.raw[base + 1u]));
}

float sample_y(uint idx)
{
    return uintBitsToFloat(u_samples.raw[idx * u_sample_stride_uints + u_sample_y_offset_uints]);
}

float sample_signal(uint idx)
{
    if (u_signal.values.length() == 0) {
        return 0.0;
    }
    return u_signal.values[idx];
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
    uint seg = uint(gl_InstanceID);
    uint i_prev = u_adjacency.indices[seg + 0u];
    uint i_p0   = u_adjacency.indices[seg + 1u];
    uint i_p1   = u_adjacency.indices[seg + 2u];
    uint i_next = u_adjacency.indices[seg + 3u];

    vec2 p_prev = sample_to_pos(i_prev);
    vec2 p0     = sample_to_pos(i_p0);
    vec2 p1     = sample_to_pos(i_p1);
    vec2 p_next = sample_to_pos(i_next);

    vec2 seg_v = p1 - p0;
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
    float half_px = max(u_line_px * 0.5, 0.5);

    vec2 p0_ext = p0 - dir * half_px;
    vec2 p1_ext = p1 + dir * half_px;

    vec2 base = (gl_VertexID < 2) ? p0_ext : p1_ext;
    float n_sign = (gl_VertexID & 1) == 0 ? 1.0 : -1.0;
    vec2 pos = base + n * (half_px * n_sign);

    fs_p_prev   = p_prev;
    fs_p0       = p0;
    fs_p1       = p1;
    fs_p_next   = p_next;
    fs_signal_0 = sample_signal(i_p0);
    fs_signal_1 = sample_signal(i_p1);
    gl_Position = pmv * vec4(pos, 0.0, 1.0);
}
