#ifndef VKRT_UTILITY_DEBUG_SLANG
#define VKRT_UTILITY_DEBUG_SLANG

#include "../material/textures.slang"
#include "../scene/constants.slang"

bool isTextureMapDebugMode(uint debugMode) {
    return debugMode == VKRT_DEBUG_MODE_BASE_COLOR_MAP || debugMode == VKRT_DEBUG_MODE_METALLIC_MAP ||
           debugMode == VKRT_DEBUG_MODE_ROUGHNESS_MAP || debugMode == VKRT_DEBUG_MODE_NORMAL_MAP ||
           debugMode == VKRT_DEBUG_MODE_EMISSIVE_MAP;
}

float3 evaluateTextureMapDebug(Material material, SurfaceTextureData surface, uint debugMode) {
    float4 metallicRoughness = float4(0.0);
    if (debugMode == VKRT_DEBUG_MODE_METALLIC_MAP || debugMode == VKRT_DEBUG_MODE_ROUGHNESS_MAP) {
        metallicRoughness = sampleMetallicRoughnessTexture(material, surface);
    }

    switch (debugMode) {
        case VKRT_DEBUG_MODE_BASE_COLOR_MAP:
            return sampleBaseColorTexture(material, surface).rgb;
        case VKRT_DEBUG_MODE_METALLIC_MAP:
            return metallicRoughness.bbb;
        case VKRT_DEBUG_MODE_ROUGHNESS_MAP:
            return metallicRoughness.ggg;
        case VKRT_DEBUG_MODE_NORMAL_MAP:
            return sampleNormalTexture(material, surface).rgb;
        case VKRT_DEBUG_MODE_EMISSIVE_MAP:
            return sampleEmissiveTexture(material, surface).rgb;
        default:
            return float3(0.0);
    }
}

#endif
