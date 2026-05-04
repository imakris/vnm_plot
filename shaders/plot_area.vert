#version 430
#extension GL_ARB_gpu_shader_int64 : require

// One instance = one segment (samples p0 -> p1) of an area fill.  Each
// instance produces a 13-vertex triangle strip:
//
//   vid  0..5  : fill triangles. Same-sign case lays out a trapezoidal
//                quad (v0..v3) with v4=v5=v3 as collapsed tail vertices;
//                sign-flip case emits two half-triangles meeting on the
//                zero axis at x_mid (T0=(v0,v1,v2), T3=(v3,v4,v5)).
//   vid  6     : duplicate of v5 to start the degenerate run.
//   vid  7..9  : duplicates of axis_start; the jump from v6 to v7 is
//                absorbed because v5==v6 and v7==v8==v9.
//   vid 10..12 : remainder of the zero-axis emphasis quad
//                (top-right, bottom-left, bottom-right).
//
// Triangles produced between vid 5 and vid 9 are all degenerate, so the
// strip cleanly bridges the fill and the axis bar.

layout(std430, binding = 0) readonly buffer Sample_buffer
{
    uint raw[];
} u_samples;

layout(std430, binding = 1) readonly buffer Adjacency_index_buffer
{
    uint indices[];
} u_adjacency;

layout(location =  0) uniform mat4    pmv;
layout(location =  1) uniform double  t_min;
layout(location =  2) uniform double  t_max;
layout(location =  3) uniform float   v_min;
layout(location =  4) uniform float   v_max;
layout(location =  5) uniform double  width;
layout(location =  6) uniform double  height;
layout(location =  7) uniform float   y_offset;
layout(location =  8) uniform vec4    color;

uniform vec4 zero_axis_color;

out GS_out
{
    vec4  color;
    float t;
} gs_out;

const uint k_sample_uint_stride = 5u;

double sample_x(uint idx)
{
    uint base = idx * k_sample_uint_stride;
    return packDouble2x32(uvec2(u_samples.raw[base], u_samples.raw[base + 1u]));
}

float sample_y(uint idx)
{
    return uintBitsToFloat(u_samples.raw[idx * k_sample_uint_stride + 2u]);
}

void main()
{
    uint seg = uint(gl_InstanceID);
    // Adjacency layout matches the line-adjacency EBO: per segment we
    // consume the centre two indices.
    uint i_p0 = u_adjacency.indices[seg + 1u];
    uint i_p1 = u_adjacency.indices[seg + 2u];

    double rt = max(t_max - t_min, 1e-30lf);
    float  rv = max(v_max - v_min, 1e-30);

    float x0 = float(width * (sample_x(i_p0) - t_min) / rt);
    float x1 = float(width * (sample_x(i_p1) - t_min) / rt);

    float color_denom = max(abs(v_min), abs(v_max));

    vec4 axis_color0 = color;
    vec4 axis_color1 = color;
    axis_color0.w = axis_color1.w = 0.57 * color.w;

    float cv0 = sample_y(i_p0);
    float cv1 = sample_y(i_p1);

    vec4 v0_color = color; v0_color.w = axis_color0.w + 0.3 * abs(cv0) / color_denom;
    vec4 v1_color = color; v1_color.w = axis_color1.w + 0.3 * abs(cv1) / color_denom;

    if (cv0 < 0) { v0_color = v0_color.zyxw; axis_color0 = axis_color0.zyxw; }
    if (cv1 < 0) { v1_color = v1_color.zyxw; axis_color1 = axis_color1.zyxw; }

    float y0     = float(height * (1.0lf - (double(cv0) - double(v_min)) / double(rv))) + y_offset;
    float y1     = float(height * (1.0lf - (double(cv1) - double(v_min)) / double(rv))) + y_offset;
    float y_axis = float(height * (1.0lf - (0.0lf - double(v_min)) / double(rv))) + y_offset;

    bool  sign_flip = (cv0 * cv1) < 0.0;
    float mid       = (cv0 - cv1) != 0.0 ? cv0 / (cv0 - cv1) : 0.0;
    double t_mid    = sample_x(i_p0) + (sample_x(i_p1) - sample_x(i_p0)) * double(mid);
    float x_mid     = float(width * (t_mid - t_min) / rt);
    float am        = abs(mid);

    // Axis emphasis offset: matches the original geometry shader, which
    // inverts the band when the pass is rendering a preview row.
    float a2 = (y_offset > 0.0) ? 0.2 : 0.8;

    vec2 axis_start = vec2(x0, y_axis + a2);
    vec4 axis_color = zero_axis_color;

    // Both same-sign and sign-flip strips end the fill at the (x1, y_axis)
    // vertex. We expose it explicitly so the connector can duplicate it.
    vec2  v5_pos   = vec2(x1, y_axis);
    vec4  v5_color = axis_color1;
    float v5_t     = 1.0;

    int  vid = gl_VertexID;
    vec2 pos;
    vec4 vcolor;
    float vt = 0.0;

    if (vid <= 5) {
        if (sign_flip) {
            switch (vid) {
                case 0:  pos = vec2(x0,    y0);     vcolor = v0_color;    vt = 0.0; break;
                case 1:  pos = vec2(x0,    y_axis); vcolor = axis_color0; vt = 0.0; break;
                case 2:  pos = vec2(x_mid, y_axis); vcolor = axis_color0; vt = am;  break;
                case 3:  pos = vec2(x_mid, y_axis); vcolor = axis_color1; vt = am;  break;
                case 4:  pos = vec2(x1,    y1);     vcolor = v1_color;    vt = 1.0; break;
                default: pos = v5_pos;              vcolor = v5_color;    vt = v5_t; break;
            }
        }
        else {
            switch (vid) {
                case 0:  pos = vec2(x0, y0);     vcolor = v0_color;    vt = 0.0; break;
                case 1:  pos = vec2(x1, y1);     vcolor = v1_color;    vt = 1.0; break;
                case 2:  pos = vec2(x0, y_axis); vcolor = axis_color0; vt = 0.0; break;
                case 3:  pos = vec2(x1, y_axis); vcolor = axis_color1; vt = 1.0; break;
                case 4:  pos = vec2(x1, y_axis); vcolor = axis_color1; vt = 1.0; break;
                default: pos = v5_pos;           vcolor = v5_color;    vt = v5_t; break;
            }
        }
    }
    else
    if (vid == 6) {
        // Duplicate of v5: closes the fill strip degenerately.
        pos    = v5_pos;
        vcolor = v5_color;
        vt     = v5_t;
    }
    else
    if (vid <= 9) {
        // Three duplicates of axis_start. These form degenerate connector
        // triangles between the fill quad and the axis emphasis quad.
        pos    = axis_start;
        vcolor = axis_color;
        vt     = 0.0;
    }
    else {
        // vid 10..12: remaining vertices of the axis emphasis quad.
        // Strip already starts at axis_start (= vid 9), so we only need
        // top-right, bottom-left, bottom-right here.
        switch (vid) {
            case 10: pos = vec2(x1, y_axis + a2); vcolor = axis_color; vt = 1.0; break;
            case 11: pos = vec2(x0, y_axis - a2); vcolor = axis_color; vt = 0.0; break;
            default: pos = vec2(x1, y_axis - a2); vcolor = axis_color; vt = 1.0; break;
        }
    }

    gs_out.color = vcolor;
    gs_out.t     = vt;
    gl_Position  = pmv * vec4(pos, 0.0, 1.0);
}
