#ifndef VKRT_BSDF_DIFFUSE_SLANG
#define VKRT_BSDF_DIFFUSE_SLANG

#include "../base.slang"

BSDFEval evalLambertian(float3 diffuseColor, float3 wi) {
    BSDFEval eval;
    eval.value = cosTheta(wi) > 0.0 ? diffuseColor * VKRT_INV_PI : float3(0.0);
    eval.pdf = cosineHemispherePdf(wi);
    return eval;
}

BSDFEval evalOrenNayar(float3 diffuseColor, float roughness, float3 wo, float3 wi) {
    BSDFEval eval;
    eval.value = float3(0.0);
    eval.pdf = 0.0;

    if (cosTheta(wo) <= 0.0 || cosTheta(wi) <= 0.0) {
        return eval;
    }

    float sigma = saturate(roughness) * (0.5 * VKRT_PI);
    float sigma2 = sigma * sigma;
    float A = 1.0 - sigma2 / (2.0 * (sigma2 + 0.33));
    float B = 0.45 * sigma2 / (sigma2 + 0.09);

    float sin2ThetaI = sin2Theta(wi);
    float sin2ThetaO = sin2Theta(wo);
    float sinThetaI = sqrt(sin2ThetaI);
    float sinThetaO = sqrt(sin2ThetaO);
    float maxCos = max(0.0, cosPhiDifference(wi, wo));

    float sinAlpha;
    float tanBeta;
    if (absCosTheta(wi) > absCosTheta(wo)) {
        sinAlpha = sinThetaO;
        tanBeta = sinThetaI / absCosTheta(wi);
    } else {
        sinAlpha = sinThetaI;
        tanBeta = sinThetaO / absCosTheta(wo);
    }

    eval.value = diffuseColor * (VKRT_INV_PI * (A + B * maxCos * sinAlpha * tanBeta));
    eval.pdf = cosineHemispherePdf(wi);
    return eval;
}

#endif
