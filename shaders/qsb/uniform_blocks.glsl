// Shared per-frame fields used by every series and chrome shader. Each
// shader declares its own outer Block { ... } u; UBO at binding 0 and
// includes this header to embed the common viewport state. The host fills
// the matching prefix of the C++ uniform struct from the active frame's
// projection matrix, viewport extents, time/value range, and y_offset
// (for the preview pane).

struct Series_view_t
{
    mat4  pmv;
    vec4  color;
    float t_min;
    float t_max;
    float v_min;
    float v_max;
    float width;
    float height;
    float y_offset;
    float win_h;
    int   framebuffer_y_up;
};
