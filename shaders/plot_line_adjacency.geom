#version 430

layout(location =  0) uniform mat4    pmv;
layout(location =  1) uniform double  t_min;
layout(location =  2) uniform double  t_max;
layout(location =  3) uniform float   v_min;
layout(location =  4) uniform float   v_max;
layout(location =  5) uniform double  width;
layout(location =  6) uniform double  height;
layout(location =  7) uniform float   y_offset;
layout(location =  8) uniform vec4    color;
layout(location =  9) uniform bool    snap_to_pixels;
layout(location = 21) uniform float   u_line_px;

layout(lines_adjacency) in;
layout(triangle_strip, max_vertices = 4) out;

in Sample {
    double t;
    float v;
    flat int status;
} gs_in[];

out vec4 line_color;

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

vec2 safe_normalize(vec2 v, vec2 fallback)
{
    const float len = length(v);
    if (len > 1e-6) {
        return v / len;
    }
    return fallback;
}

vec2 safe_miter(vec2 n0, vec2 n1)
{
    vec2 miter = n0 + n1;
    const float len = length(miter);
    if (len > 1e-6) {
        return miter / len;
    }
    return n1;
}

void main()
{
    const vec2 p_prev = sample_to_pos(0);
    const vec2 p0 = sample_to_pos(1);
    const vec2 p1 = sample_to_pos(2);
    const vec2 p_next = sample_to_pos(3);

    const vec2 dir1 = safe_normalize(p1 - p0, vec2(1.0, 0.0));
    const vec2 dir0 = safe_normalize(p0 - p_prev, dir1);
    const vec2 dir2 = safe_normalize(p_next - p1, dir1);

    const vec2 n0 = vec2(-dir0.y, dir0.x);
    const vec2 n1 = vec2(-dir1.y, dir1.x);
    const vec2 n2 = vec2(-dir2.y, dir2.x);

    const float half_px = max(u_line_px * 0.5, 0.5);
    const float k_miter_limit = 4.0;
    const float min_denom = 1.0 / k_miter_limit;

    const vec2 miter0 = safe_miter(n0, n1);
    const float denom0 = dot(miter0, n1);
    const vec2 offset0 = (denom0 > min_denom) ? (miter0 * (half_px / denom0)) : (n1 * half_px);

    const vec2 miter1 = safe_miter(n1, n2);
    const float denom1 = dot(miter1, n1);
    const vec2 offset1 = (denom1 > min_denom) ? (miter1 * (half_px / denom1)) : (n1 * half_px);

    gl_Position = pmv * vec4(p0 + offset0, 0.0, 1.0);
    line_color = color;
    EmitVertex();

    gl_Position = pmv * vec4(p1 + offset1, 0.0, 1.0);
    line_color = color;
    EmitVertex();

    gl_Position = pmv * vec4(p0 - offset0, 0.0, 1.0);
    line_color = color;
    EmitVertex();

    gl_Position = pmv * vec4(p1 - offset1, 0.0, 1.0);
    line_color = color;
    EmitVertex();

    EndPrimitive();
}
