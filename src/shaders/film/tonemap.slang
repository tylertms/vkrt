#ifndef VKRT_UTILITY_TONEMAP_SLANG
#define VKRT_UTILITY_TONEMAP_SLANG

#include "../scene/resources.slang"

// References:
// ACES filmic curve approximation: Narkowicz, 2016 - https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/

float srgbEncodeScalar(float value) {
    if (value <= 0.0031308) return 12.92 * value;
    return 1.055 * pow(value, 1.0 / 2.4) - 0.055;
}

float3 encodeDisplayColor(float3 color) {
    color = saturate(color);
    return float3(srgbEncodeScalar(color.x), srgbEncodeScalar(color.y), srgbEncodeScalar(color.z));
}

float3 mapSceneColorToDisplay(float3 color) {
    return encodeDisplayColor(toneMap(max(color * scene.exposure, float3(0.0))));
}

float3 toneMapACES(float3 color) {
    return (color * (2.51 * color + 0.03)) / (color * (2.43 * color + 0.59) + 0.14);
}

float3 toneMap(float3 color) {
    if (VKRT_SCENE_TONE_MAPPING_MODE_FROM_RENDER_SETTINGS(scene.packedRenderSettings) == VKRT_TONE_MAPPING_MODE_ACES) {
        return toneMapACES(color);
    }
    return color;
}

#endif
