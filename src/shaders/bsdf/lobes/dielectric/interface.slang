#ifndef VKRT_BSDF_DIELECTRIC_INTERFACE_SLANG
#define VKRT_BSDF_DIELECTRIC_INTERFACE_SLANG

#include "../ggx.slang"

// Rough dielectric reflection/transmission interface.
//
// References:
// Walter et al., 2007 - https://www.cs.cornell.edu/~srm/publications/EGSR07-btdf.pdf
// Burley, 2015 - https://blog.selfshadow.com/publications/s2015-shading-course/burley/s2015_pbs_disney_bsdf_notes.pdf
// Pharr, Jakob, Humphreys, 2023 - https://www.pbr-book.org/4ed/Reflection_Models/Dielectric_BSDF

bool dielectricIsIdentity(BSDFMaterial material) {
    return interfaceIor(material) <= 1.0 + 1e-4;
}

float dielectricFresnel(BSDFMaterial material, uint frontFace, float cosThetaI, float lambdaNm, uint spectralMode) {
    if (dielectricIsIdentity(material)) {
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
    if (dot(h, h) <= GGX_EPSILON) {
        return false;
    }

    wm = safeNormalize(h);
    if (wm.z < 0.0) {
        wm = -wm;
    }

    woDotWm = dot(wo, wm);
    wiDotWm = dot(wi, wm);
    denom = wiDotWm + woDotWm / etap;
    return woDotWm > 0.0 && wiDotWm < 0.0 && abs(denom) > GGX_EPSILON;
}

#endif
