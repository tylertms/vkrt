#ifndef VKRT_BSDF_CLEARCOAT_SLANG
#define VKRT_BSDF_CLEARCOAT_SLANG

#include "../base.slang"

// Disney clearcoat
//
// References:
// Disney clearcoat lobe, GTR1 distribution, gloss remap, and fixed masking term:
// Burley, 2012 - https://blog.selfshadow.com/publications/s2012-shading-course/burley/s2012_pbs_disney_brdf_notes_v3.pdf
// Microfacet reflection half-vector Jacobian and PDF conversion:
// Pharr, Jakob, Humphreys, 2023 - https://www.pbr-book.org/4ed/Reflection_Models/Roughness_Using_Microfacet_Theory

static const float CLEARCOAT_EPSILON = 1e-6;

struct ClearcoatParams {
    float alpha;
};

float clearcoatAlpha(Material material) {
    return lerp(0.1, 0.001, saturate(material.clearcoatGloss));
}

ClearcoatParams makeClearcoatParams(Material material) {
    ClearcoatParams params;
    params.alpha = max(clearcoatAlpha(material), 1e-3);
    return params;
}

float clearcoatSmithG1(float cosThetaValue, float alpha) {
    float alpha2 = alpha * alpha;
    float cos2 = max(cosThetaValue * cosThetaValue, CLEARCOAT_EPSILON);
    float tan2 = max(1.0 - cos2, 0.0) / cos2;
    return 2.0 / (1.0 + sqrt(1.0 + alpha2 * tan2));
}

float clearcoatDistribution(float cosThetaM, float alpha) {
    float alpha2 = alpha * alpha;
    if (alpha2 >= 1.0 - CLEARCOAT_EPSILON) {
        return VKRT_INV_PI;
    }

    float denom = VKRT_PI * log(alpha2) * (1.0 + (alpha2 - 1.0) * cosThetaM * cosThetaM);
    return (alpha2 - 1.0) / min(denom, -CLEARCOAT_EPSILON);
}

BSDFEval evalClearcoat(float clearcoatWeight, float3 wo, float3 wi, ClearcoatParams params) {
    BSDFEval eval;
    eval.value = float3(0.0);
    eval.pdf = 0.0;

    if (clearcoatWeight <= 0.0 || cosTheta(wo) <= 0.0 || cosTheta(wi) <= 0.0) {
        return eval;
    }

    float3 h = safeNormalize(wo + wi);
    float woDotH = saturate(dot(wo, h));
    if (woDotH <= 0.0) {
        return eval;
    }

    float D = clearcoatDistribution(saturate(cosTheta(h)), params.alpha);
    float F = lerp(0.04, 1.0, schlickWeight(woDotH));
    float G = clearcoatSmithG1(cosTheta(wo), 0.25) * clearcoatSmithG1(cosTheta(wi), 0.25);
    float value = 0.25 * clearcoatWeight * D * F * G / max(4.0 * cosTheta(wo) * cosTheta(wi), CLEARCOAT_EPSILON);
    float pdfM = D * saturate(cosTheta(h));

    eval.value = float3(value);
    eval.pdf = pdfM / max(4.0 * woDotH, CLEARCOAT_EPSILON);
    return eval;
}

bool sampleClearcoat(float3 wo, ClearcoatParams params, inout uint rng, out float3 wi) {
    float u1 = rand(rng);
    float u2 = rand(rng);
    float alpha2 = params.alpha * params.alpha;

    float cosThetaM;
    if (alpha2 >= 1.0 - CLEARCOAT_EPSILON) {
        cosThetaM = sqrt(max(0.0, 1.0 - u1));
    } else {
        float exponent = pow(alpha2, 1.0 - u1);
        cosThetaM = sqrt(saturate((1.0 - exponent) / max(1.0 - alpha2, CLEARCOAT_EPSILON)));
    }

    float sinThetaM = sqrt(saturate(1.0 - cosThetaM * cosThetaM));
    float phi = 2.0 * VKRT_PI * u2;
    float3 m = float3(sinThetaM * cos(phi), sinThetaM * sin(phi), cosThetaM);
    float woDotM = dot(wo, m);
    if (woDotM <= 0.0) {
        wi = float3(0.0);
        return false;
    }

    wi = 2.0 * woDotM * m - wo;
    return cosTheta(wi) > 0.0;
}

#endif
