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
    if (dielectricIsIdentity(material, lambdaNm, spectralMode) || cosTheta(wo) <= 0.0 || cosTheta(wi) <= 0.0) {
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
                 (Dm * G / (4.0 * cosTheta(wo) * cosTheta(wi)));
    eval.pdf = reflectionProbability * ggxReflectionPdf(wo, wm, params);
    return eval;
}

BSDFEval evalDielectricTransmissionInterface(
    BSDFMaterial material,
    float3 wo,
    float3 wi,
    uint frontFace,
    GGXParams params,
    float lambdaNm,
    uint spectralMode
) {
    BSDFEval eval = {};
    if (material.transmission <= 0.0 || dielectricIsIdentity(material, lambdaNm, spectralMode) || cosTheta(wo) <= 0.0 ||
        cosTheta(wi) >= 0.0) {
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
    float transportScale = 1.0 / (etap * etap);
    float dwmDwi = abs(wiDotWm) / denom2;
    float transmissionTerm = dwmDwi * woDotWm / (absCosTheta(wi) * cosTheta(wo));

    eval.value = ((1.0 - fresnel) * Dm * G * transmissionTerm * transportScale).xxx;
    eval.pdf = transmissionProbability * ggxVisibleNormalPdf(wo, wm, params) * dwmDwi;
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
    BSDFEval eval = evalDielectricTransmissionInterface(material, wo, wi, frontFace, params, lambdaNm, spectralMode);
    float3 color = bsdfTransmissionColor(material);
    if (spectralMode != 0u) color = spectralScalarFromLinearSrgb(saturate(color), lambdaNm).xxx;
    eval.value *= color * ((1.0 - material.metallic) * material.transmission * coatAttenuation);
    return eval;
}

#endif
