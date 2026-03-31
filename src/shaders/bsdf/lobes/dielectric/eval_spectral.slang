#ifndef VKRT_BSDF_DIELECTRIC_EVAL_SPECTRAL_SLANG
#define VKRT_BSDF_DIELECTRIC_EVAL_SPECTRAL_SLANG

#include "./interface.slang"

float4 evalSpectralDielectricReflection(
    BSDFMaterial material,
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
    BSDFMaterial material,
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
    BSDFMaterial material,
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

#endif
