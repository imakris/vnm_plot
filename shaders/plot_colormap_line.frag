#version 430

uniform float win_h;
uniform float u_line_px;
layout(binding = 0) uniform sampler1D u_colormap_tex;

flat in vec2 fs_p_prev;
flat in vec2 fs_p0;
flat in vec2 fs_p1;
flat in vec2 fs_p_next;
flat in float fs_signal_0;
flat in float fs_signal_1;

out vec4 frag_color;

struct Segment_result {
    float dist;
    float t;  // interpolation parameter along segment [0,1]
};

Segment_result dist_to_segment(vec2 p, vec2 a, vec2 b)
{
    Segment_result r;
    vec2 ab = b - a;
    float len2 = dot(ab, ab);
    if (len2 <= 1e-12) {
        r.dist = length(p - a);
        r.t = 0.0;
        return r;
    }
    r.t = dot(p - a, ab) / len2;
    r.t = clamp(r.t, 0.0, 1.0);
    vec2 proj = a + r.t * ab;
    r.dist = length(p - proj);
    return r;
}

void main(void)
{
    vec2 frag = vec2(gl_FragCoord.x, win_h - gl_FragCoord.y);

    // Compute distance to all three segments
    Segment_result r0 = dist_to_segment(frag, fs_p0, fs_p1);
    float d_prev = dist_to_segment(frag, fs_p_prev, fs_p0).dist;
    float d_next = dist_to_segment(frag, fs_p1, fs_p_next).dist;

    float dist = min(r0.dist, min(d_prev, d_next));

    float half_px = max(u_line_px * 0.5, 0.5);
    float aa = max(fwidth(dist), 0.75);
    float alpha = 1.0 - smoothstep(half_px - aa, half_px + aa, dist);
    if (alpha <= 0.0) {
        discard;
    }

    // Interpolate signal based on position along main segment
    // Use r0.t which is the interpolation parameter for the main segment
    float signal = mix(fs_signal_0, fs_signal_1, r0.t);

    // Sample colormap texture
    vec3 color = texture(u_colormap_tex, clamp(signal, 0.0, 1.0)).rgb;

    frag_color = vec4(color, alpha);
}
