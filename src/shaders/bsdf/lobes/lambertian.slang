#ifndef VKRT_BSDF_LAMBERTIAN_SLANG
#define VKRT_BSDF_LAMBERTIAN_SLANG

#include "bsdf/base.slang"

// Lambertian diffuse reflection.
//
// References:
// Pharr, Jakob, Humphreys, 2023 - https://www.pbr-book.org/4ed/Reflection_Models/Diffuse_Reflection

float lambertianPdf(float3 wi) {
    return cosTheta(wi) > 0.0 ? cosTheta(wi) * VKRT_INV_PI : 0.0;
}

BSDFEval evalLambertian(float3 diffuseColor, float3 wi) {
    BSDFEval eval;
    eval.value = cosTheta(wi) > 0.0 ? diffuseColor * VKRT_INV_PI : float3(0.0);
    eval.pdf = lambertianPdf(wi);
    return eval;
}

float3 sampleLambertian(inout uint rng) {
    float u1 = rand(rng);
    float u2 = rand(rng);
    float r = sqrt(u1);
    float phi = 2.0 * VKRT_PI * u2;
    return float3(r * cos(phi), r * sin(phi), sqrt(max(0.0, 1.0 - u1)));
}

#endif
