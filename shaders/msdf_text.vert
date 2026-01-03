#version 330 core

uniform mat4 pmv;

layout(location = 0) in vec2 vertex;
layout(location = 1) in vec2 tex_coord;
layout(location = 2) in vec4 tex_bounds;

out VS_out
{
    smooth vec2 tex_coord;
    flat   vec4 tex_bounds;
} vs_out;

void main()
{
    vs_out.tex_coord = tex_coord;
    vs_out.tex_bounds = tex_bounds;
    gl_Position = pmv * vec4(vertex, 0.1, 1.0);
}
