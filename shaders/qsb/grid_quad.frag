#version 440

const int GRID_LEVEL_MAX = 32;

// Per-level grid parameters packed into one vec4 (.x = spacing_px,
// .y = start_px, .z = alpha, .w = thickness_px). std140 stores arrays
// of scalars at vec4 stride; packing four scalars per element keeps the
// UBO compact.
layout(std140, binding = 0) uniform Block
{
    vec2  plot_size_px;
    vec2  region_origin_px;
    vec4  grid_color;
    int   v_count;
    int   t_count;
    int   framebuffer_y_up;
    float win_h;
    vec4  v_levels[GRID_LEVEL_MAX];
    vec4  t_levels[GRID_LEVEL_MAX];
} u;

layout(location = 0) out vec4 out_color;

float line_mask(float coord, float spacing, float start, float thickness_px)
{
    if (spacing <= 0.0) {
        return 0.0;
    }
    float wrapped = mod(coord - start, spacing);
    float dist    = min(wrapped, spacing - wrapped);
    float half_t  = thickness_px * 0.1;
    return 1.0 - smoothstep(half_t, half_t + 1.0, dist);
}

float accumulate_lines(int count, float coord, vec4 levels[GRID_LEVEL_MAX])
{
    float mask = 0.0;
    for (int i = 0; i < count && i < GRID_LEVEL_MAX; ++i) {
        float contrib = line_mask(coord, levels[i].x, levels[i].y, levels[i].w);
        mask = max(mask, contrib * levels[i].z);
    }
    return mask;
}

void main()
{
    float frag_y = (u.framebuffer_y_up != 0)
        ? (u.win_h - gl_FragCoord.y)
        : gl_FragCoord.y;
    vec2 frag = vec2(gl_FragCoord.x, frag_y) - u.region_origin_px;

    if (frag.x < 0.0              || frag.y < 0.0 ||
        frag.x > u.plot_size_px.x || frag.y > u.plot_size_px.y)
    {
        discard;
    }

    float v_mask = accumulate_lines(u.v_count, frag.y, u.v_levels);
    float t_mask = accumulate_lines(u.t_count, frag.x, u.t_levels);
    float mask   = max(v_mask, t_mask);

    if (mask <= 0.001) {
        discard;
    }

    out_color = vec4(u.grid_color.rgb, u.grid_color.a * clamp(mask, 0.0, 1.0));
}
