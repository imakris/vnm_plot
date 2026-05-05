#version 440
#extension GL_GOOGLE_include_directive : require

// One instance per sample; gl_VertexIndex selects one of four triangle-strip
// corners. Per-sample data is fed via gpu_sample_t vertex attributes.

#include "uniform_blocks.glsl"

layout(location = 0) in float in_x_rel;
layout(location = 1) in float in_y;

layout(std140, binding = 0) uniform Block
{
    Series_view_t view;
    float point_diameter_px;
} u;

layout(location = 0) out vec2 fs_uv;

void main()
{
    float r_t = max(u.view.t_max - u.view.t_min, 1e-30);
    float r_v = max(u.view.v_max - u.view.v_min, 1e-30);

    float x = u.view.width  *       (in_x_rel - u.view.t_min) / r_t;
    float y = u.view.height * (1.0 - (in_y     - u.view.v_min) / r_v) + u.view.y_offset;

    float half_size = max(u.point_diameter_px * 0.5, 1.0);

    // Triangle strip corners selected by gl_VertexIndex:
    //   0 -> top-left      (-1,  1)
    //   1 -> top-right     ( 1,  1)
    //   2 -> bottom-left   (-1, -1)
    //   3 -> bottom-right  ( 1, -1)
    float ux = (gl_VertexIndex & 1) == 0 ? -1.0 :  1.0;
    float uy = (gl_VertexIndex & 2) == 0 ?  1.0 : -1.0;

    fs_uv       = vec2(ux, uy);
    gl_Position = u.view.pmv * vec4(x + ux * half_size, y + uy * half_size, 0.0, 1.0);
}
