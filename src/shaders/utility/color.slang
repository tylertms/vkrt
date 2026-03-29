#ifndef VKRT_UTILITY_COLOR_SLANG
#define VKRT_UTILITY_COLOR_SLANG

static const float3 VKRT_LINEAR_SRGB_LUMINANCE_WEIGHTS = float3(0.2126, 0.7152, 0.0722);

float linearSrgbLuminance(float3 rgb) {
    return dot(rgb, VKRT_LINEAR_SRGB_LUMINANCE_WEIGHTS);
}

#endif
