#version 430

uniform mat4    pmv;

layout (points) in;
layout (triangle_strip, max_vertices = 4) out;

in vec4 vs_col[];
out vec4 gs_col;

void main()
{
    gs_col = vs_col[0];
    vec4 v = gl_in[0].gl_Position;
    gl_Position = pmv * vec4(v.xy, 0, 1); EmitVertex();
    gl_Position = pmv * vec4(v.zy, 0, 1); EmitVertex();
    gl_Position = pmv * vec4(v.xw, 0, 1); EmitVertex();
    gl_Position = pmv * vec4(v.zw, 0, 1); EmitVertex(); EndPrimitive();
}
