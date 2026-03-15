#ifndef VKRT_BSDF_SHEEN_SLANG
#define VKRT_BSDF_SHEEN_SLANG

#include "../common.slang"

// Sheen using the Estevez-Kulla sheen NDF and the Neubelt-Pettineo cloth visibility term.
// Roughness-controllable cloth/sheen lobe
//
// References:
// Sheen NDF: Estevez, Kulla, 2017 - https://blog.selfshadow.com/publications/s2017-shading-course/imageworks/s2017_pbs_imageworks_sheen.pdf
// Cloth visibility term: Neubelt, Pettineo, 2013 - https://blog.selfshadow.com/publications/s2013-shading-course/rad/s2013_pbs_rad_notes.pdf

static const float SHEEN_EPSILON = 1e-6;

float charlieDistribution(float cosTheta, float roughness) {
    float invR = 1.0 / max(roughness, SHEEN_EPSILON);
    float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
    return (2.0 + invR) * pow(max(sinTheta, SHEEN_EPSILON), invR) * (0.5 * VKRT_INV_PI);
}

float neubeltVisibility(float cosI, float cosO) {
    return 1.0 / max(4.0 * (cosI + cosO - cosI * cosO), SHEEN_EPSILON);
}

float sheenHalfVectorPdf(float cosThetaH, float roughness) {
    return charlieDistribution(cosThetaH, roughness) * cosThetaH;
}

BSDFEval evalSheen(float3 sheenColor, float sheenRoughness, float3 wo, float3 wi) {
    BSDFEval eval;
    eval.value = float3(0.0);
    eval.pdf = 0.0;

    float cosO = cosTheta(wo);
    float cosI = cosTheta(wi);
    if (cosO <= 0.0 || cosI <= 0.0) {
        return eval;
    }

    float3 h = safeNormalize(wo + wi);
    float cosThetaH = saturate(cosTheta(h));
    float woDotH = saturate(dot(wo, h));
    if (woDotH <= 0.0) {
        return eval;
    }

    float D = charlieDistribution(cosThetaH, sheenRoughness);
    float V = neubeltVisibility(cosI, cosO);
    eval.value = sheenColor * (D * V);
    eval.pdf = sheenHalfVectorPdf(cosThetaH, sheenRoughness) / max(4.0 * woDotH, SHEEN_EPSILON);
    return eval;
}

bool sampleSheen(float3 wo, float sheenRoughness, inout uint rng, out float3 wi) {
    float u1 = rand(rng);
    float u2 = rand(rng);

    float r = max(sheenRoughness, SHEEN_EPSILON);
    float sinThetaH = pow(u1, r / (1.0 + 2.0 * r));
    float cosThetaH = sqrt(max(1.0 - sinThetaH * sinThetaH, 0.0));
    float phi = 2.0 * VKRT_PI * u2;

    float3 h = float3(sinThetaH * cos(phi), sinThetaH * sin(phi), cosThetaH);
    float woDotH = dot(wo, h);
    if (woDotH <= 0.0) {
        wi = float3(0.0);
        return false;
    }

    wi = 2.0 * woDotH * h - wo;
    return cosTheta(wi) > 0.0;
}

#endif
