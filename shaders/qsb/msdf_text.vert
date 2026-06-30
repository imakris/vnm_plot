#version 440

layout(std140, binding = 0) uniform Block
{
    mat4  pmv;
    vec4  color;
    vec4  shadow_color;
    float px_range;
    float target_width;
    float target_height;
    float shadow_radius;
    float lcd_subpixel_order;
    int   framebuffer_y_up;
    float padding_0;
    float padding_1;
    vec4  background_color;
} u;

layout(location = 0) in vec2 vertex;
layout(location = 1) in vec4 tex_bounds;
layout(location = 2) in vec4 frame_rect;

layout(location = 0) smooth out vec4 vs_tex_bounds;
layout(location = 1) smooth out vec4 vs_frame_rect;

void main()
{
    vs_tex_bounds = tex_bounds;
    vs_frame_rect = frame_rect;
    gl_Position = u.pmv * vec4(vertex, 0.1, 1.0);
}
