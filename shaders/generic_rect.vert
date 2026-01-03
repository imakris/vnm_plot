#version 430

layout(location = 0) in vec4 color_in;
layout(location = 1) in vec4 position_in;

out vec4 vs_col;

void main(void)
{
    gl_Position = position_in;
    vs_col = color_in;
};
