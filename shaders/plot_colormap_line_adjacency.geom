#version 430

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

layout(lines_adjacency) in;
layout(triangle_strip, max_vertices = 4) out;

in Sample {
    double t;
    float v;
} gs_in[];

in float vs_signal[];

flat out vec2 fs_p_prev;
flat out vec2 fs_p0;
flat out vec2 fs_p1;
flat out vec2 fs_p_next;
flat out float fs_signal_0;
flat out float fs_signal_1;

vec2 sample_to_pos(int idx)
{
    const double rt = max(t_max - t_min, 1e-30);
    const double rv = max(double(v_max - v_min), 1e-30);

    double x = width  *      (gs_in[idx].t - t_min) / rt;
    double y = height * (1.0 - (double(gs_in[idx].v) - double(v_min)) / rv) + double(y_offset);

    if (snap_to_pixels) {
        x = floor(x) + 0.5;
        y = floor(y) + 0.5;
    }

    return vec2(float(x), float(y));
}

void emit_vertex(
    vec2 pos,
    vec2 p_prev,
    vec2 p0,
    vec2 p1,
    vec2 p_next,
    float s0,
    float s1)
{
    fs_p_prev = p_prev;
    fs_p0 = p0;
    fs_p1 = p1;
    fs_p_next = p_next;
    fs_signal_0 = s0;
    fs_signal_1 = s1;
    gl_Position = pmv * vec4(pos, 0.0, 1.0);
    EmitVertex();
}

void main()
{
    const vec2 p_prev = sample_to_pos(0);
    const vec2 p0 = sample_to_pos(1);
    const vec2 p1 = sample_to_pos(2);
    const vec2 p_next = sample_to_pos(3);

    const float s0 = vs_signal[1];
    const float s1 = vs_signal[2];

    const vec2 seg = p1 - p0;
    const float seg_len = length(seg);
    if (seg_len <= 1e-6) {
        return;
    }

    const vec2 dir = seg / seg_len;
    const vec2 n = vec2(-dir.y, dir.x);
    const float half_px = max(u_line_px * 0.5, 0.5);

    const vec2 p0_ext = p0 - dir * half_px;
    const vec2 p1_ext = p1 + dir * half_px;

    const vec2 v0 = p0_ext + n * half_px;
    const vec2 v1 = p0_ext - n * half_px;
    const vec2 v2 = p1_ext + n * half_px;
    const vec2 v3 = p1_ext - n * half_px;

    emit_vertex(v0, p_prev, p0, p1, p_next, s0, s1);
    emit_vertex(v1, p_prev, p0, p1, p_next, s0, s1);
    emit_vertex(v2, p_prev, p0, p1, p_next, s0, s1);
    emit_vertex(v3, p_prev, p0, p1, p_next, s0, s1);
    EndPrimitive();
}
