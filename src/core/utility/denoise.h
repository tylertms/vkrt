#pragma once

#include <stdint.h>

typedef enum VKRT_OIDNAuxImage {
    VKRT_OIDN_AUX_IMAGE_ALBEDO = 0,
    VKRT_OIDN_AUX_IMAGE_NORMAL,
} VKRT_OIDNAuxImage;

typedef struct VKRT_OIDNFilterInput {
    const float* color;
    const float* albedo;
    const float* normal;
    uint32_t width;
    uint32_t height;
    uint8_t cleanAux;
} VKRT_OIDNFilterInput;

int vkrtOIDNDenoise(const VKRT_OIDNFilterInput* input, float* output, const char** outErrorMessage);
int vkrtOIDNPrefilterAux(
    VKRT_OIDNAuxImage auxImage,
    const float* input,
    uint32_t width,
    uint32_t height,
    float* output,
    const char** outErrorMessage
);
