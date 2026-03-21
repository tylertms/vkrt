#ifndef VKRT_UTILITY_DEBUG_SLANG
#define VKRT_UTILITY_DEBUG_SLANG

#include "../scene/constants.slang"
#include "../material/textures.slang"

bool isTextureMapDebugMode(uint debugMode) {
    return debugMode == VKRT_DEBUG_MODE_BASE_COLOR_MAP ||
        debugMode == VKRT_DEBUG_MODE_METALLIC_MAP ||
        debugMode == VKRT_DEBUG_MODE_ROUGHNESS_MAP ||
        debugMode == VKRT_DEBUG_MODE_NORMAL_MAP ||
        debugMode == VKRT_DEBUG_MODE_EMISSIVE_MAP;
}

float3 evaluateTextureMapDebug(Material material, float2 texcoord0, float2 texcoord1, uint debugMode) {
    float4 metallicRoughness = float4(0.0);
    if (debugMode == VKRT_DEBUG_MODE_METALLIC_MAP || debugMode == VKRT_DEBUG_MODE_ROUGHNESS_MAP) {
        metallicRoughness = sampleMaterialTextureSlot(material, VKRT_TEXTURE_SLOT_METALLIC_ROUGHNESS, texcoord0, texcoord1);
    }

    switch (debugMode) {
        case VKRT_DEBUG_MODE_BASE_COLOR_MAP:
            return sampleMaterialTextureSlot(material, VKRT_TEXTURE_SLOT_BASE_COLOR, texcoord0, texcoord1).rgb;
        case VKRT_DEBUG_MODE_METALLIC_MAP:
            return metallicRoughness.bbb;
        case VKRT_DEBUG_MODE_ROUGHNESS_MAP:
            return metallicRoughness.ggg;
        case VKRT_DEBUG_MODE_NORMAL_MAP:
            return sampleMaterialTextureSlot(material, VKRT_TEXTURE_SLOT_NORMAL, texcoord0, texcoord1).rgb;
        case VKRT_DEBUG_MODE_EMISSIVE_MAP:
            return sampleMaterialTextureSlot(material, VKRT_TEXTURE_SLOT_EMISSIVE, texcoord0, texcoord1).rgb;
        default:
            return float3(0.0);
    }
}

#endif
