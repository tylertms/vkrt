#ifndef VKRT_BSDF_LAMBERTIAN_SLANG
#define VKRT_BSDF_LAMBERTIAN_SLANG

#include "common.slang"
#include "../utility/random.slang"

// https://pema.dev/obsidian/math/light-transport/cosine-weighted-sampling.html
// https://pbr-book.org/4ed/Reflection_Models/Diffuse_Reflection

float3 sampleCosineHemisphere(inout uint state) {
    float u1 = rand(state);
    float u2 = rand(state);
    float r = sqrt(u1);
    float phi = 2.0 * VKRT_PI * u2;
    return float3(r * cos(phi), r * sin(phi), sqrt(max(0.0, 1.0 - u1)));
}

float3 evalLambertian(BSDFInput input, float3 wi) {
    float cosTheta = dot(input.normal, wi);
    if (cosTheta <= 0.0) return float3(0.0);
    return input.material.baseColor * VKRT_INV_PI;
}

float pdfLambertian(BSDFInput input, float3 wi) {
    float cosTheta = dot(input.normal, wi);
    return cosTheta > 0.0 ? cosTheta * VKRT_INV_PI : 0.0;
}

BSDFSample sampleLambertian(BSDFInput input, inout uint state) {
    BSDFSample sample = BSDFSample();
    float3 localWi = sampleCosineHemisphere(state);
    sample.wi = localToWorld(localWi, input.normal);
    sample.value = evalLambertian(input, sample.wi);
    sample.pdf = pdfLambertian(input, sample.wi);

    float cosTheta = saturate(dot(input.normal, sample.wi));
    sample.weight = sample.pdf > 0.0 ? sample.value * cosTheta / sample.pdf : float3(0.0);
    return sample;
}

#endif
