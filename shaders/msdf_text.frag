#version 330 core

in VS_out
{
    smooth vec2 tex_coord;
    flat   vec4 tex_bounds;
} vs_in;

out vec4 out_color;

uniform sampler2D tex;
uniform vec4 color;
uniform float px_range;

float median(vec3 v)
{
    return max(min(v.r, v.g), min(max(v.r, v.g), v.b));
}

void main()
{
    // Clamp UV to prevent bleeding from adjacent glyphs
    vec2 atlas_size = vec2(textureSize(tex, 0));
    vec2 glyph_span = max(vs_in.tex_bounds.zw - vs_in.tex_bounds.xy, vec2(0.0));
    vec2 half_texel = vec2(0.5) / atlas_size;
    vec2 clamp_margin = min(half_texel, glyph_span * 0.499);
    vec2 uv_min = vs_in.tex_bounds.xy + clamp_margin;
    vec2 uv_max = vs_in.tex_bounds.zw - clamp_margin;
    vec2 clamped_uv = clamp(vs_in.tex_coord, uv_min, uv_max);

    vec4 texel = texture(tex, clamped_uv);

    // MSDF distance calculation
    // px_range maps the gradient width to screen pixels
    float sd = (median(texel.rgb) - 0.5) * px_range;
    float alpha = clamp(sd + 0.5, 0.0, 1.0);

    // SDF clipping for artifact cleanup
    if (texel.a > 0.0) {
        float sd_sdf = (texel.a - 0.5) * px_range;
        float mask = clamp(sd_sdf + 0.5, 0.0, 1.0);
        alpha = min(alpha, mask);
    }

    out_color = vec4(color.rgb, color.a * alpha);
}
