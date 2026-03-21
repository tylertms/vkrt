#include "image.h"

#include "debug.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <limits.h>
#include <string.h>

static int mimeTypeStartsWith(const char* mimeType, const char* prefix) {
    if (!mimeType || !prefix) return 0;
    size_t prefixLength = strlen(prefix);
    return strncmp(mimeType, prefix, prefixLength) == 0;
}

static int mimeTypeExplicitlyUnsupported(const char* mimeType) {
    if (!mimeType || !mimeType[0]) return 0;
    return mimeTypeStartsWith(mimeType, "image/ktx") ||
        mimeTypeStartsWith(mimeType, "image/vnd-ms.dds");
}

static const char* imageSourceLabel(const char* mimeType) {
    return mimeType && mimeType[0] ? mimeType : "memory";
}

static int finalizeLoadedImage(
    const char* sourceLabel,
    stbi_uc* pixels,
    int width,
    int height,
    VKRT_LoadedImage* outImage
) {
    if (!pixels || !outImage || width <= 0 || height <= 0) {
        stbi_image_free(pixels);
        LOG_ERROR("Decoded image from %s had invalid dimensions", sourceLabel);
        return 0;
    }

    outImage->pixels = pixels;
    outImage->width = (uint32_t)width;
    outImage->height = (uint32_t)height;
    return 1;
}

static int reportDecodeFailure(const char* sourceLabel) {
    const char* reason = stbi_failure_reason();
    LOG_ERROR("Image decode from %s failed (%s)", sourceLabel, reason ? reason : "unknown error");
    return 0;
}

int vkrtLoadImageFromFile(const char* path, VKRT_LoadedImage* outImage) {
    if (!path || !path[0] || !outImage) return 0;
    *outImage = (VKRT_LoadedImage){0};

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load(path, &width, &height, &channels, STBI_rgb_alpha);
    if (!pixels) return reportDecodeFailure(path);
    return finalizeLoadedImage(path, pixels, width, height, outImage);
}

int vkrtLoadImageFromMemory(
    const void* data,
    size_t size,
    const char* mimeType,
    VKRT_LoadedImage* outImage
) {
    if (!data || size == 0 || !outImage || size > (size_t)INT_MAX) return 0;
    *outImage = (VKRT_LoadedImage){0};
    if (mimeTypeExplicitlyUnsupported(mimeType)) {
        LOG_ERROR("Unsupported embedded image mime type: %s", mimeType);
        return 0;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(
        (const stbi_uc*)data,
        (int)size,
        &width,
        &height,
        &channels,
        STBI_rgb_alpha
    );
    if (!pixels) return reportDecodeFailure(imageSourceLabel(mimeType));
    return finalizeLoadedImage(imageSourceLabel(mimeType), pixels, width, height, outImage);
}

void vkrtFreeLoadedImage(VKRT_LoadedImage* image) {
    if (!image) return;
    stbi_image_free(image->pixels);
    *image = (VKRT_LoadedImage){0};
}
