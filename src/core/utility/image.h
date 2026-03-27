#pragma once

#include "constants.h"
#include "formats.h"

#include <stddef.h>
#include <stdint.h>

typedef struct VKRT_LoadedImage {
    void* pixels;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t colorSpace;
} VKRT_LoadedImage;

int vkrtTryComputeImageByteCount(uint32_t width, uint32_t height, uint32_t channels, size_t* outByteCount);
int vkrtLoadImageFromFile(const char* path, uint32_t preferredColorSpace, VKRT_LoadedImage* outImage);
int vkrtLoadImageFromMemory(
    const void* data,
    size_t size,
    const char* mimeType,
    uint32_t preferredColorSpace,
    VKRT_LoadedImage* outImage
);
void vkrtFreeLoadedImage(VKRT_LoadedImage* image);
