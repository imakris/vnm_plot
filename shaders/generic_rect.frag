#version 430

in  vec4 gs_col;
out vec4 color;

void main(void)
{
    //color = gs_col.a * gs_col + (1-gs_col.a) * color;
    //

    color = gs_col;

    //color = vec4(0.0, 0.0, 0.0, 0.5);
};
