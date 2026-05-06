#version 440
#extension GL_GOOGLE_include_directive : require

// Two-pass area fill. Pass 0 fills between the curve and the zero axis;
// pass 1 emits the zero-axis emphasis quad on top. Splitting the passes
// keeps each instance to four useful triangles instead of stitching them
// together with a degenerate-connector chain.
//
// Sample data is fed via instanced vertex attributes: locations 0/1 hold
// p0 (sample i), locations 4/5 hold p1 (sample i+1). Both bindings point
// at the same gpu_sample_t VBO with location 4/5's offset shifted by one
// sizeof(gpu_sample_t).

#include "uniform_blocks.glsl"

layout(location = 0) in float in_x0_rel;
layout(location = 1) in float in_y0;
layout(location = 4) in float in_x1_rel;
layout(location = 5) in float in_y1;

layout(std140, binding = 0) uniform Block
{
    Series_view_t view;
    vec4 zero_axis_color;
    int  axis_pass;
} u;

layout(location = 0) out vec4  vs_color;

void main()
{
    float rt = max(u.view.t_max - u.view.t_min, 1e-30);
    float rv = max(u.view.v_max - u.view.v_min, 1e-30);

    float x0     = u.view.width * (in_x0_rel - u.view.t_min) / rt;
    float x1     = u.view.width * (in_x1_rel - u.view.t_min) / rt;
    float y_axis = u.view.height * (1.0 - (0.0 - u.view.v_min) / rv) + u.view.y_offset;

    // Axis emphasis offset, in pixels. Preview rows (y_offset > 0) shrink
    // the emphasis band so it stays visually subordinate to the main band.
    float a2 = (u.view.y_offset > 0.0) ? 0.2 : 0.8;

    int  vid    = gl_VertexIndex;
    vec2 pos    = vec2(0.0);
    vec4 vcolor = u.view.color;

    if (u.axis_pass != 0) {
        // Pass 1: zero-axis emphasis quad.
        switch (vid) {
            case 0:  pos = vec2(x0, y_axis + a2); break;
            case 1:  pos = vec2(x1, y_axis + a2); break;
            case 2:  pos = vec2(x0, y_axis - a2); break;
            default: pos = vec2(x1, y_axis - a2); break;
        }
        vcolor = u.zero_axis_color;
    }
    else {
        // Pass 0: fill between curve and zero axis.
        float color_denom = max(abs(u.view.v_min), abs(u.view.v_max));

        vec4 axis_color0 = u.view.color;
        vec4 axis_color1 = u.view.color;
        axis_color0.w = axis_color1.w = 0.57 * u.view.color.w;

        float cv0 = in_y0;
        float cv1 = in_y1;

        vec4 v0_color = u.view.color; v0_color.w = axis_color0.w + 0.3 * abs(cv0) / color_denom;
        vec4 v1_color = u.view.color; v1_color.w = axis_color1.w + 0.3 * abs(cv1) / color_denom;

        if (cv0 < 0.0) { v0_color = v0_color.zyxw; axis_color0 = axis_color0.zyxw; }
        if (cv1 < 0.0) { v1_color = v1_color.zyxw; axis_color1 = axis_color1.zyxw; }

        float y0 = u.view.height * (1.0 - (cv0 - u.view.v_min) / rv) + u.view.y_offset;
        float y1 = u.view.height * (1.0 - (cv1 - u.view.v_min) / rv) + u.view.y_offset;

        bool  sign_flip = (cv0 * cv1) < 0.0;
        float mid       = (cv0 - cv1) != 0.0 ? cv0 / (cv0 - cv1) : 0.0;
        float t_mid     = in_x0_rel + (in_x1_rel - in_x0_rel) * mid;
        float x_mid     = u.view.width * (t_mid - u.view.t_min) / rt;

        if (sign_flip) {
            switch (vid) {
                case 0:  pos = vec2(x0,    y0);     vcolor = v0_color;    break;
                case 1:  pos = vec2(x0,    y_axis); vcolor = axis_color0; break;
                case 2:  pos = vec2(x_mid, y_axis); vcolor = axis_color0; break;
                case 3:  pos = vec2(x_mid, y_axis); vcolor = axis_color1; break;
                case 4:  pos = vec2(x1,    y1);     vcolor = v1_color;    break;
                default: pos = vec2(x1,    y_axis); vcolor = axis_color1; break;
            }
        }
        else {
            switch (vid) {
                case 0:  pos = vec2(x0, y0);     vcolor = v0_color;    break;
                case 1:  pos = vec2(x1, y1);     vcolor = v1_color;    break;
                case 2:  pos = vec2(x0, y_axis); vcolor = axis_color0; break;
                case 3:  pos = vec2(x1, y_axis); vcolor = axis_color1; break;
                case 4:  pos = vec2(x1, y_axis); vcolor = axis_color1; break;
                default: pos = vec2(x1, y_axis); vcolor = axis_color1; break;
            }
        }
    }

    vs_color    = vcolor;
    gl_Position = u.view.pmv * vec4(pos, 0.0, 1.0);
}
