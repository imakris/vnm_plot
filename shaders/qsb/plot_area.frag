#version 440
#extension GL_GOOGLE_include_directive : require

#include "uniform_blocks.glsl"

layout(std140, binding = 0) uniform Block
{
    Series_view_t view;
    vec4 zero_axis_color;
    float aux_metric_min;
    float aux_metric_inv_span;
    int  has_colormap;
    int  axis_pass;
} u;

layout(binding = 1) uniform sampler2D u_colormap_tex;

layout(location = 0) in vec4  vs_color;
layout(location = 1) in float vs_t;
layout(location = 2) in float vs_aux;

layout(location = 0) out vec4 frag_color;

void main()
{
    // Clip outside the active value band; keeps the fill from leaking past
    // v_min / v_max when the fragment shifts off the rasterized triangle.
    float frag_y = (u.view.framebuffer_y_up != 0)
        ? (u.view.win_h - gl_FragCoord.y)
        : gl_FragCoord.y;
    float y    = frag_y - u.view.y_offset;
    float vmin = u.view.v_min;
    float vmax = u.view.v_max;
    float vy   = (1.0 - (y / u.view.height)) * (vmax - vmin) + vmin;
    if (vy > vmax || vy < vmin) {
        discard;
    }

    if (u.has_colormap != 0 && u.axis_pass == 0) {
        vec4 color = texture(u_colormap_tex, vec2(clamp(vs_aux, 0.0, 1.0), 0.0));
        color.a *= vs_color.a;
        frag_color = color;
    }
    else {
        frag_color = vs_color;
    }
}
