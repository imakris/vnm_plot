#version 430

uniform vec4 color;
uniform float win_h;
uniform float u_line_px;

flat in vec2 fs_p_prev;
flat in vec2 fs_p0;
flat in vec2 fs_p1;
flat in vec2 fs_p_next;

out vec4 frag_color;

float dist_to_segment(vec2 p, vec2 a, vec2 b)
{
    vec2 ab = b - a;
    float len2 = dot(ab, ab);
    if (len2 <= 1e-12) {
        return length(p - a);
    }
    float t = dot(p - a, ab) / len2;
    t = clamp(t, 0.0, 1.0);
    vec2 proj = a + t * ab;
    return length(p - proj);
}

void main(void)
{
    vec2 frag = vec2(gl_FragCoord.x, win_h - gl_FragCoord.y);

    float d0 = dist_to_segment(frag, fs_p0, fs_p1);
    float d_prev = dist_to_segment(frag, fs_p_prev, fs_p0);
    float d_next = dist_to_segment(frag, fs_p1, fs_p_next);
    float dist = min(d0, min(d_prev, d_next));

    float half_px = max(u_line_px * 0.5, 0.5);
    float aa = max(fwidth(dist), 0.75);
    float alpha = 1.0 - smoothstep(half_px - aa, half_px + aa, dist);
    if (alpha <= 0.0) {
        discard;
    }

    frag_color = vec4(color.rgb, color.a * alpha);
}
