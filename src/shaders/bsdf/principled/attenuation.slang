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

float3 dielectricDirectionalAlbedo(BSDFMaterial material, float3 w, GGXParams params) {
    if (dielectricIsIdentity(material)) return float3(0.0);
    if (absCosTheta(w) <= 0.0) {
        return float3(1.0);
    }

    float2 energy = float2(params.energy, params.schlickEnergy);
    float3 f0 = bsdfDielectricSpecularF0(material);
    float3 scale = ggxEnergyScale(energy.x, params.averageEnergy, lerp(f0, float3(1.0), 1.0 / 21.0));
    return saturate((f0 * (energy.x - energy.y) + energy.y) * scale);
}

float3 dielectricDirectionalAttenuation(BSDFMaterial material, float3 w, GGXParams params) {
    return saturate(1.0 - dielectricDirectionalAlbedo(material, w, params));
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
