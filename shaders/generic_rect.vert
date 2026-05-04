#version 430

// Per-instance attributes (divisor = 1).
// Each instance is one rectangle.
layout(location = 0) in vec4 color_in;
layout(location = 1) in vec4 position_in;  // x0, y0, x1, y1

uniform mat4 pmv;

out vec4 vs_col;

void main(void)
{
    // Triangle strip corners, indexed by gl_VertexID:
    //   0 -> (x0, y0)
    //   1 -> (x1, y0)
    //   2 -> (x0, y1)
    //   3 -> (x1, y1)
    float x = (gl_VertexID & 1) == 0 ? position_in.x : position_in.z;
    float y = (gl_VertexID & 2) == 0 ? position_in.y : position_in.w;

    gl_Position = pmv * vec4(x, y, 0.0, 1.0);
    vs_col = color_in;
}
