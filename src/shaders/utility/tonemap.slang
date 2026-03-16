#ifndef VKRT_UTILITY_TONEMAP_SLANG
#define VKRT_UTILITY_TONEMAP_SLANG

#include "../core/resources.slang"

// References:
// ACES filmic curve approximation: Narkowicz, 2016 - https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/

float3 toneMapACES(float3 color) {
    return (color * (2.51 * color + 0.03)) / (color * (2.43 * color + 0.59) + 0.14);
}

float3 toneMap(float3 color) {
    if (scene.toneMappingMode == VKRT_TONE_MAPPING_MODE_ACES) return toneMapACES(color);
    return color;
}

#endif
