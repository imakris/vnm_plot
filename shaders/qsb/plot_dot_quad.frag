#version 440
#extension GL_GOOGLE_include_directive : require

#include "uniform_blocks.glsl"

layout(std140, binding = 0) uniform Block
{
    Series_view_t view;
    float point_diameter_px;
} u;

layout(location = 0) in vec2 fs_uv;

layout(location = 0) out vec4 frag_color;

void main()
{
    // fs_uv is in [-1, 1] for both axes; the quad encloses one antialiased disc.
    float dist = length(fs_uv);
    if (dist > 1.0) {
        discard;
    }

    // Smooth ~2 px edge.
    float edge_width = fwidth(dist) * 1.5;
    float alpha      = 1.0 - smoothstep(1.0 - edge_width, 1.0, dist);

    frag_color = vec4(u.view.color.rgb, u.view.color.a * alpha);
}
