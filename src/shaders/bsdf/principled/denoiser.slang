#ifndef VKRT_BSDF_PRINCIPLED_DENOISER_SLANG
#define VKRT_BSDF_PRINCIPLED_DENOISER_SLANG

static const float VKRT_DENOISER_SPECULAR_FOLLOW_ROUGHNESS = 0.02;
static const float VKRT_DENOISER_LAYER_EPSILON = 1e-3;

bool materialDenoiserShouldFollowSpecularHit(BSDFMaterial material, uint frontFace) {
    float diffuseWeight = maxComponent(bsdfDiffuseColor(material)) * (1.0 - material.transmission);
    float subsurfaceWeight = diffuseWeight * material.subsurface;
    float sheenLayerWeight = maxComponent(bsdfSheenColor(material)) * (1.0 - material.transmission);
    float clearcoatLayerWeight = frontFace != 0u ? material.clearcoat : 0.0;
    bool hasVisibleBaseLayer =
        diffuseWeight > VKRT_DENOISER_LAYER_EPSILON || subsurfaceWeight > VKRT_DENOISER_LAYER_EPSILON ||
        sheenLayerWeight > VKRT_DENOISER_LAYER_EPSILON || clearcoatLayerWeight > VKRT_DENOISER_LAYER_EPSILON;
    if (hasVisibleBaseLayer) return false;
    if (material.transmission > VKRT_DENOISER_LAYER_EPSILON && dielectricIsIdentity(material)) return true;
    if (material.roughness > VKRT_DENOISER_SPECULAR_FOLLOW_ROUGHNESS) return false;

    return material.transmission > VKRT_DENOISER_LAYER_EPSILON ||
           material.metallic >= 1.0 - VKRT_DENOISER_LAYER_EPSILON ||
           maxComponent(bsdfDielectricSpecularF0(material)) > VKRT_DENOISER_LAYER_EPSILON;
}

float3 materialDenoiserAlbedo(BSDFMaterial material) {
    float nonMetal = 1.0 - material.metallic;
    float3 diffuseColor = bsdfDiffuseColor(material);
    float3 sheenColor = bsdfSheenColor(material);
    float3 dielectricF0 = bsdfDielectricSpecularF0(material);
    float3 metallicF0 = materialHasConductor(material)
                          ? fresnelConductor(1.0, max(material.eta, float3(1e-3)), max(material.k, float3(0.0)))
                          : saturate(material.baseColor);
    float diffuseWeight = nonMetal * (1.0 - material.transmission) * (1.0 - material.subsurface);
    float subsurfaceWeight = nonMetal * (1.0 - material.transmission) * material.subsurface;
    float specularWeight = nonMetal * (1.0 - material.transmission) * bsdfDielectricSpecularF0Luminance(material);
    float transmissionWeight = nonMetal * material.transmission;
    float metallicWeight = material.metallic;
    float sheenLayerWeight = maxComponent(sheenColor) * nonMetal * (1.0 - material.transmission);
    float totalWeight =
        diffuseWeight + subsurfaceWeight + specularWeight + transmissionWeight + metallicWeight + sheenLayerWeight;

    if (totalWeight <= 0.0) return saturate(diffuseColor);

    float3 albedo = diffuseColor * diffuseWeight + diffuseColor * subsurfaceWeight + dielectricF0 * specularWeight +
                    float3(1.0) * transmissionWeight + metallicF0 * metallicWeight + sheenColor * sheenLayerWeight;
    return saturate(albedo / totalWeight);
}

#endif
