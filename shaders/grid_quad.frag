#version 430

#define GRID_LEVEL_MAX 32

uniform vec2  plot_size_px;
uniform vec2  region_origin_px;
uniform vec4  grid_color;

uniform int   v_count;
uniform float v_spacing_px[GRID_LEVEL_MAX];
uniform float v_start_px[GRID_LEVEL_MAX];
uniform float v_alpha[GRID_LEVEL_MAX];
uniform float v_thickness_px[GRID_LEVEL_MAX];

uniform int   t_count;
uniform float t_spacing_px[GRID_LEVEL_MAX];
uniform float t_start_px[GRID_LEVEL_MAX];
uniform float t_alpha[GRID_LEVEL_MAX];
uniform float t_thickness_px[GRID_LEVEL_MAX];

out vec4 out_color;

float line_mask(float coord, float spacing, float start, float thickness_px)
{
    if (spacing <= 0.0) {
        return 0.0;
    }

    const float wrapped = mod(coord - start, spacing);
    const float dist = min(wrapped, spacing - wrapped);
    const float half_t = thickness_px * 0.1;
    return 1.0 - smoothstep(half_t, half_t + 1.0, dist);
}

float accumulate_lines(
    int count,
    float coord,
    const float spacing[GRID_LEVEL_MAX],
    const float start[GRID_LEVEL_MAX],
    const float alpha[GRID_LEVEL_MAX],
    const float thickness[GRID_LEVEL_MAX])
{
    float mask = 0.0;
    for (int i = 0; i < count && i < GRID_LEVEL_MAX; ++i) {
        const float contrib = line_mask(coord, spacing[i], start[i], thickness[i]);
        mask = max(mask, contrib * alpha[i]);
    }
    return mask;
}

void main()
{
    const vec2 frag = gl_FragCoord.xy - region_origin_px;

    if (frag.x < 0.0            || frag.y < 0.0 ||
        frag.x > plot_size_px.x || frag.y > plot_size_px.y)
    {
        discard;
    }

    const float v_mask = accumulate_lines(v_count, frag.y, v_spacing_px, v_start_px, v_alpha, v_thickness_px);
    const float t_mask = accumulate_lines(t_count, frag.x, t_spacing_px, t_start_px, t_alpha, t_thickness_px);
    const float mask = max(v_mask, t_mask);

    if (mask <= 0.001) {
        discard;
    }

    out_color = vec4(grid_color.rgb, grid_color.a * clamp(mask, 0.0, 1.0));
}
