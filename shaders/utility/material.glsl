#ifndef SHADER_UTILITY_MATERIAL_GLSL
#define SHADER_UTILITY_MATERIAL_GLSL

#include "core/types.glsl"
#include "core/math.glsl"

struct MaterialBSDFState {
    vec3 diffuseColor;
    vec3 f0;
    float alpha;
};

float dielectricF0FromIor(float ior) {
    float safeIor = max(ior, 1.0001);
    float eta = (safeIor - 1.0) / (safeIor + 1.0);
    return clamp(eta * eta, 0.0, 0.999);
}

MaterialBSDFState buildMaterialBSDFState(Material material) {
    MaterialBSDFState state;

    vec3 baseColor = clamp(material.baseColor, vec3(0.0), vec3(1.0));
    float baseWeight = clamp(material.baseWeight, 0.0, 1.0);
    float metalness = clamp(material.baseMetalness, 0.0, 1.0);
    float specularWeight = clamp(material.specularWeight, 0.0, 1.0);
    vec3 specularColor = clamp(material.specularColor, vec3(0.0), vec3(1.0));

    vec3 dielectricF0 = vec3(dielectricF0FromIor(material.specularIor)) * specularColor;
    vec3 metallicF0 = baseColor * specularColor;

    state.f0 = clamp(mix(dielectricF0, metallicF0, metalness) * specularWeight, vec3(0.0), vec3(0.999));
    state.diffuseColor = baseWeight * (1.0 - metalness) * baseColor;
    float roughness = clamp(material.specularRoughness, 0.0, 1.0);
    state.alpha = max(roughness * roughness, 0.001);

    return state;
}

float specularSampleWeight(MaterialBSDFState state) {
    float diffuseEnergy = max(luminance(state.diffuseColor * (vec3(1.0) - state.f0)), 0.0);
    float specularEnergy = max(luminance(state.f0), 0.0);
    float energySum = diffuseEnergy + specularEnergy;
    if (energySum <= 1e-6) return 0.0;
    return clamp(specularEnergy / energySum, 0.0, 1.0);
}

float specularSampleWeight(Material material) {
    return specularSampleWeight(buildMaterialBSDFState(material));
}

#endif
