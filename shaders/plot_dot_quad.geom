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

layout(location = 21) uniform float   u_point_diameter_px;

layout(points) in;
layout(triangle_strip, max_vertices = 4) out;

in Sample
{
    double t;
    float v;
} gs_in[];

out vec2 fs_uv;

void main()
{
    double r_t = t_max - t_min;
    double r_v = v_max - v_min;

    float x = float(width  *       (gs_in[0].t - t_min) / r_t);
    float y = float(height * (1.lf - (gs_in[0].v - v_min) / r_v)) + y_offset;

    float half_size = max(u_point_diameter_px * 0.5, 1.0);

    // Emit 4 vertices forming a quad centered on (x, y)
    // Triangle strip order: top-left, top-right, bottom-left, bottom-right

    gl_Position = pmv * vec4(x - half_size, y + half_size, 0, 1);
    fs_uv = vec2(-1.0, 1.0);
    EmitVertex();

    gl_Position = pmv * vec4(x + half_size, y + half_size, 0, 1);
    fs_uv = vec2(1.0, 1.0);
    EmitVertex();

    gl_Position = pmv * vec4(x - half_size, y - half_size, 0, 1);
    fs_uv = vec2(-1.0, -1.0);
    EmitVertex();

    gl_Position = pmv * vec4(x + half_size, y - half_size, 0, 1);
    fs_uv = vec2(1.0, -1.0);
    EmitVertex();

    EndPrimitive();
}
