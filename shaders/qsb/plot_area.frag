#version 440
#extension GL_GOOGLE_include_directive : require

#include "uniform_blocks.glsl"

layout(std140, binding = 0) uniform Block
{
    Series_view_t view;
    vec4 zero_axis_color;
    int  axis_pass;
} u;

layout(location = 0) in vec4  vs_color;
layout(location = 1) in float vs_t;

layout(location = 0) out vec4 frag_color;

void main()
{
    // Clip outside the active value band; keeps the fill from leaking past
    // v_min / v_max when the fragment shifts off the rasterized triangle.
    float y    = (u.view.win_h - gl_FragCoord.y) - u.view.y_offset;
    float vmin = u.view.v_min;
    float vmax = u.view.v_max;
    float vy   = (1.0 - (y / u.view.height)) * (vmax - vmin) + vmin;
    if (vy > vmax || vy < vmin) {
        discard;
    }

    frag_color = vs_color;
}
