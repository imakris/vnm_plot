#version 430
#extension GL_ARB_gpu_shader_int64 : require

// Vertex shader for function_sample_t (double x, float y, float y_min, float y_max)
layout(location = 0) in double in_x;
layout(location = 1) in float  in_y;
layout(location = 2) in float  in_y_min;
layout(location = 3) in float  in_y_max;

out Sample {
    double t;
    float  v;
    flat int status;
} vs_out;

void main(void) {
    vs_out.t = in_x;
    vs_out.v = in_y;
    vs_out.status = 0;
    gl_Position = vec4(0.0);
}
