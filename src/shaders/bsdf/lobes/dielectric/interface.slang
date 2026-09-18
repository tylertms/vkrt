#ifndef VKRT_BSDF_DIELECTRIC_INTERFACE_SLANG
#define VKRT_BSDF_DIELECTRIC_INTERFACE_SLANG

#include "../ggx.slang"

bool dielectricIsIdentity(BSDFMaterial material) {
    return interfaceIor(material) == 1.0;
}

bool dielectricIsIdentity(BSDFMaterial material, float lambdaNm, uint spectralMode) {
    return interfaceIor(material, lambdaNm, spectralMode) == 1.0;
}

float dielectricFresnel(BSDFMaterial material, uint frontFace, float cosThetaI, float lambdaNm, uint spectralMode) {
    if (dielectricIsIdentity(material, lambdaNm, spectralMode)) {
        return 0.0;
    }

    return fresnelDielectric(cosThetaI, interfaceEta(material, frontFace, lambdaNm, spectralMode));
}

float4 dielectricFresnel4(BSDFMaterial material, uint frontFace, float cosThetaI, float4 wavelengthsNm) {
    if (dielectricIsIdentity(material)) {
        return float4(0.0);
    }

    return float4(
        fresnelDielectric(cosThetaI, interfaceEta(material, frontFace, wavelengthsNm.x, 1u)),
        fresnelDielectric(cosThetaI, interfaceEta(material, frontFace, wavelengthsNm.y, 1u)),
        fresnelDielectric(cosThetaI, interfaceEta(material, frontFace, wavelengthsNm.z, 1u)),
        fresnelDielectric(cosThetaI, interfaceEta(material, frontFace, wavelengthsNm.w, 1u))
    );
}

float3 dielectricReflectionColor(
    BSDFMaterial material,
    float cosThetaI,
    float fresnel,
    float lambdaNm,
    uint spectralMode
) {
    if (material.transmission > 0.0 && material.metallic <= 0.0) {
        return fresnel.xxx;
    }

    float3 reflectionColor = fresnelSchlick(cosThetaI, bsdfDielectricSpecularF0(material));
    return spectralMode != 0u ? spectralScalarFromLinearSrgb(saturate(reflectionColor), lambdaNm).xxx : reflectionColor;
}

float4 dielectricReflectionColor4(BSDFMaterial material, float cosThetaI, float4 fresnel, float4 wavelengthsNm) {
    if (material.transmission > 0.0 && material.metallic <= 0.0) {
        return fresnel;
    }

    float3 reflectionColor = fresnelSchlick(cosThetaI, bsdfDielectricSpecularF0(material));
    return spectralScalarFromLinearSrgb4(saturate(reflectionColor), wavelengthsNm);
}

float dielectricTransmissionProbability(BSDFMaterial material, float fresnel) {
    float transmissionWeight = material.transmission * (1.0 - fresnel);
    float totalWeight = fresnel + transmissionWeight;
    return totalWeight <= 0.0 ? 0.0 : transmissionWeight / totalWeight;
}

bool dielectricTransmissionHalfVector(
    float3 wo,
    float3 wi,
    float etap,
    out float3 wm,
    out float woDotWm,
    out float wiDotWm,
    out float denom
) {
    wm = float3(0.0);
    woDotWm = 0.0;
    wiDotWm = 0.0;
    denom = 0.0;

    if (cosTheta(wo) <= 0.0 || cosTheta(wi) >= 0.0) {
        return false;
    }

    float3 h = wo + wi * etap;
    float h2 = dot(h, h);
    if (h2 <= 0.0) {
        return false;
    }

    wm = h * rsqrt(h2);
    if (wm.z < 0.0) {
        wm = -wm;
    }

    woDotWm = dot(wo, wm);
    wiDotWm = dot(wi, wm);
    denom = wiDotWm + woDotWm / etap;
    return woDotWm > 0.0 && wiDotWm < 0.0 && denom != 0.0;
}

#endif
