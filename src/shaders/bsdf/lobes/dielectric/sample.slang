#ifndef VKRT_BSDF_DIELECTRIC_SAMPLE_SLANG
#define VKRT_BSDF_DIELECTRIC_SAMPLE_SLANG

#include "./interface.slang"
#include "./types.slang"

bool sampleDielectric(
    BSDFMaterial material,
    float3 wo,
    uint frontFace,
    GGXParams params,
    float lambdaNm,
    uint spectralMode,
    inout uint rng,
    out DielectricSample sample
) {
    sample = {};
    if (cosTheta(wo) <= 0.0) {
        return false;
    }

    if (dielectricIsIdentity(material)) {
        sample.wi = -wo;
        sample.isTransmission = material.transmission > 0.0 ? 1u : 0u;
        return material.transmission > 0.0;
    }

    float3 wm;
    if (!sampleGGXVNDF(wo, params, float2(rand(rng), rand(rng)), wm)) {
        return false;
    }

    float woDotWm = dot(wo, wm);
    if (woDotWm <= 0.0) {
        return false;
    }

    float fresnel = dielectricFresnel(material, frontFace, woDotWm, lambdaNm, spectralMode);
    float transmissionProbability = dielectricTransmissionProbability(material, fresnel);
    if (transmissionProbability > 0.0 && rand(rng) < transmissionProbability) {
        float eta = interfaceRefractionEta(material, frontFace, lambdaNm, spectralMode);
        float3 wi = refract(-wo, wm, eta);
        if (dot(wi, wi) > 0.0 && cosTheta(wi) < 0.0) {
            sample.wi = wi;
            sample.isTransmission = 1u;
            return true;
        }
    }

    sample.wi = 2.0 * woDotWm * wm - wo;
    sample.isTransmission = 0u;
    return cosTheta(sample.wi) > 0.0;
}

#endif
