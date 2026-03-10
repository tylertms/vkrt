#ifndef VKRT_UTILITY_TONEMAP_SLANG
#define VKRT_UTILITY_TONEMAP_SLANG

float3 toneMapACES(float3 color) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((color * (a * color + b)) / (color * (c * color + d) + e));
}

#endif
