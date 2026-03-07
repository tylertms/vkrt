#ifndef SHADER_MIS_GLSL
#define SHADER_MIS_GLSL

float misPowerWeight(float pdfA, float pdfB) {
    float a2 = pdfA * pdfA;
    float b2 = pdfB * pdfB;
    float denom = a2 + b2;
    if (denom <= 1e-8) return 0.0;
    return a2 / denom;
}

#endif
