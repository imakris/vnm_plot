#version 430
#extension GL_ARB_gpu_shader_int64 : require

// One instance per sample. gl_VertexID selects one of the four quad corners
// (triangle-strip order: TL, TR, BL, BR).

layout(location = 0) in double in_x;
layout(location = 1) in float  in_y;

layout(location =  0) uniform mat4    pmv;
layout(location =  1) uniform double  t_min;
layout(location =  2) uniform double  t_max;
layout(location =  3) uniform float   v_min;
layout(location =  4) uniform float   v_max;
layout(location =  5) uniform double  width;
layout(location =  6) uniform double  height;
layout(location =  7) uniform float   y_offset;

layout(location = 21) uniform float   u_point_diameter_px;

out vec2 fs_uv;

void main()
{
    double r_t = max(t_max - t_min, 1e-30lf);
    float  r_v = max(v_max - v_min, 1e-30);

    float x = float(width  *       (in_x - t_min) / r_t);
    float y = float(height * (1.lf - (double(in_y) - double(v_min)) / double(r_v))) + y_offset;

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
