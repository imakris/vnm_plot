#version 430

// One instance per sample. gl_VertexID selects one of the four quad corners
// (triangle-strip order: TL, TR, BL, BR).
//
// Per-sample data is read from a fixed gpu_sample_t vertex layout: location 0
// carries fp32 seconds relative to the per-view origin; location 1 carries
// the primary value. The host rebases timestamps on upload so the value at
// location 0 stays inside fp32's usable range.

layout(location = 0) in float in_x_rel;
layout(location = 1) in float in_y;

layout(location =  0) uniform mat4    pmv;
layout(location =  1) uniform float   t_min;
layout(location =  2) uniform float   t_max;
layout(location =  3) uniform float   v_min;
layout(location =  4) uniform float   v_max;
layout(location =  5) uniform float   width;
layout(location =  6) uniform float   height;
layout(location =  7) uniform float   y_offset;

layout(location = 21) uniform float   u_point_diameter_px;

out vec2 fs_uv;

void main()
{
    float r_t = max(t_max - t_min, 1e-30);
    float r_v = max(v_max - v_min, 1e-30);

    float x = width  *       (in_x_rel - t_min) / r_t;
    float y = height * (1.0 - (in_y - v_min) / r_v) + y_offset;

    float half_size = max(u_point_diameter_px * 0.5, 1.0);

    // Triangle strip corners selected by gl_VertexID:
    //   0 -> top-left      (-1,  1)
    //   1 -> top-right     ( 1,  1)
    //   2 -> bottom-left   (-1, -1)
    //   3 -> bottom-right  ( 1, -1)
    float ux = (gl_VertexID & 1) == 0 ? -1.0 :  1.0;
    float uy = (gl_VertexID & 2) == 0 ?  1.0 : -1.0;

    fs_uv = vec2(ux, uy);
    gl_Position = pmv * vec4(x + ux * half_size, y + uy * half_size, 0.0, 1.0);
}
