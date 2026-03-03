#ifndef SHADER_UTILITY_MATERIAL_GLSL
#define SHADER_UTILITY_MATERIAL_GLSL

#include "core/types.glsl"
#include "core/math.glsl"

float specularSampleWeight(Material material) {
    vec3 f0 = mix(vec3(0.04), material.baseColor, material.baseMetalness);
    float diffuseEnergy = (1.0 - material.baseMetalness) * max(luminance(material.baseColor), 0.0);
    float specularEnergy = max(luminance(f0), 0.0);
    float energySum = diffuseEnergy + specularEnergy;
    if (energySum <= 1e-6) return 0.5;
    return clamp(specularEnergy / energySum, 0.0, 1.0);
}

#endif
