#ifndef VKRT_BSDF_OREN_NAYAR_SLANG
#define VKRT_BSDF_OREN_NAYAR_SLANG

#include "../base.slang"

// Oren-Nayar rough diffuse reflection.
//
// References:
// Oren, Nayar, 1994 - http://www1.cs.columbia.edu/CAVE/publications/pdfs/Oren_SIGGRAPH94.pdf
// Implementation summary: Pharr, Jakob, Humphreys, 2023 - https://www.pbr-book.org/4ed/Reflection_Models/Diffuse_Reflection

float orenNayarPdf(float3 wi) {
    return cosTheta(wi) > 0.0 ? cosTheta(wi) * VKRT_INV_PI : 0.0;
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
        tanBeta = sinThetaI / max(absCosTheta(wi), 1e-6);
    } else {
        sinAlpha = sinThetaI;
        tanBeta = sinThetaO / max(absCosTheta(wo), 1e-6);
    }

    eval.value = diffuseColor * (VKRT_INV_PI * (A + B * maxCos * sinAlpha * tanBeta));
    eval.pdf = orenNayarPdf(wi);
    return eval;
}

float3 sampleOrenNayar(inout uint rng) {
    float u1 = rand(rng);
    float u2 = rand(rng);
    float r = sqrt(u1);
    float phi = 2.0 * VKRT_PI * u2;
    return float3(r * cos(phi), r * sin(phi), sqrt(max(0.0, 1.0 - u1)));
}

#endif
