#ifndef SHADER_BSDF_GLSL
#define SHADER_BSDF_GLSL

#include "core/types.glsl"
#include "core/math.glsl"
#include "utility/rand.glsl"

struct BSDFSample {
    vec3 incoming;
    vec3 f;
    float pdf;
};

vec3 sampleCosineHemisphere(vec3 normal, inout uint state) {
    float r1 = rand(state);
    float r2 = rand(state);
    float r = sqrt(r1);
    float phi = TWO_PI * r2;

    vec3 local = vec3(r * cos(phi), r * sin(phi), sqrt(max(1.0 - r1, 0.0)));
    return buildTBN(normal) * local;
}

float cosineHemispherePdf(vec3 normal, vec3 incoming) {
    return max(dot(normal, incoming), 0.0) * INV_PI;
}

vec3 evalBSDF(vec3 normal, vec3 incoming, vec3 outgoing, Material material) {
    float cosNL = max(dot(normal, incoming), 0.0);
    if (cosNL <= 0.0 || dot(normal, outgoing) <= 0.0) return vec3(0.0);

    vec3 diffuseColor = material.baseColor * (1.0 - material.metallic);
    return diffuseColor * INV_PI;
}

float pdfBSDF(vec3 normal, vec3 incoming, vec3 outgoing, Material material) {
    return cosineHemispherePdf(normal, incoming);
}

BSDFSample sampleBSDF(vec3 normal, vec3 outgoing, Material material, inout uint state) {
    BSDFSample result;
    result.incoming = sampleCosineHemisphere(normal, state);
    result.f = evalBSDF(normal, result.incoming, outgoing, material);
    result.pdf = pdfBSDF(normal, result.incoming, outgoing, material);
    return result;
}

#endif
