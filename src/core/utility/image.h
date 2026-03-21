#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct VKRT_LoadedImage {
    uint8_t* pixels;
    uint32_t width;
    uint32_t height;
} VKRT_LoadedImage;

int vkrtLoadImageFromFile(const char* path, VKRT_LoadedImage* outImage);
int vkrtLoadImageFromMemory(
    const void* data,
    size_t size,
    const char* mimeType,
    VKRT_LoadedImage* outImage
);
void vkrtFreeLoadedImage(VKRT_LoadedImage* image);
