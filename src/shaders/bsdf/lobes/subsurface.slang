#ifndef VKRT_BSDF_SUBSURFACE_SLANG
#define VKRT_BSDF_SUBSURFACE_SLANG

#include "./diffuse.slang"

BSDFEval evalFakeSubsurface(float3 diffuseColor, float roughness, float3 wo, float3 wi) {
    BSDFEval eval;
    eval.value = float3(0.0);
    eval.pdf = 0.0;

    if (cosTheta(wo) <= 0.0 || cosTheta(wi) <= 0.0) {
        return eval;
    }

    float3 h = normalize(wo + wi);
    float wiDotH = saturate(dot(wi, h));
    float fss90 = wiDotH * wiDotH * roughness;
    float fssIn = lerp(1.0, fss90, schlickWeight(cosTheta(wi)));
    float fssOut = lerp(1.0, fss90, schlickWeight(cosTheta(wo)));
    float fss = fssIn * fssOut;
    float ss = 1.25 * (fss * (1.0 / (cosTheta(wi) + cosTheta(wo)) - 0.5) + 0.5);

    eval.value = diffuseColor * (VKRT_INV_PI * ss);
    eval.pdf = cosineHemispherePdf(wi);
    return eval;
}

#endif
