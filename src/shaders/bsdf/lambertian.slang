#ifndef VKRT_BSDF_LAMBERTIAN_SLANG
#define VKRT_BSDF_LAMBERTIAN_SLANG

#include "common.slang"
#include "../utility/random.slang"
#include "../utility/ray.slang"

// https://pema.dev/obsidian/math/light-transport/cosine-weighted-sampling.html
// https://pbr-book.org/4ed/Reflection_Models/Diffuse_Reflection

float3 sampleCosineHemisphere(inout uint state) {
    float u1 = rand(state);
    float u2 = rand(state);
    float r = sqrt(u1);
    float phi = 2.0 * VKRT_PI * u2;

    float x = r * cos(phi);
    float y = r * sin(phi);
    float z = sqrt(max(0.0, 1.0 - u1));

    return float3(x, y, z);
}

float3 evalLambertian(BSDFInput input, float3 wi) {
    float cosTheta = max(0.0, dot(input.normal, wi));
    if (cosTheta <= 0.0) return float3(0.0);
    return input.material.baseColor * VKRT_INV_PI;
}

float pdfLambertian(BSDFInput input, float3 wi) {
    float cosTheta = max(0.0, dot(input.normal, wi));
    return cosTheta * VKRT_INV_PI;
}

BSDFSample sampleLambertian(BSDFInput input, inout uint rng) {
    BSDFSample sample = BSDFSample();

    sample.wi = localToWorld(sampleCosineHemisphere(rng), input.normal);
    sample.value = evalLambertian(input, sample.wi);
    sample.pdf = pdfLambertian(input, sample.wi);
    sample.weight = input.material.baseColor;

    return sample;
}

#endif
