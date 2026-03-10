#pragma once

#include <math.h>

static inline float vkrtClampf(float value, float minValue, float maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

static inline float vkrtFiniteOrf(float value, float fallback) {
    return isfinite(value) ? value : fallback;
}

static inline float vkrtFiniteClampf(float value, float fallback, float minValue, float maxValue) {
    return vkrtClampf(vkrtFiniteOrf(value, fallback), minValue, maxValue);
}
