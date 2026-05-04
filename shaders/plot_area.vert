#version 430
#extension GL_ARB_gpu_shader_int64 : require

// One instance = one segment (samples p0 -> p1) of an area fill. The host
// runs this shader twice per AREA pipe:
//
//   * pass 0 (u_axis_pass = 0): six vertices forming a triangle strip that
//     fills between the curve and the zero axis. Same-sign segments lay out
//     a trapezoid (v0..v3 with v4=v5=v3 padding); sign-flip segments emit
//     two half-triangles meeting on the zero axis at x_mid.
//   * pass 1 (u_axis_pass = 1): four vertices forming the zero-axis
//     emphasis quad in zero_axis_color.
//
// Splitting the two phases keeps each instance to four useful triangles
// instead of stitching them together with a degenerate-connector chain.
//
// Sample data is fed via instanced vertex attributes: locations 0/1 hold
// p0 (sample i), locations 4/5 hold p1 (sample i+1). Both bindings point
// at the same points VBO with location 4/5's offset shifted by one stride,
// so the GPU's attribute fetcher does the next-sample lookup with no
// shader-side indexing.

layout(location = 0) in double in_x0;
layout(location = 1) in float  in_y0;
layout(location = 4) in double in_x1;
layout(location = 5) in float  in_y1;

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
uniform uint u_axis_pass;

out GS_out
{
    vec4  color;
    float t;
} gs_out;

void main()
{
    double rt = max(t_max - t_min, 1e-30lf);
    float  rv = max(v_max - v_min, 1e-30);

    float x0 = float(width * (in_x0 - t_min) / rt);
    float x1 = float(width * (in_x1 - t_min) / rt);

    float y_axis = float(height * (1.0lf - (0.0lf - double(v_min)) / double(rv))) + y_offset;

    // Axis emphasis offset: matches the original geometry shader, which
    // inverts the band when the pass is rendering a preview row.
    float a2 = (y_offset > 0.0) ? 0.2 : 0.8;

    int  vid = gl_VertexID;
    vec2 pos;
    vec4 vcolor;
    float vt = 0.0;

    if (u_axis_pass != 0u) {
        // Pass 1: zero-axis emphasis quad.
        switch (vid) {
            case 0:  pos = vec2(x0, y_axis + a2); vt = 0.0; break;
            case 1:  pos = vec2(x1, y_axis + a2); vt = 1.0; break;
            case 2:  pos = vec2(x0, y_axis - a2); vt = 0.0; break;
            default: pos = vec2(x1, y_axis - a2); vt = 1.0; break;
        }
        vcolor = zero_axis_color;
    }
    else {
        // Pass 0: fill between curve and zero axis.
        float color_denom = max(abs(v_min), abs(v_max));

        vec4 axis_color0 = color;
        vec4 axis_color1 = color;
        axis_color0.w = axis_color1.w = 0.57 * color.w;

        float cv0 = in_y0;
        float cv1 = in_y1;

        vec4 v0_color = color; v0_color.w = axis_color0.w + 0.3 * abs(cv0) / color_denom;
        vec4 v1_color = color; v1_color.w = axis_color1.w + 0.3 * abs(cv1) / color_denom;

        if (cv0 < 0) { v0_color = v0_color.zyxw; axis_color0 = axis_color0.zyxw; }
        if (cv1 < 0) { v1_color = v1_color.zyxw; axis_color1 = axis_color1.zyxw; }

        float y0 = float(height * (1.0lf - (double(cv0) - double(v_min)) / double(rv))) + y_offset;
        float y1 = float(height * (1.0lf - (double(cv1) - double(v_min)) / double(rv))) + y_offset;

        bool  sign_flip = (cv0 * cv1) < 0.0;
        float mid       = (cv0 - cv1) != 0.0 ? cv0 / (cv0 - cv1) : 0.0;
        double t_mid    = in_x0 + (in_x1 - in_x0) * double(mid);
        float x_mid     = float(width * (t_mid - t_min) / rt);
        float am        = abs(mid);

        if (sign_flip) {
            switch (vid) {
                case 0:  pos = vec2(x0,    y0);     vcolor = v0_color;    vt = 0.0; break;
                case 1:  pos = vec2(x0,    y_axis); vcolor = axis_color0; vt = 0.0; break;
                case 2:  pos = vec2(x_mid, y_axis); vcolor = axis_color0; vt = am;  break;
                case 3:  pos = vec2(x_mid, y_axis); vcolor = axis_color1; vt = am;  break;
                case 4:  pos = vec2(x1,    y1);     vcolor = v1_color;    vt = 1.0; break;
                default: pos = vec2(x1,    y_axis); vcolor = axis_color1; vt = 1.0; break;
            }
        }
        else {
            switch (vid) {
                case 0:  pos = vec2(x0, y0);     vcolor = v0_color;    vt = 0.0; break;
                case 1:  pos = vec2(x1, y1);     vcolor = v1_color;    vt = 1.0; break;
                case 2:  pos = vec2(x0, y_axis); vcolor = axis_color0; vt = 0.0; break;
                case 3:  pos = vec2(x1, y_axis); vcolor = axis_color1; vt = 1.0; break;
                case 4:  pos = vec2(x1, y_axis); vcolor = axis_color1; vt = 1.0; break;
                default: pos = vec2(x1, y_axis); vcolor = axis_color1; vt = 1.0; break;
            }
        }
    }

    gs_out.color = vcolor;
    gs_out.t     = vt;
    gl_Position  = pmv * vec4(pos, 0.0, 1.0);
}
