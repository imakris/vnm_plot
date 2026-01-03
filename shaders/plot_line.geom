#version 430

layout(location =  0) uniform mat4    pmv;
layout(location =  1) uniform double  t_min;
layout(location =  2) uniform double  t_max;
layout(location =  3) uniform double  v_min;
layout(location =  4) uniform double  v_max;
layout(location =  5) uniform double  width;
layout(location =  6) uniform double  height;
layout(location =  7) uniform float   y_offset;
layout(location =  8) uniform vec4    color;
layout(location =  9) uniform bool    snap_to_pixels;

layout (lines) in;
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
    double rv = max(v_max - v_min, 1e-30);

    // A
    double x0 = width  *      (gs_in[0].t - t_min) / rt;
    double y0 = height * (1.0 - (double(gs_in[0].v) - v_min) / rv) + double(y_offset);
    emit_point(x0, y0);

    // B
    double x1 = width  *      (gs_in[1].t - t_min) / rt;
    double y1 = height * (1.0 - (double(gs_in[1].v) - v_min) / rv) + double(y_offset);
    emit_point(x1, y1);

    EndPrimitive();
}
