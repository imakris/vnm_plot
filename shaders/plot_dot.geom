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

layout (points) in;
layout (points, max_vertices = 1) out;

in Sample
{
    double t;
    float v;
    int status;
} gs_in[];

void main()
{
    // 5: communication failure, 1: sampling is stopped (nothing to show)
    if (gs_in[0].status == 1 || gs_in[0].status == 5)
    {
        return;
    }

    float x, y;
    double r_t = t_max-t_min;
    float r_v = v_max - v_min;
    x = float(width  *       (gs_in[0].t-t_min)/r_t  );
    y = float(height * (1.lf - (gs_in[0].v - v_min) / r_v)) + y_offset;
    gl_Position = pmv * vec4(x, y, 0, 1);
    gl_PointSize = 5.0;
    EmitVertex();

//...

    EndPrimitive();
}
