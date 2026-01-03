#version 430

uniform vec4 color;

out vec4 frag_color;

void main(void)
{

    // This seemingly pointless statement would force the evaluation
    // of the shader per sample, rather than per pixel.
    //gl_SampleID;

    frag_color = color;
}
