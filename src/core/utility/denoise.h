#pragma once

#include <stdint.h>

typedef struct VKRT_OIDNDenoiser VKRT_OIDNDenoiser;

typedef struct VKRT_OIDNFilterInput {
    const float* color;
    const float* albedo;
    const float* normal;
    const uint8_t* deviceUUID;
    uint32_t width;
    uint32_t height;
} VKRT_OIDNFilterInput;

VKRT_OIDNDenoiser* vkrtOIDNCreateDenoiser(void);
void vkrtOIDNDestroyDenoiser(VKRT_OIDNDenoiser* denoiser);
int vkrtOIDNDenoise(
    VKRT_OIDNDenoiser* denoiser,
    const VKRT_OIDNFilterInput* input,
    float* output,
    const char** outErrorMessage
);
