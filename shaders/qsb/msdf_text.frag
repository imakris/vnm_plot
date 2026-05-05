#version 440

layout(std140, binding = 0) uniform Block
{
    mat4  pmv;
    vec4  color;
    float px_range;
} u;

layout(binding = 1) uniform sampler2D msdf_tex;

layout(location = 0) smooth in vec2 vs_tex_coord;
layout(location = 1) flat   in vec4 vs_tex_bounds;

layout(location = 0) out vec4 out_color;

float median(vec3 v)
{
    return max(min(v.r, v.g), min(max(v.r, v.g), v.b));
}

void main()
{
    // Clamp the lookup inside the glyph box so neighbouring atlas glyphs
    // cannot bleed across the half-texel edge under bilinear filtering.
    vec2 atlas_size   = vec2(textureSize(msdf_tex, 0));
    vec2 glyph_span   = max(vs_tex_bounds.zw - vs_tex_bounds.xy, vec2(0.0));
    vec2 half_texel   = vec2(0.5) / atlas_size;
    vec2 clamp_margin = min(half_texel, glyph_span * 0.499);
    vec2 uv_min       = vs_tex_bounds.xy + clamp_margin;
    vec2 uv_max       = vs_tex_bounds.zw - clamp_margin;
    vec2 clamped_uv   = clamp(vs_tex_coord, uv_min, uv_max);

    vec4 texel = texture(msdf_tex, clamped_uv);

    // px_range maps the MSDF gradient width to screen pixels.
    float sd    = (median(texel.rgb) - 0.5) * u.px_range;
    float alpha = clamp(sd + 0.5, 0.0, 1.0);

    // SDF clipping for artifact cleanup.
    if (texel.a > 0.0) {
        float sd_sdf = (texel.a - 0.5) * u.px_range;
        float mask   = clamp(sd_sdf + 0.5, 0.0, 1.0);
        alpha = min(alpha, mask);
    }

    out_color = vec4(u.color.rgb, u.color.a * alpha);
}
