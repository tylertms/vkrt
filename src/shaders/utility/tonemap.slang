#ifndef VKRT_UTILITY_TONEMAP_SLANG
#define VKRT_UTILITY_TONEMAP_SLANG

float3 toneMapACES(float3 color) {
    return (color * (2.51 * color + 0.03)) / (color * (2.43 * color + 0.59) + 0.14);
}

#endif
