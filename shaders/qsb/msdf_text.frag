#version 440

layout(std140, binding = 0) uniform Block
{
    mat4  pmv;
    vec4  color;
    vec4  shadow_color;
    float px_range;
    float shadow_radius;
    float lcd_subpixel_order;
    float padding;
    vec4  background_color;
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

float glyph_alpha_at_ratio(vec2 glyph_ratio, vec2 uv_min, vec2 uv_max)
{
    vec2 glyph_span = max(vs_tex_bounds.zw - vs_tex_bounds.xy, vec2(0.000001));
    vec2 glyph_uv   = vs_tex_bounds.xy + glyph_ratio * glyph_span;
    return glyph_alpha_from_texel(sample_glyph(glyph_uv, uv_min, uv_max));
}

vec3 filtered_lcd_coverage(
    vec2 glyph_ratio,
    vec2 subpixel_step,
    bool forward_order,
    vec2 uv_min,
    vec2 uv_max)
{
    float sample_0 = glyph_alpha_at_ratio(glyph_ratio - subpixel_step * 3.0, uv_min, uv_max);
    float sample_1 = glyph_alpha_at_ratio(glyph_ratio - subpixel_step * 2.0, uv_min, uv_max);
    float sample_2 = glyph_alpha_at_ratio(glyph_ratio - subpixel_step, uv_min, uv_max);
    float sample_3 = glyph_alpha_at_ratio(glyph_ratio, uv_min, uv_max);
    float sample_4 = glyph_alpha_at_ratio(glyph_ratio + subpixel_step, uv_min, uv_max);
    float sample_5 = glyph_alpha_at_ratio(glyph_ratio + subpixel_step * 2.0, uv_min, uv_max);
    float sample_6 = glyph_alpha_at_ratio(glyph_ratio + subpixel_step * 3.0, uv_min, uv_max);

    float filter_edge = 0.03125;
    float filter_side = 0.30078125;
    float filter_center = 0.3359375;
    float first_coverage =
        sample_0 * filter_edge +
        sample_1 * filter_side +
        sample_2 * filter_center +
        sample_3 * filter_side +
        sample_4 * filter_edge;
    float center_coverage =
        sample_1 * filter_edge +
        sample_2 * filter_side +
        sample_3 * filter_center +
        sample_4 * filter_side +
        sample_5 * filter_edge;
    float last_coverage =
        sample_2 * filter_edge +
        sample_3 * filter_side +
        sample_4 * filter_center +
        sample_5 * filter_side +
        sample_6 * filter_edge;

    return forward_order
        ? vec3(first_coverage, center_coverage, last_coverage)
        : vec3(last_coverage, center_coverage, first_coverage);
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

    bool lcd_rgb = u.lcd_subpixel_order > 0.5 && u.lcd_subpixel_order < 1.5;
    bool lcd_bgr = u.lcd_subpixel_order > 1.5 && u.lcd_subpixel_order < 2.5;
    bool lcd_vrgb = u.lcd_subpixel_order > 2.5 && u.lcd_subpixel_order < 3.5;
    bool lcd_vbgr = u.lcd_subpixel_order > 3.5 && u.lcd_subpixel_order < 4.5;
    bool lcd_horizontal = lcd_rgb || lcd_bgr;
    bool lcd_vertical = lcd_vrgb || lcd_vbgr;
    vec2 safe_glyph_span = max(glyph_span, vec2(0.000001));
    vec2 glyph_ratio = (vs_tex_coord - vs_tex_bounds.xy) / safe_glyph_span;
    float horizontal_step = abs(dFdx(glyph_ratio.x)) / 3.0;
    float vertical_step = abs(dFdy(glyph_ratio.y)) / 3.0;
    bool lcd_enabled =
        (lcd_horizontal || lcd_vertical) &&
        u.shadow_radius <= 0.0 &&
        u.color.a >= 0.999 &&
        u.background_color.a >= 0.999 &&
        ((lcd_horizontal && horizontal_step > 0.0) ||
         (lcd_vertical && vertical_step > 0.0));
    if (lcd_enabled) {
        vec2 subpixel_step = lcd_horizontal
            ? vec2(horizontal_step, 0.0)
            : vec2(0.0, -vertical_step);
        bool forward_order = lcd_rgb || lcd_vrgb;
        vec3 lcd_coverage =
            filtered_lcd_coverage(glyph_ratio, subpixel_step, forward_order, uv_min, uv_max);
        float alpha = max(lcd_coverage.r, max(lcd_coverage.g, lcd_coverage.b));
        if (alpha <= 0.0) {
            out_color = vec4(u.color.rgb, 0.0);
            return;
        }

        vec3 precomposed_rgb = mix(
            u.background_color.rgb,
            u.color.rgb,
            lcd_coverage);
        vec3 straight_rgb =
            (precomposed_rgb -
                u.background_color.rgb * (1.0 - alpha)) /
            alpha;
        out_color = vec4(clamp(straight_rgb, 0.0, 1.0), alpha);
        return;
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
