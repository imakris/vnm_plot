#version 440
#extension GL_GOOGLE_include_directive : require

#include "uniform_blocks.glsl"

layout(std140, binding = 0) uniform Block
{
    Series_view_t view;
    vec4  color_multiplier;
    float line_px;
    int   snap_to_pixels;
    int   has_signal;
} u;

// 2D Nx1 texture; SPIRV-Cross would otherwise rewrite a sampler1D into a
// sampler2D for ES because ES has no 1D textures, leaving the C++ binding
// contract on the host ambiguous. Spelling it as 2D up front keeps the
// host's QRhiTexture allocation unconditional.
layout(binding = 4) uniform sampler2D u_colormap_tex;

layout(location = 0) flat in vec2  fs_p_prev;
layout(location = 1) flat in vec2  fs_p0;
layout(location = 2) flat in vec2  fs_p1;
layout(location = 3) flat in vec2  fs_p_next;
layout(location = 4) flat in float fs_signal_0;
layout(location = 5) flat in float fs_signal_1;

layout(location = 0) out vec4 frag_color;

struct segment_result_t {
    float dist;
    float t;
};

segment_result_t dist_to_segment(vec2 p, vec2 a, vec2 b)
{
    segment_result_t r;
    vec2  ab   = b - a;
    float len2 = dot(ab, ab);
    if (len2 <= 1e-12) {
        r.dist = length(p - a);
        r.t    = 0.0;
        return r;
    }
    r.t    = clamp(dot(p - a, ab) / len2, 0.0, 1.0);
    r.dist = length(p - (a + r.t * ab));
    return r;
}

void main()
{
    vec2 frag = vec2(gl_FragCoord.x, u.view.win_h - gl_FragCoord.y);

    segment_result_t r0     = dist_to_segment(frag, fs_p0,     fs_p1);
    float            d_prev = dist_to_segment(frag, fs_p_prev,  fs_p0).dist;
    float            d_next = dist_to_segment(frag, fs_p1,      fs_p_next).dist;

    float dist    = min(r0.dist, min(d_prev, d_next));
    float half_px = max(u.line_px * 0.5, 0.5);
    float aa      = max(fwidth(dist), 0.75);
    float alpha   = 1.0 - smoothstep(half_px - aa, half_px + aa, dist);
    if (alpha <= 0.0) {
        discard;
    }

    float signal = mix(fs_signal_0, fs_signal_1, r0.t);
    vec3  color  = texture(u_colormap_tex, vec2(clamp(signal, 0.0, 1.0), 0.0)).rgb;

    frag_color = vec4(color, alpha) * u.color_multiplier;
}
