#ifndef VKRT_BSDF_DIELECTRIC_EVAL_SPECTRAL_SLANG
#define VKRT_BSDF_DIELECTRIC_EVAL_SPECTRAL_SLANG

#include "./eval_rgb.slang"

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
    [unroll] for (uint lane = 0u; lane < 4u; lane++) {
        techniquePdf[lane] = dielectricIsIdentity(material, wavelengthsNm[lane], 1u)
                               ? 0.0
                               : (1.0 - dielectricTransmissionProbability(material, fresnel[lane])) * reflectionBasePdf;
    }
    if (!any(techniquePdf > 0.0)) {
        return float4(0.0);
    }

    float4 value = dielectricReflectionColor4(material, woDotWm, fresnel, wavelengthsNm) *
                   (Dm * G / (4.0 * cosTheta(wo) * cosTheta(wi)));
    [unroll] for (uint lane = 0u; lane < 4u; lane++) {
        if (techniquePdf[lane] <= 0.0) value[lane] = 0.0;
    }
    return value;
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
    BSDFEval eval = evalDielectricTransmissionInterface(material, wo, wi, frontFace, params, lambdaNm, 1u);
    pdf = eval.pdf;
    return coatAttenuation * transmissionColor * eval.value.x;
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
