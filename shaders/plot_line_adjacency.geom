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
layout(triangle_strip, max_vertices = 10) out;

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

float cross2(vec2 a, vec2 b)
{
    return a.x * b.y - a.y * b.x;
}

float intersection_param_on_miter(
    vec2 p,
    vec2 miter_convex,
    vec2 edge_point,
    vec2 edge_dir,
    float fallback)
{
    const float denom = cross2(miter_convex, edge_dir);
    if (abs(denom) < 1e-6) {
        return fallback;
    }
    return cross2(edge_point - p, edge_dir) / denom;
}

void main()
{
    const vec2 p_prev = sample_to_pos(0);
    const vec2 p0 = sample_to_pos(1);
    const vec2 p1 = sample_to_pos(2);
    const vec2 p_next = sample_to_pos(3);

    const float half_px = max(u_line_px * 0.5, 0.5);
    const float dir_eps = 1e-6;
    const float cross_eps = 1e-6;

    const vec2 seg_vec = p1 - p0;
    const float seg_len = length(seg_vec);
    if (seg_len <= dir_eps) {
        return;
    }
    const vec2 d_seg = seg_vec / seg_len;
    const vec2 n_seg = vec2(-d_seg.y, d_seg.x);

    vec2 start_left = p0 + n_seg * half_px;
    vec2 start_right = p0 - n_seg * half_px;
    vec2 end_left = p1 + n_seg * half_px;
    vec2 end_right = p1 - n_seg * half_px;
    bool have_start_junction = false;
    bool have_end_junction = false;
    vec2 start_gc = vec2(0.0);
    vec2 start_overlap = vec2(0.0);
    vec2 start_cm = vec2(0.0);
    vec2 end_gc = vec2(0.0);
    vec2 end_overlap = vec2(0.0);
    vec2 end_cm = vec2(0.0);

    {
        const vec2 in_vec = p0 - p_prev;
        const float in_len = length(in_vec);
        if (in_len > dir_eps) {
            const vec2 d_in = in_vec / in_len;
            const vec2 d_out = d_seg;
            const float turn = cross2(d_in, d_out);
            if (abs(turn) > cross_eps) {
                const float convex_sign = (turn > 0.0) ? 1.0 : -1.0;
                const vec2 n_in = vec2(-d_in.y, d_in.x);
                const vec2 n_out = n_seg;
                vec2 miter = safe_miter(n_in, n_out);
                if (length(miter) > dir_eps) {
                    const vec2 miter_convex = miter * convex_sign;
                    const vec2 miter_concave = -miter_convex;

                    const vec2 gc_in = p0 - n_in * half_px * convex_sign;
                    const vec2 gc_out = p0 - n_out * half_px * convex_sign;
                    const vec2 edge_in_point = p0 + n_in * half_px * convex_sign;
                    const vec2 edge_out_point = p0 + n_out * half_px * convex_sign;

                    float s_in = intersection_param_on_miter(
                        p0, miter_convex, edge_in_point, d_in, half_px);
                    float s_out = intersection_param_on_miter(
                        p0, miter_convex, edge_out_point, d_out, half_px);
                    float s = min(s_in, s_out);
                    s = max(s, 0.0);

                    start_overlap = p0 + s * miter_convex;
                    start_cm = p0 + miter_concave * half_px;
                    start_gc = gc_out;

                    start_left = start_overlap;
                    start_right = gc_out;
                    have_start_junction = true;
                }
            }
        }
    }

    {
        const vec2 out_vec = p_next - p1;
        const float out_len = length(out_vec);
        if (out_len > dir_eps) {
            const vec2 d_in = d_seg;
            const vec2 d_out = out_vec / out_len;
            const float turn = cross2(d_in, d_out);
            if (abs(turn) > cross_eps) {
                const float convex_sign = (turn > 0.0) ? 1.0 : -1.0;
                const vec2 n_in = n_seg;
                const vec2 n_out = vec2(-d_out.y, d_out.x);
                vec2 miter = safe_miter(n_in, n_out);
                if (length(miter) > dir_eps) {
                    const vec2 miter_convex = miter * convex_sign;
                    const vec2 miter_concave = -miter_convex;

                    const vec2 gc_in = p1 - n_in * half_px * convex_sign;
                    const vec2 edge_in_point = p1 + n_in * half_px * convex_sign;
                    const vec2 edge_out_point = p1 + n_out * half_px * convex_sign;

                    float s_in = intersection_param_on_miter(
                        p1, miter_convex, edge_in_point, d_in, half_px);
                    float s_out = intersection_param_on_miter(
                        p1, miter_convex, edge_out_point, d_out, half_px);
                    float s = min(s_in, s_out);
                    s = max(s, 0.0);

                    end_overlap = p1 + s * miter_convex;
                    end_cm = p1 + miter_concave * half_px;
                    end_gc = gc_in;

                    end_left = end_overlap;
                    end_right = gc_in;
                    have_end_junction = true;
                }
            }
        }
    }

    gl_Position = pmv * vec4(start_left, 0.0, 1.0);
    line_color = color;
    EmitVertex();

    gl_Position = pmv * vec4(end_left, 0.0, 1.0);
    line_color = color;
    EmitVertex();

    gl_Position = pmv * vec4(start_right, 0.0, 1.0);
    line_color = color;
    EmitVertex();

    gl_Position = pmv * vec4(end_right, 0.0, 1.0);
    line_color = color;
    EmitVertex();

    EndPrimitive();

    if (have_start_junction) {
        gl_Position = pmv * vec4(start_gc, 0.0, 1.0);
        line_color = color;
        EmitVertex();

        gl_Position = pmv * vec4(start_overlap, 0.0, 1.0);
        line_color = color;
        EmitVertex();

        gl_Position = pmv * vec4(start_cm, 0.0, 1.0);
        line_color = color;
        EmitVertex();

        EndPrimitive();
    }

    if (have_end_junction) {
        gl_Position = pmv * vec4(end_gc, 0.0, 1.0);
        line_color = color;
        EmitVertex();

        gl_Position = pmv * vec4(end_overlap, 0.0, 1.0);
        line_color = color;
        EmitVertex();

        gl_Position = pmv * vec4(end_cm, 0.0, 1.0);
        line_color = color;
        EmitVertex();

        EndPrimitive();
    }
}
