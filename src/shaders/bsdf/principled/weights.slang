#ifndef VKRT_BSDF_PRINCIPLED_WEIGHTS_SLANG
#define VKRT_BSDF_PRINCIPLED_WEIGHTS_SLANG

BSDFBranchWeights normalizeBranchWeights(BSDFBranchWeights weights) {
    float totalWeight =
        weights.sheen + weights.coat + weights.metal + weights.dielectric + weights.diffuse + weights.subsurface;

    if (totalWeight <= 0.0) {
        weights.diffuse = 1.0;
        weights.sheen = 0.0;
        weights.coat = 0.0;
        weights.metal = 0.0;
        weights.dielectric = 0.0;
        weights.subsurface = 0.0;
        return weights;
    }

    weights.sheen /= totalWeight;
    weights.coat /= totalWeight;
    weights.metal /= totalWeight;
    weights.dielectric /= totalWeight;
    weights.diffuse /= totalWeight;
    weights.subsurface /= totalWeight;
    return weights;
}

BSDFBranchWeights makeBSDFBranchWeights(BSDFMaterial material, float3 wo, uint frontFace) {
    BSDFBranchWeights weights;
    float3 diffuseColor = bsdfDiffuseColor(material);
    float3 sheenColor = bsdfSheenColor(material);
    float nonMetal = 1.0 - material.metallic;
    float baseColorWeight = max(linearSrgbLuminance(material.baseColor), 1e-3);
    float diffuseColorWeight = max(linearSrgbLuminance(diffuseColor), 1e-3);
    float baseScatter = nonMetal * (1.0 - material.transmission);
    float transmissionWeight = material.transmission * max(linearSrgbLuminance(bsdfTransmissionColor(material)), 1e-3);
    float dielectricSpecularWeight =
        nonMetal * max(bsdfDielectricSpecularF0Luminance(material) + transmissionWeight, 0.0);
    weights.sheen = linearSrgbLuminance(sheenColor);

    if (material.transmission > 0.0 && frontFace == 0u) {
        weights.sheen = 0.0;
        weights.coat = 0.0;
        weights.metal = material.metallic * baseColorWeight;
        weights.dielectric = dielectricSpecularWeight;
        weights.diffuse = 0.0;
        weights.subsurface = 0.0;
        return normalizeBranchWeights(weights);
    }

    float viewSheenAttenuation = sheenDirectionalAttenuation(sheenColor, wo, material.sheenRoughness);
    float viewCoatAttenuation = coatDirectionalAttenuation(material, wo);
    float dielectricWeightAttenuation = linearSrgbLuminance(dielectricDirectionalAttenuation(material, wo));

    weights.coat = viewSheenAttenuation * material.clearcoat * 0.25;
    weights.metal = viewSheenAttenuation * viewCoatAttenuation * material.metallic * baseColorWeight;
    weights.dielectric = viewSheenAttenuation * viewCoatAttenuation * dielectricSpecularWeight;
    weights.diffuse = viewSheenAttenuation * viewCoatAttenuation * dielectricWeightAttenuation * baseScatter *
                      (1.0 - material.subsurface) * diffuseColorWeight;
    weights.subsurface = viewSheenAttenuation * viewCoatAttenuation * dielectricWeightAttenuation * baseScatter *
                         material.subsurface * diffuseColorWeight;
    return normalizeBranchWeights(weights);
}

#endif
