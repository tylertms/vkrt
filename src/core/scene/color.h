#ifndef VKRT_SCENE_COLOR_H
#define VKRT_SCENE_COLOR_H

static inline float linearSRGBLuminance(const float rgb[3]) {
    if (!rgb) return 0.0f;
    return (0.2126f * rgb[0]) + (0.7152f * rgb[1]) + (0.0722f * rgb[2]);
}

#endif
