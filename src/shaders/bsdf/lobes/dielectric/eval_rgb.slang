#ifndef VKRT_BSDF_DIELECTRIC_EVAL_RGB_SLANG
#define VKRT_BSDF_DIELECTRIC_EVAL_RGB_SLANG

#include "./interface.slang"

BSDFEval evalDielectricReflection(
    BSDFMaterial material,
    float3 wo,
    float3 wi,
    uint frontFace,
    GGXParams params,
    float lambdaNm,
    uint spectralMode
) {
    BSDFEval eval = {};
    if (dielectricIsIdentity(material) || cosTheta(wo) <= 0.0 || cosTheta(wi) <= 0.0) {
        return eval;
    }

    float3 wm;
    float woDotWm;
    if (!ggxHalfVector(wo, wi, wm, woDotWm)) {
        return eval;
    }

    float fresnel = dielectricFresnel(material, frontFace, woDotWm, lambdaNm, spectralMode);
    float reflectionProbability = 1.0 - dielectricTransmissionProbability(material, fresnel);
    if (reflectionProbability <= 0.0) {
        return eval;
    }

    float Dm = ggxDistribution(wm, params.alpha);
    float G = ggxMasking(wo, wi, params.alpha);
    eval.value = dielectricReflectionColor(material, woDotWm, fresnel, lambdaNm, spectralMode) *
                 (Dm * G / max(4.0 * cosTheta(wo) * cosTheta(wi), GGX_EPSILON));
    eval.pdf = reflectionProbability * ggxReflectionPdf(wo, wm, params);
    return eval;
}

BSDFEval evalDielectricTransmission(
    BSDFMaterial material,
    float3 wo,
    float3 wi,
    uint frontFace,
    GGXParams params,
    float coatAttenuation,
    float lambdaNm,
    uint spectralMode
) {
    BSDFEval eval = {};
    if (material.transmission <= 0.0 || cosTheta(wo) <= 0.0 || cosTheta(wi) >= 0.0) {
        return eval;
    }

    float3 transmissionColor = bsdfTransmissionColor(material) * ((1.0 - material.metallic) * material.transmission);
    if (spectralMode != 0u) {
        transmissionColor = spectralScalarFromLinearSrgb(saturate(transmissionColor), lambdaNm).xxx;
    }

    if (dielectricIsIdentity(material)) {
        if (dot(wi, -wo) < 0.9999) {
            return eval;
        }

        eval.value = coatAttenuation * transmissionColor / max(absCosTheta(wi), GGX_EPSILON);
        eval.pdf = 1.0;
        return eval;
    }

    float etap = interfaceEta(material, frontFace, lambdaNm, spectralMode);
    float3 wm;
    float woDotWm;
    float wiDotWm;
    float denom;
    if (!dielectricTransmissionHalfVector(wo, wi, etap, wm, woDotWm, wiDotWm, denom)) {
        return eval;
    }

    float fresnel = dielectricFresnel(material, frontFace, woDotWm, lambdaNm, spectralMode);
    float transmissionProbability = dielectricTransmissionProbability(material, fresnel);
    if (transmissionProbability <= 0.0) {
        return eval;
    }

    float Dm = ggxDistribution(wm, params.alpha);
    float G = ggxMasking(wo, wi, params.alpha);
    float denom2 = denom * denom;
    float transportScale = 1.0 / max(etap * etap, GGX_EPSILON);
    float transmissionTerm = abs((wiDotWm * woDotWm) / max(absCosTheta(wi) * cosTheta(wo) * denom2, GGX_EPSILON));
    float dwmDwi = abs(wiDotWm) / max(denom2, GGX_EPSILON);

    eval.value = coatAttenuation * transmissionColor * ((1.0 - fresnel) * Dm * G * transmissionTerm * transportScale);
    eval.pdf = transmissionProbability * ggxVisibleNormalPdf(wo, wm, params) * dwmDwi;
    return eval;
}

#endif
