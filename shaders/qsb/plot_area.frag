#version 440
#extension GL_GOOGLE_include_directive : require

#include "uniform_blocks.glsl"

layout(std140, binding = 0) uniform Block
{
    Series_view_t view;
    int  interpolation;
} u;

layout(location = 0) in vec4  vs_color;

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

    frag_color = vs_color;
}
