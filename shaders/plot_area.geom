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
uniform vec4 zero_axis_color;

layout (lines_adjacency) in;
layout (triangle_strip, max_vertices = 8) out;

in Sample
{
    double t;
    float v;
} gs_in[];

out GS_out
{
    vec4 color;
    float t; // lerp t
} gs_out;

void main()
{
    double r_t = max(t_max - t_min, 1e-30);
    float r_v = max(v_max - v_min, 1e-30);

    float x0 = float(width * (gs_in[1].t-t_min)/r_t  );
    float x1 = float(width * (gs_in[2].t-t_min)/r_t  );

    float color_denom = max(abs(v_min), abs(v_max));

    vec4 axis_color0 = color;
    vec4 axis_color1 = color;
    axis_color0.w = axis_color1.w = 0.57*color.w;

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

    // Zero axis line
    float a2 = 0.8;
    if (y_offset > 0.0) {
        a2 = 0.2;
    }
    gl_Position = pmv * vec4(x0,    y_axis+a2, 0, 1); gs_out.color = zero_axis_color; gs_out.t=0.; EmitVertex();
    gl_Position = pmv * vec4(x1,    y_axis+a2, 0, 1); gs_out.color = zero_axis_color; gs_out.t=1.; EmitVertex();
    gl_Position = pmv * vec4(x0,    y_axis-a2, 0, 1); gs_out.color = zero_axis_color; gs_out.t=0.; EmitVertex();
    gl_Position = pmv * vec4(x1,    y_axis-a2, 0, 1); gs_out.color = zero_axis_color; gs_out.t=1.; EmitVertex();
    EndPrimitive();
}
