#version 430

layout(location =  3) uniform float   v_min;
layout(location =  4) uniform float   v_max;
layout(location =  6) uniform double  height;
layout(location =  7) uniform float   y_offset;
uniform float win_h;

in GS_out
{
    vec4 color;
    float t; // lerp t
} gs_out;

out vec4 color;

void main(void)
{
    float y = (win_h - gl_FragCoord.y) - y_offset;

    float vmin = v_min;
    float vmax = v_max;
    float h    = float(height);

    float vy = (1.0-(y / h)) * (vmax-vmin) + vmin;
    if (vy > vmax || vy < vmin) {
        discard;
    }

    color = gs_out.color;
}
