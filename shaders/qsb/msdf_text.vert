#version 440

layout(std140, binding = 0) uniform Block
{
    mat4  pmv;
    vec4  color;
    float px_range;
} u;

layout(location = 0) in vec2 vertex;
layout(location = 1) in vec2 tex_coord;
layout(location = 2) in vec4 tex_bounds;

layout(location = 0) smooth out vec2 vs_tex_coord;
layout(location = 1) flat   out vec4 vs_tex_bounds;

void main()
{
    vs_tex_coord  = tex_coord;
    vs_tex_bounds = tex_bounds;
    gl_Position   = u.pmv * vec4(vertex, 0.1, 1.0);
}
