#version 440

layout(location = 0) in  vec4 vs_col;
layout(location = 0) out vec4 frag_color;

void main()
{
    frag_color = vs_col;
}
