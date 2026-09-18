#pragma once

#include <stdint.h>

typedef struct VKRT_OIDNFilterInput {
    const float* color;
    const float* albedo;
    const float* normal;
    const uint8_t* deviceUUID;
    uint32_t width;
    uint32_t height;
} VKRT_OIDNFilterInput;

int vkrtOIDNDenoise(const VKRT_OIDNFilterInput* input, float* output, const char** outErrorMessage);
