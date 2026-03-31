#ifndef VKRT_BSDF_PRINCIPLED_ATTENUATION_SLANG
#define VKRT_BSDF_PRINCIPLED_ATTENUATION_SLANG

bool useInteriorDielectricInterface(BSDFState state) {
    return state.material.transmission > 0.0 && state.frontFace == 0u;
}

float scalarSchlickFresnel(float cosThetaValue, float f0) {
    return lerp(f0, 1.0, schlickWeight(abs(cosThetaValue)));
}

float coatDirectionalAttenuation(BSDFMaterial material, float3 w) {
    if (material.clearcoat <= 0.0) {
        return 1.0;
    }

    if (absCosTheta(w) <= 0.0) {
        return 0.0;
    }

    float coatFresnel = 0.25 * material.clearcoat * scalarSchlickFresnel(absCosTheta(w), 0.04);
    return 1.0 - saturate(coatFresnel);
}

float sheenDirectionalAttenuation(float3 sheenColor, float3 w, float sheenRoughness) {
    float sheenWeight = maxComponent(sheenColor);
    return sheenLayerAttenuation(sheenWeight, cosTheta(w), sheenRoughness);
}

float2 dielectricDirectionalAlbedoAB(float roughness, float cosThetaValue) {
    float clampedRoughness = saturate(roughness);
    float clampedCosTheta = saturate(cosThetaValue);
    float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    float4 r = clampedRoughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * clampedCosTheta)) * r.x + r.y;
    return saturate(float2(-1.04, 1.04) * a004 + r.zw);
}

float3 dielectricDirectionalAlbedo(BSDFMaterial material, float3 w) {
    if (absCosTheta(w) <= 0.0) {
        return float3(1.0);
    }

    float2 ab = dielectricDirectionalAlbedoAB(material.roughness, absCosTheta(w));
    return saturate(bsdfDielectricSpecularF0(material) * ab.x + ab.y);
}

float3 dielectricDirectionalAttenuation(BSDFMaterial material, float3 w) {
    return saturate(1.0 - dielectricDirectionalAlbedo(material, w));
}

float reflectionStackAttenuation(BSDFMaterial material, float viewCoatAttenuation, float3 wi) {
    return viewCoatAttenuation * coatDirectionalAttenuation(material, wi);
}

float transmissionStackAttenuation(float viewSheenAttenuation, float viewCoatAttenuation) {
    return viewSheenAttenuation * viewCoatAttenuation;
}

float materialTransmissionStackAttenuation(BSDFMaterial material, float3 wo) {
    return transmissionStackAttenuation(
        sheenDirectionalAttenuation(bsdfSheenColor(material), wo, material.sheenRoughness),
        coatDirectionalAttenuation(material, wo)
    );
}

#endif
