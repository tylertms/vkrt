#ifndef VKRT_BSDF_DIELECTRIC_SLANG
#define VKRT_BSDF_DIELECTRIC_SLANG

#include "ggx.slang"

struct DielectricSample {
    float3 wi;
    uint isTransmission;
};

// Rough dielectric reflection/transmission interface.
//
// References:
// Walter et al., 2007 - https://www.cs.cornell.edu/~srm/publications/EGSR07-btdf.pdf
// Burley, 2015 - https://blog.selfshadow.com/publications/s2015-shading-course/burley/s2015_pbs_disney_bsdf_notes.pdf
// Pharr, Jakob, Humphreys, 2023 - https://www.pbr-book.org/4ed/Reflection_Models/Dielectric_BSDF

bool dielectricIsIdentity(Material material) {
    return interfaceIor(material) <= 1.0 + 1e-4;
}

float dielectricFresnel(Material material, uint frontFace, float cosThetaI, float lambdaNm, uint spectralMode) {
    if (dielectricIsIdentity(material)) {
        return 0.0;
    }

    return fresnelDielectric(cosThetaI, interfaceEta(material, frontFace, lambdaNm, spectralMode));
}

float4 dielectricFresnel4(Material material, uint frontFace, float cosThetaI, float4 wavelengthsNm) {
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

float3 dielectricReflectionColor(Material material, float cosThetaI, float fresnel, float lambdaNm, uint spectralMode) {
    if (material.transmission > 0.0 && material.metallic <= 0.0) {
        return fresnel.xxx;
    }

    float3 reflectionColor = fresnelSchlick(cosThetaI, bsdfDielectricSpecularF0(material));
    return spectralMode != 0u ? spectralScalarFromLinearSrgb(saturate(reflectionColor), lambdaNm).xxx : reflectionColor;
}

float4 dielectricReflectionColor4(Material material, float cosThetaI, float4 fresnel, float4 wavelengthsNm) {
    if (material.transmission > 0.0 && material.metallic <= 0.0) {
        return fresnel;
    }

    float3 reflectionColor = fresnelSchlick(cosThetaI, bsdfDielectricSpecularF0(material));
    return spectralScalarFromLinearSrgb4(saturate(reflectionColor), wavelengthsNm);
}

float dielectricTransmissionProbability(Material material, float fresnel) {
    float transmissionWeight = material.transmission * (1.0 - fresnel);
    float totalWeight = fresnel + transmissionWeight;
    if (totalWeight <= 0.0) {
        return 0.0;
    }

    return transmissionWeight / totalWeight;
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

BSDFEval evalDielectricReflection(
    Material material,
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
    Material material,
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

float4 evalSpectralDielectricReflection(
    Material material,
    float3 wo,
    float3 wi,
    uint frontFace,
    GGXParams params,
    float4 wavelengthsNm,
    out float4 techniquePdf
) {
    techniquePdf = float4(0.0);
    if (dielectricIsIdentity(material) || cosTheta(wo) <= 0.0 || cosTheta(wi) <= 0.0) {
        return float4(0.0);
    }

    float3 wm;
    float woDotWm;
    if (!ggxHalfVector(wo, wi, wm, woDotWm)) {
        return float4(0.0);
    }

    float4 fresnel = dielectricFresnel4(material, frontFace, woDotWm, wavelengthsNm);
    float Dm = ggxDistribution(wm, params.alpha);
    float G = ggxMasking(wo, wi, params.alpha);
    float reflectionBasePdf = ggxReflectionPdf(wo, wm, params);
    techniquePdf = float4(
                       1.0 - dielectricTransmissionProbability(material, fresnel.x),
                       1.0 - dielectricTransmissionProbability(material, fresnel.y),
                       1.0 - dielectricTransmissionProbability(material, fresnel.z),
                       1.0 - dielectricTransmissionProbability(material, fresnel.w)
                   ) *
                   reflectionBasePdf;
    if (!any(techniquePdf > 0.0)) {
        return float4(0.0);
    }

    return dielectricReflectionColor4(material, woDotWm, fresnel, wavelengthsNm) *
           (Dm * G / max(4.0 * cosTheta(wo) * cosTheta(wi), GGX_EPSILON));
}

float evalSpectralDielectricTransmissionLane(
    Material material,
    float3 wo,
    float3 wi,
    uint frontFace,
    GGXParams params,
    float coatAttenuation,
    float transmissionColor,
    float lambdaNm,
    out float pdf
) {
    pdf = 0.0;
    if (material.transmission <= 0.0 || cosTheta(wo) <= 0.0 || cosTheta(wi) >= 0.0) {
        return 0.0;
    }

    if (dielectricIsIdentity(material)) {
        if (dot(wi, -wo) < 0.9999) {
            return 0.0;
        }

        pdf = 1.0;
        return coatAttenuation * transmissionColor / max(absCosTheta(wi), GGX_EPSILON);
    }

    float etap = interfaceEta(material, frontFace, lambdaNm, 1u);
    float3 wm;
    float woDotWm;
    float wiDotWm;
    float denom;
    if (!dielectricTransmissionHalfVector(wo, wi, etap, wm, woDotWm, wiDotWm, denom)) {
        return 0.0;
    }

    float fresnel = fresnelDielectric(woDotWm, etap);
    float transmissionProbability = dielectricTransmissionProbability(material, fresnel);
    if (transmissionProbability <= 0.0) {
        return 0.0;
    }

    float Dm = ggxDistribution(wm, params.alpha);
    float G = ggxMasking(wo, wi, params.alpha);
    float denom2 = denom * denom;
    float transportScale = 1.0 / max(etap * etap, GGX_EPSILON);
    float transmissionTerm = abs((wiDotWm * woDotWm) / max(absCosTheta(wi) * cosTheta(wo) * denom2, GGX_EPSILON));
    float dwmDwi = abs(wiDotWm) / max(denom2, GGX_EPSILON);

    pdf = transmissionProbability * ggxVisibleNormalPdf(wo, wm, params) * dwmDwi;
    return coatAttenuation * transmissionColor * ((1.0 - fresnel) * Dm * G * transmissionTerm * transportScale);
}

float4 evalSpectralDielectricTransmission(
    Material material,
    float3 wo,
    float3 wi,
    uint frontFace,
    GGXParams params,
    float coatAttenuation,
    float4 wavelengthsNm,
    out float4 techniquePdf
) {
    float4 value = float4(0.0);
    techniquePdf = float4(0.0);
    float4 transmissionColor = spectralScalarFromLinearSrgb4(saturate(bsdfTransmissionColor(material)), wavelengthsNm) *
                               ((1.0 - material.metallic) * material.transmission);

    value.x = evalSpectralDielectricTransmissionLane(
        material,
        wo,
        wi,
        frontFace,
        params,
        coatAttenuation,
        transmissionColor.x,
        wavelengthsNm.x,
        techniquePdf.x
    );
    value.y = evalSpectralDielectricTransmissionLane(
        material,
        wo,
        wi,
        frontFace,
        params,
        coatAttenuation,
        transmissionColor.y,
        wavelengthsNm.y,
        techniquePdf.y
    );
    value.z = evalSpectralDielectricTransmissionLane(
        material,
        wo,
        wi,
        frontFace,
        params,
        coatAttenuation,
        transmissionColor.z,
        wavelengthsNm.z,
        techniquePdf.z
    );
    value.w = evalSpectralDielectricTransmissionLane(
        material,
        wo,
        wi,
        frontFace,
        params,
        coatAttenuation,
        transmissionColor.w,
        wavelengthsNm.w,
        techniquePdf.w
    );
    return value;
}

bool sampleDielectric(
    Material material,
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
