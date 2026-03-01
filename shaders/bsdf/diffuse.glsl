#ifndef SHADER_BSDF_DIFFUSE_GLSL
#define SHADER_BSDF_DIFFUSE_GLSL

#include "core/math.glsl"
#include "utility/rand.glsl"

vec3 sampleDiffuse(vec3 normal, inout uint state) {
    return normalize(normal + randDir(state));
}

vec3 evalDiffuse(vec3 baseColor) {
    return baseColor * INV_PI;
}

float pdfDiffuse(vec3 normal, vec3 incoming) {
    return max(dot(normal, incoming), 0.0) * INV_PI;
}

#endif
