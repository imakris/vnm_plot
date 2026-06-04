#version 440

layout(std140, binding = 0) uniform Block
{
    mat4  pmv;
    vec4  color;
    vec4  shadow_color;
    float px_range;
    float shadow_radius;
} u;

layout(binding = 1) uniform sampler2D msdf_tex;

layout(location = 0) smooth in vec2 vs_tex_coord;
layout(location = 1) flat   in vec4 vs_tex_bounds;

layout(location = 0) out vec4 out_color;

float median(vec3 v)
{
    return max(min(v.r, v.g), min(max(v.r, v.g), v.b));
}

float smootherstep(float edge0, float edge1, float x)
{
    float t = clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

vec4 sample_glyph(vec2 uv, vec2 uv_min, vec2 uv_max)
{
    return texture(msdf_tex, clamp(uv, uv_min, uv_max));
}

float glyph_alpha_from_texel(vec4 texel)
{
    float sd = (median(texel.rgb) - 0.5) * u.px_range;
    float alpha = clamp(sd + 0.5, 0.0, 1.0);

    // The alpha channel is the true SDF from the MTSDF atlas. Keep it as an
    // artifact mask for the crisp foreground glyph, but not as the primary
    // edge source because MSDF keeps sharper corners.
    if (texel.a > 0.0) {
        float sd_sdf = (texel.a - 0.5) * u.px_range;
        alpha = min(alpha, clamp(sd_sdf + 0.5, 0.0, 1.0));
    }

    return alpha;
}

float signed_distance_from_texel(vec4 texel)
{
    if (texel.a > 0.0) {
        return (texel.a - 0.5) * u.px_range;
    }

    return (median(texel.rgb) - 0.5) * u.px_range;
}

float shadow_alpha_from_texel(vec4 texel)
{
    float radius = max(u.shadow_radius, 0.0);
    float sd = signed_distance_from_texel(texel);
    return smootherstep(-radius, 0.5, sd);
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

    vec4 texel = sample_glyph(clamped_uv, uv_min, uv_max);

    float glyph_alpha = glyph_alpha_from_texel(texel);
    float shadow_alpha = 0.0;
    if (u.shadow_radius > 0.0 && u.shadow_color.a > 0.0) {
        shadow_alpha = shadow_alpha_from_texel(texel);
    }

    float glyph_a = u.color.a * glyph_alpha;
    float shadow_a = u.shadow_color.a * shadow_alpha;
    float out_a = glyph_a + shadow_a * (1.0 - glyph_a);
    if (out_a <= 0.0) {
        out_color = vec4(0.0);
        return;
    }

    vec3 out_rgb =
        (u.color.rgb * glyph_a + u.shadow_color.rgb * shadow_a * (1.0 - glyph_a)) /
        out_a;
    out_color = vec4(out_rgb, out_a);
}
