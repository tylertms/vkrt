#pragma once

#include "vkrt_internal.h"

#include <stddef.h>
#include <stdint.h>

typedef enum RenderImageFormat {
    RENDER_IMAGE_FORMAT_PNG = 0,
    RENDER_IMAGE_FORMAT_JPEG,
    RENDER_IMAGE_FORMAT_EXR,
} RenderImageFormat;

typedef enum RenderImageBufferFormat {
    RENDER_IMAGE_BUFFER_FORMAT_RGBA32F = 0,
    RENDER_IMAGE_BUFFER_FORMAT_RGBA16F,
    RENDER_IMAGE_BUFFER_FORMAT_RGBA16_UNORM,
} RenderImageBufferFormat;

typedef struct RenderImageBuffer {
    void* pixels;
    RenderImageBufferFormat format;
} RenderImageBuffer;

typedef enum RenderImageJobType {
    RENDER_IMAGE_JOB_TYPE_SAVE = 0,
    RENDER_IMAGE_JOB_TYPE_VIEWPORT_DENOISE,
} RenderImageJobType;

typedef struct RenderImageExportJob {
    struct RenderImageExportJob* next;
    RenderImageJobType type;
    uint64_t renderSequence;
    char* path;
    uint32_t width;
    uint32_t height;
    RenderImageFormat format;
    VKRT_RenderExportSettings settings;
    VKRT_SceneSettingsSnapshot sceneSettings;
    RenderImageBuffer beauty;
    RenderImageBuffer albedo;
    RenderImageBuffer normal;
} RenderImageExportJob;

int resolveRenderImagePath(const char* requestedPath, char** outResolvedPath, RenderImageFormat* outFormat);
int queryRenderImageBufferByteCount(
    uint32_t width,
    uint32_t height,
    RenderImageBufferFormat format,
    size_t* outByteCount
);
RenderImageExportJob* createRenderImageJob(
    VKRT* vkrt,
    RenderImageJobType type,
    uint32_t width,
    uint32_t height,
    const VKRT_RenderExportSettings* settings
);
void freeRenderImageExportJob(RenderImageExportJob* job);
int processRenderImageExportJob(RenderImageExportJob* job);
int processViewportDenoiseJob(RenderImageExportJob* job, uint16_t** outPixels, size_t* outByteCount);
int queueRenderImageJob(VKRT* vkrt, RenderImageExportJob* job);
int readbackImagePixels(
    VKRT* vkrt,
    VkImage image,
    uint32_t width,
    uint32_t height,
    RenderImageBufferFormat format,
    void** outPixels
);
int uploadImagePixels(VKRT* vkrt, VkImage image, uint32_t width, uint32_t height, const void* pixels, size_t byteCount);
