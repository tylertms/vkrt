#ifndef VKRT_BSDF_LAMBERTIAN_SLANG
#define VKRT_BSDF_LAMBERTIAN_SLANG

#include "../common.slang"

// Lambertian diffuse lobe:
// Perfectly matte, cosine-weighted distribution.
//
// References:
// Lambert diffuse BRDF: https://www.pbr-book.org/4ed/Reflection_Models/Diffuse_Reflection
// Cosine hemisphere sampling: https://www.pbr-book.org/4ed/Sampling_Algorithms/Sampling_Multidimensional_Functions

float pdfLambertianLocal(float3 wi) {
    return wi.z > 0.0 ? wi.z * VKRT_INV_PI : 0.0;
}

float3 sampleLambertianDirectionLocal(inout uint rng) {
    float u1 = rand(rng);
    float u2 = rand(rng);
    float r = sqrt(u1);
    float phi = 2.0 * VKRT_PI * u2;
    return float3(r * cos(phi), r * sin(phi), sqrt(max(0.0, 1.0 - u1)));
}

#endif
