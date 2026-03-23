#pragma once

#include "image.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int vkrtLoadEXRImageFromFile(const char* path, VKRT_LoadedImage* outImage);
int vkrtLoadEXRImageFromMemory(const void* data, size_t size, const char* sourceLabel, VKRT_LoadedImage* outImage);
int vkrtWriteEXRFromRGBA32F(const char* path, const float* rgba32f, uint32_t width, uint32_t height);

#ifdef __cplusplus
}
#endif
