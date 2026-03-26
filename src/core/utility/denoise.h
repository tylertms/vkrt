#pragma once

#include <stdint.h>

typedef struct VKRT_OIDNFilterInput {
    const float* color;
    const float* albedo;
    const float* normal;
    uint32_t width;
    uint32_t height;
    uint8_t cleanAux;
} VKRT_OIDNFilterInput;

int vkrtOIDNAvailable(void);
int vkrtOIDNDenoise(const VKRT_OIDNFilterInput* input, float* output, const char** outErrorMessage);
