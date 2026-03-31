#ifndef VKRT_BSDF_BASE_MATERIAL_SLANG
#define VKRT_BSDF_BASE_MATERIAL_SLANG

struct BSDFMaterial {
    float3 baseColor = float3(1.0);
    float roughness = 1.0;
    float3 eta = float3(1.0);
    float metallic = 0.0;
    float3 k = float3(0.0);
    float anisotropic = 0.0;
    float specular = 0.5;
    float specularTint = 0.0;
    float abbeNumber = 0.0;
    float4 sheenTintWeight = float4(0.0);
    float clearcoat = 0.0;
    float clearcoatGloss = 0.0;
    float ior = 1.0;
    float diffuseRoughness = 0.0;
    float transmission = 0.0;
    float subsurface = 0.0;
    float sheenRoughness = 0.0;
    float absorptionCoefficient = 0.0;
    float3 attenuationColor = float3(1.0);

    __init() {}

    __init(Material material) {
        baseColor = material.baseColor;
        roughness = material.roughness;
        eta = material.eta;
        metallic = material.metallic;
        k = material.k;
        anisotropic = material.anisotropic;
        specular = material.specular;
        specularTint = material.specularTint;
        abbeNumber = material.abbeNumber;
        sheenTintWeight = material.sheenTintWeight;
        clearcoat = material.clearcoat;
        clearcoatGloss = material.clearcoatGloss;
        ior = material.ior;
        diffuseRoughness = material.diffuseRoughness;
        transmission = material.transmission;
        subsurface = material.subsurface;
        sheenRoughness = material.sheenRoughness;
        absorptionCoefficient = material.absorptionCoefficient;
        attenuationColor = material.attenuationColor;
    }
};

float maxComponent(float3 value) {
    return max(value.x, max(value.y, value.z));
}

float3 bsdfDiffuseColor(BSDFMaterial material) {
    return material.baseColor * (1.0 - material.metallic);
}

float3 bsdfTintColor(float3 baseColor) {
    float luminance = linearSrgbLuminance(baseColor);
    return luminance <= 0.0 ? float3(1.0) : baseColor / luminance;
}

float bsdfSheenWeight(BSDFMaterial material) {
    return saturate(material.sheenTintWeight.w);
}

float3 bsdfSheenTint(BSDFMaterial material) {
    return saturate(material.sheenTintWeight.xyz);
}

float3 bsdfSheenColor(BSDFMaterial material) {
    return bsdfSheenTint(material) * bsdfSheenWeight(material);
}

float3 bsdfTransmissionColor(BSDFMaterial material) {
    return material.transmission > 0.0 && material.metallic <= 0.0 ? float3(1.0) : material.baseColor;
}

#endif
