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
layout(location =  9) uniform float   line_width;
layout(location = 10) uniform vec4    line_color;
uniform vec4 zero_axis_color;

layout (lines_adjacency) in;
layout (triangle_strip, max_vertices = 24) out;

in Sample
{
    double t;
    float v;
} gs_in[];

out vec4 gs_col;

out GS_out
{
    vec4 color;
    float t; // lerp t
} gs_out;

vec2 sample_to_pos(int idx)
{
    const double rt = max(t_max - t_min, 1e-30);
    const double rv = max(double(v_max - v_min), 1e-30);

    const double x = width  *      (gs_in[idx].t - t_min) / rt;
    const double y = height * (1.0 - (double(gs_in[idx].v) - double(v_min)) / rv) + double(y_offset);

    return vec2(float(x), float(y));
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

void emit_vertex(vec2 pos, vec4 color_out, float t_out)
{
    gl_Position = pmv * vec4(pos, 0.0, 1.0);
    gs_out.color = color_out;
    gs_out.t = t_out;
    EmitVertex();
}

void emit_line_strip(
    vec2 p_prev,
    vec2 p0,
    vec2 p1,
    vec2 p_next,
    float half_px,
    vec4 color_out)
{
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

                    if (convex_sign > 0.0) {
                        start_left = start_overlap;
                        start_right = start_gc;
                    }
                    else {
                        start_left = start_gc;
                        start_right = start_overlap;
                    }
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

                    if (convex_sign > 0.0) {
                        end_left = end_overlap;
                        end_right = end_gc;
                    }
                    else {
                        end_left = end_gc;
                        end_right = end_overlap;
                    }
                    have_end_junction = true;
                }
            }
        }
    }

    emit_vertex(start_left, color_out, 0.0);
    emit_vertex(end_left, color_out, 0.0);
    emit_vertex(start_right, color_out, 0.0);
    EndPrimitive();

    emit_vertex(end_left, color_out, 0.0);
    emit_vertex(end_right, color_out, 0.0);
    emit_vertex(start_right, color_out, 0.0);
    EndPrimitive();

    if (have_start_junction) {
        emit_vertex(start_gc, color_out, 0.0);
        emit_vertex(start_overlap, color_out, 0.0);
        emit_vertex(start_cm, color_out, 0.0);
        EndPrimitive();
    }

    if (have_end_junction) {
        emit_vertex(end_gc, color_out, 0.0);
        emit_vertex(end_overlap, color_out, 0.0);
        emit_vertex(end_cm, color_out, 0.0);
        EndPrimitive();
    }
}

void main()
{
    float x, y;
    double r_t = max(t_max - t_min, 1e-30);
    float r_v = max(v_max - v_min, 1e-30);

    float x0 = float(width * (gs_in[1].t-t_min)/r_t  );
    float x1 = float(width * (gs_in[2].t-t_min)/r_t  );

    float color_denom = max(abs(v_min), abs(v_max));

    vec4 axis_color0 = color;
    vec4 axis_color1 = color;
    axis_color0.w = axis_color1.w = 0.57*color.w;


    // This adapts the vertices to a more reasonable range, to avoid artifacts
    // due to post-transformation floating point precision inaccuracy.
    // The fragment shader is expected to discard fragments falling out of bounds.

    // [imak: hm? this does not really work...]
    //float cv0 = min(max(gs_in[0].v, float(v_min)*10 ), float(v_max)*10 );
    //float cv1 = min(max(gs_in[1].v, float(v_min)*10 ), float(v_max)*10 );
    float cv0 = gs_in[1].v;
    float cv1 = gs_in[2].v;

    vec4 v0_color = color; v0_color.w = axis_color0.w + 0.3*abs(cv0)/color_denom;
    vec4 v1_color = color; v1_color.w = axis_color1.w + 0.3*abs(cv1)/color_denom;

    if (cv0 < 0) { v0_color = v0_color.zyxw; axis_color0 = axis_color0.zyxw; }
    if (cv1 < 0) { v1_color = v1_color.zyxw; axis_color1 = axis_color1.zyxw; }

    float y0     = float(height * (1.lf - (cv0 - v_min) / r_v)) + y_offset;
    float y1     = float(height * (1.lf - (cv1 - v_min) / r_v)) + y_offset;
    // Always anchor fill to data value 0.0, even if it is outside the current view range.
    float y_axis = float(height * (1.lf - (0.0 - v_min) / r_v)) + y_offset;
    y_axis = clamp(y_axis, y_offset, y_offset + float(height));

    if (cv0 * cv1 > 0) {
        gl_Position = pmv * vec4(x0,        y0, 0, 1); gs_out.color = v0_color;    gs_out.t=0.; EmitVertex();
        gl_Position = pmv * vec4(x1,        y1, 0, 1); gs_out.color = v1_color;    gs_out.t=1.; EmitVertex();
        gl_Position = pmv * vec4(x0,    y_axis, 0, 1); gs_out.color = axis_color0; gs_out.t=0.; EmitVertex();
        gl_Position = pmv * vec4(x1,    y_axis, 0, 1); gs_out.color = axis_color1; gs_out.t=1.; EmitVertex();
        EndPrimitive();
    }
    else {
        float mid = cv0 / (cv0-cv1);
        double t_mid = gs_in[1].t + (gs_in[2].t-gs_in[1].t) * double( mid );
        float x_mid = float(width  * (t_mid-t_min)/r_t  );
        float am = abs(mid);

        gl_Position = pmv * vec4(x0,        y0, 0, 1); gs_out.color = v0_color;    gs_out.t=0.; EmitVertex();
        gl_Position = pmv * vec4(x0,    y_axis, 0, 1); gs_out.color = axis_color0; gs_out.t=0.; EmitVertex();
        gl_Position = pmv * vec4(x_mid, y_axis, 0, 1); gs_out.color = axis_color0; gs_out.t=am; EmitVertex();
        EndPrimitive();

        gl_Position = pmv * vec4(   x1,     y1, 0, 1); gs_out.color = v1_color;    gs_out.t=1.; EmitVertex();
        gl_Position = pmv * vec4(   x1, y_axis, 0, 1); gs_out.color = axis_color1; gs_out.t=1.; EmitVertex();
        gl_Position = pmv * vec4(x_mid, y_axis, 0, 1); gs_out.color = axis_color1; gs_out.t=am; EmitVertex();
        EndPrimitive();
    }


    float a2 = 0.8;
    if (y_offset > 0.0) {
        a2 = 0.2;
    }
    gl_Position = pmv * vec4(x0,    y_axis+a2, 0, 1); gs_out.color = zero_axis_color; gs_out.t=0.; EmitVertex();
    gl_Position = pmv * vec4(x1,    y_axis+a2, 0, 1); gs_out.color = zero_axis_color; gs_out.t=1.; EmitVertex();
    gl_Position = pmv * vec4(x0,    y_axis-a2, 0, 1); gs_out.color = zero_axis_color; gs_out.t=0.; EmitVertex();
    gl_Position = pmv * vec4(x1,    y_axis-a2, 0, 1); gs_out.color = zero_axis_color; gs_out.t=1.; EmitVertex();
    EndPrimitive();


/*
    // a quad
    float r = 10;
    vec4 cc = vec4(0, 1, 0, 1);
    gl_Position = pmv * vec4(x0-r, y0+r, 0, 1); gs_out.color = cc; gs_out.t=0.; EmitVertex();
    gl_Position = pmv * vec4(x0+r, y0+r, 0, 1); gs_out.color = cc; gs_out.t=0.; EmitVertex();
    gl_Position = pmv * vec4(x0-r, y0-r, 0, 1); gs_out.color = cc; gs_out.t=0.; EmitVertex();
    gl_Position = pmv * vec4(x0+r, y0-r, 0, 1); gs_out.color = cc; gs_out.t=0.; EmitVertex();
    EndPrimitive();
*/

    // Outline stroke on top of fill using adjacency-aware geometry.
    if (gs_in[2].t >= t_min && gs_in[1].t <= t_max) {
        float half_px = max(line_width * 0.5, 0.5);
        emit_line_strip(sample_to_pos(0), sample_to_pos(1), sample_to_pos(2), sample_to_pos(3), half_px, line_color);
    }

}
