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

// Adjacency-aware input: receives 4 vertices per line segment
// gs_in[0]: previous vertex (for incoming join)
// gs_in[1]: current segment start
// gs_in[2]: current segment end
// gs_in[3]: next vertex (for outgoing join)
layout (lines_adjacency) in;
layout (line_strip, max_vertices = 2) out;

in Sample {
    double t;
    float v;
    flat int status;
} gs_in[];

out vec4 line_color;

void emit_point(double xd, double yd)
{
    if (snap_to_pixels) {
        // Snap to pixel centers to avoid subpixel gaps across segments.
        xd = floor(xd) + 0.5;
        yd = floor(yd) + 0.5;
    }

    gl_Position = pmv * vec4(float(xd), float(yd), 0.0, 1.0);
    line_color = color;
    EmitVertex();
}

void main()
{
    // safe denominators
    double rt = max(t_max - t_min, 1e-30);
    double rv = max(double(v_max - v_min), 1e-30);

    // Transform adjacency vertices to screen space for future join calculations
    // For now, we just render the segment normally, but with adjacency info available

    // Previous vertex (gs_in[0]) - available for join calculation
    double x_prev = width  *      (gs_in[0].t - t_min) / rt;
    double y_prev = height * (1.0 - (double(gs_in[0].v) - double(v_min)) / rv) + double(y_offset);

    // Current segment start (gs_in[1])
    double x0 = width  *      (gs_in[1].t - t_min) / rt;
    double y0 = height * (1.0 - (double(gs_in[1].v) - double(v_min)) / rv) + double(y_offset);

    // Current segment end (gs_in[2])
    double x1 = width  *      (gs_in[2].t - t_min) / rt;
    double y1 = height * (1.0 - (double(gs_in[2].v) - double(v_min)) / rv) + double(y_offset);

    // Next vertex (gs_in[3]) - available for join calculation
    double x_next = width  *      (gs_in[3].t - t_min) / rt;
    double y_next = height * (1.0 - (double(gs_in[3].v) - double(v_min)) / rv) + double(y_offset);

    // For this conservative first step, we just render the segment normally
    // Future enhancements will use x_prev/y_prev and x_next/y_next for proper joins
    emit_point(x0, y0);
    emit_point(x1, y1);

    EndPrimitive();
}
