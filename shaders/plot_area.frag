#version 430

layout(location =  3) uniform double  v_min;
layout(location =  4) uniform double  v_max;
layout(location =  6) uniform double  height;
layout(location =  7) uniform float   y_offset;

in GS_out
{
    vec4 color;
    float t; // lerp t
    flat int status[2];
} gs_out;

out vec4 color;



void main(void)
{

#define FC (3.1415926*0.2)

    float x = gl_FragCoord.x;
    float y = gl_FragCoord.y-y_offset;


    float vmin = float(v_min);
    float vmax = float(v_max);
    float h    = float(height);

    float dmax = max(abs(vmin), abs(vmax));
    float vy = (1.0-(y / h)) * (vmax-vmin) + vmin;
    if (vy > vmax || vy < vmin) {
        discard;
    }


    float am0 = 0.0;
    if (gs_out.status[0] == 1 || gs_out.status[0] == 5)
    {
        am0 = 0.3 * (1.0 - gs_out.t) *
            (clamp(cos(FC*x+FC*y)*10.+2., 0.5, 1.) +
             clamp(cos(FC*x-FC*y)*10.+2., 0. , 2.) );
    }

    float am1 = 0.0;
    if (gs_out.status[1] == 1 || gs_out.status[1] == 5) {
        am1 = 0.3 * gs_out.t *
            (clamp(cos(FC*x+FC*y)*10.+2., 0.5, 1.) +
             clamp(cos(FC*x-FC*y)*10.+2., 0. , 2.) );
    }

    color = vec4(gs_out.color.xyz, gs_out.color.w - gs_out.color.w * (am0 + am1) * 0.5);
};
