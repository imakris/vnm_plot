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
layout(location =  9) uniform float   line_width;
layout(location = 10) uniform vec4    line_color;

layout (lines) in;
layout (triangle_strip, max_vertices = 18) out;

in Sample
{
    double t;
    float v;
    int status;
} gs_in[];

out vec4 gs_col;

out GS_out
{
    vec4 color;
    float t; // lerp t
    flat int status[2];
} gs_out;


vec4 get_error_color(int st)
{
    switch (st) {
        case 1:  return vec4(0.6, 0.6, 0.6, 0.8);
        case 2:  return vec4(0.8, 0.6, 0.5, 0.8);
        case 3:  return vec4(0.7, 0.0, 0.0, 0.3);
        case 4:  return vec4(0.7, 0.0, 0.0, 0.4);
        case 5:  return vec4(0.7, 0.0, 0.0, 0.6);
        default: return vec4(0.0, 0.0, 0.0, 0.0); // <- NOT the vertex that caused the error
    }
}


void main()
{
    float x, y;
    double r_t = t_max-t_min;
    double r_v = v_max-v_min;

    float x0 = float(width * (gs_in[0].t-t_min)/r_t  );
    float x1 = float(width * (gs_in[1].t-t_min)/r_t  );

    gs_out.status[0] = gs_in[0].status;
    gs_out.status[1] = gs_in[1].status;

    if (gs_in[0].status != 0 || gs_in[1].status != 0) {

        vec4 c0 = get_error_color(gs_in[0].status);
        vec4 c1;
        if (gs_in[0].status == 1) {
            // the first sample is a stop sample. Stop samples do not come in pairs.
            c1 = c0;
            gs_out.status[1] = 1;
        }
        else {
            c1 = get_error_color(gs_in[1].status);
        }

        gl_Position = pmv * vec4(x0, float(height) + y_offset, 0, 1); gs_out.color = c0; gs_out.t=0.; EmitVertex();
        gl_Position = pmv * vec4(x1, float(height) + y_offset, 0, 1); gs_out.color = c1; gs_out.t=1.; EmitVertex();
        gl_Position = pmv * vec4(x0,                 y_offset, 0, 1); gs_out.color = c0; gs_out.t=0.; EmitVertex();
        gl_Position = pmv * vec4(x1,                 y_offset, 0, 1); gs_out.color = c1; gs_out.t=1.; EmitVertex();
        EndPrimitive();

        // 5: communication failure, 1: sampling is stopped (nothing to show)
        if (gs_in[0].status == 1 || gs_in[1].status == 1 ||
            gs_in[0].status == 5 || gs_in[1].status == 5)
        {
            return;
        }
    }

    float color_denom = float(max(abs(v_min), abs(v_max)));

    vec4 axis_color0 = color;
    vec4 axis_color1 = color;
    axis_color0.w = axis_color1.w = 0.57*color.w;


    // This adapts the vertices to a more reasonable range, to avoid artifacts
    // due to post-transformation floating point precision inaccuracy.
    // The fragment shader is expected to discard fragments falling out of bounds.

    // [imak: hm? this does not really work...]
    //float cv0 = min(max(gs_in[0].v, float(v_min)*10 ), float(v_max)*10 );
    //float cv1 = min(max(gs_in[1].v, float(v_min)*10 ), float(v_max)*10 );
    float cv0 = gs_in[0].v;
    float cv1 = gs_in[1].v;

    vec4 v0_color = color; v0_color.w = axis_color0.w + 0.3*abs(cv0)/color_denom;
    vec4 v1_color = color; v1_color.w = axis_color1.w + 0.3*abs(cv1)/color_denom;

    if (cv0 < 0) { v0_color = v0_color.zyxw; axis_color0 = axis_color0.zyxw; }
    if (cv1 < 0) { v1_color = v1_color.zyxw; axis_color1 = axis_color1.zyxw; }

    float y0     = float(height * (1.lf-(cv0-v_min)/r_v) ) + y_offset;
    float y1     = float(height * (1.lf-(cv1-v_min)/r_v) ) + y_offset;
    // Always anchor fill to data value 0.0, even if it is outside the current view range.
    float y_axis = float(height * (1.lf-(0.0 - v_min)/r_v) ) + y_offset;
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
        double t_mid = gs_in[0].t + (gs_in[1].t-gs_in[0].t) * double( mid );
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


    vec4 axis_color = vec4(0.0, 0.0, 0.0, 1.0);
    float a2 = 0.8;
    if (y_offset > 0.0) {
        a2 = 0.2;
    }
    gl_Position = pmv * vec4(x0,    y_axis+a2, 0, 1); gs_out.color = axis_color; gs_out.t=0.; EmitVertex();
    gl_Position = pmv * vec4(x1,    y_axis+a2, 0, 1); gs_out.color = axis_color; gs_out.t=1.; EmitVertex();
    gl_Position = pmv * vec4(x0,    y_axis-a2, 0, 1); gs_out.color = axis_color; gs_out.t=0.; EmitVertex();
    gl_Position = pmv * vec4(x1,    y_axis-a2, 0, 1); gs_out.color = axis_color; gs_out.t=1.; EmitVertex();
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

    /*
    if (gs_in[1].t > t_min || gs_in[0].t < t_max) {
        float hlw = line_width * 0.5;
        gl_Position = pmv * vec4(x0,    y0+hlw, 0, 1); gs_out.color = line_color; EmitVertex();
        gl_Position = pmv * vec4(x1,    y1+hlw, 0, 1); gs_out.color = line_color; EmitVertex();
        gl_Position = pmv * vec4(x0,    y0-hlw, 0, 1); gs_out.color = line_color; EmitVertex();
        gl_Position = pmv * vec4(x1,    y1-hlw, 0, 1); gs_out.color = line_color; EmitVertex();
        EndPrimitive();
    }
    */

}
