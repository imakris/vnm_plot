#version 430

uniform vec4 color;
void main(void)
{
    // This seemingly pointless statement would force the evaluation
    // of the shader per sample, rather than per pixel.
    //gl_SampleID;

    vec2 xy = 2.0 * (gl_PointCoord - vec2(0.5));
    float r = dot(xy, xy);

    if (r<1) {
        gl_FragColor = color;
    }
    else {
        discard;
    }
};
