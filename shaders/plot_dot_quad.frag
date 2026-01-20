#version 430

layout(location = 8) uniform vec4 color;

in vec2 fs_uv;

out vec4 frag_color;

void main()
{
    // fs_uv is in range [-1, 1] for both x and y
    float dist = length(fs_uv);

    // Discard fragments outside the circle
    if (dist > 1.0) {
        discard;
    }

    // Smooth antialiased edge
    // The smoothstep creates a soft edge over roughly 2 pixels
    float edge_width = fwidth(dist) * 1.5;
    float alpha = 1.0 - smoothstep(1.0 - edge_width, 1.0, dist);

    frag_color = vec4(color.rgb, color.a * alpha);
}
