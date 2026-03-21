#include "export.h"

#include "buffer.h"
#include "command/pool.h"
#include "command/record.h"
#include "debug.h"
#include "io.h"
#include "platform.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint32_t kMaxPendingImageExportJobs = 2u;
static const int kJPEGQuality = 95;

typedef enum RenderImageFormat {
    RENDER_IMAGE_FORMAT_PNG = 0,
    RENDER_IMAGE_FORMAT_JPEG,
    RENDER_IMAGE_FORMAT_BMP,
    RENDER_IMAGE_FORMAT_TGA,
} RenderImageFormat;

typedef struct RenderImageExportJob {
    struct RenderImageExportJob* next;
    char* path;
    uint16_t* rgba16;
    uint32_t width;
    uint32_t height;
    RenderImageFormat format;
} RenderImageExportJob;

static int tryComputeImageByteCount(uint32_t width, uint32_t height, uint32_t channels, size_t* outByteCount) {
    if (!outByteCount || width == 0u || height == 0u || channels == 0u) return 0;
    size_t pixelCount = (size_t)width * (size_t)height;
    if (pixelCount > SIZE_MAX / (size_t)channels) return 0;
    *outByteCount = pixelCount * (size_t)channels;
    return 1;
}

static uint8_t convertUnorm16ToUnorm8(uint16_t value) {
    return (uint8_t)(((uint32_t)value * 255u + 32767u) / 65535u);
}

static const char* queryPathExtension(const char* path) {
    const char* basename = pathBasename(path);
    if (!basename || !basename[0]) return NULL;

    const char* extension = strrchr(basename, '.');
    if (!extension || extension == basename || extension[1] == '\0') return NULL;
    return extension;
}

static int equalsIgnoreCaseASCII(const char* lhs, const char* rhs) {
    if (!lhs || !rhs) return 0;
    while (*lhs && *rhs) {
        if (tolower((unsigned char)*lhs) != tolower((unsigned char)*rhs)) return 0;
        lhs++;
        rhs++;
    }
    return *lhs == '\0' && *rhs == '\0';
}

static int queryRenderImageFormatForPath(const char* path, RenderImageFormat* outFormat) {
    if (!path || !outFormat) return 0;

    const char* extension = queryPathExtension(path);
    if (!extension) return 0;

    if (equalsIgnoreCaseASCII(extension, ".png")) {
        *outFormat = RENDER_IMAGE_FORMAT_PNG;
        return 1;
    }
    if (equalsIgnoreCaseASCII(extension, ".jpg") || equalsIgnoreCaseASCII(extension, ".jpeg")) {
        *outFormat = RENDER_IMAGE_FORMAT_JPEG;
        return 1;
    }
    if (equalsIgnoreCaseASCII(extension, ".bmp")) {
        *outFormat = RENDER_IMAGE_FORMAT_BMP;
        return 1;
    }
    if (equalsIgnoreCaseASCII(extension, ".tga")) {
        *outFormat = RENDER_IMAGE_FORMAT_TGA;
        return 1;
    }
    return 0;
}

static char* duplicatePathWithAppendedExtension(const char* path, const char* extension) {
    if (!path || !path[0] || !extension || !extension[0]) return NULL;
    size_t pathLength = strlen(path);
    size_t extensionLength = strlen(extension);
    if (pathLength > SIZE_MAX - extensionLength - 1u) return NULL;

    char* combined = (char*)malloc(pathLength + extensionLength + 1u);
    if (!combined) return NULL;

    memcpy(combined, path, pathLength);
    memcpy(combined + pathLength, extension, extensionLength + 1u);
    return combined;
}

static int resolveRenderImagePath(
    const char* requestedPath,
    char** outResolvedPath,
    RenderImageFormat* outFormat
) {
    if (!requestedPath || !requestedPath[0] || !outResolvedPath || !outFormat) return 0;
    *outResolvedPath = NULL;

    if (queryRenderImageFormatForPath(requestedPath, outFormat)) {
        *outResolvedPath = stringDuplicate(requestedPath);
        return *outResolvedPath != NULL;
    }

    if (!queryPathExtension(requestedPath)) {
        *outFormat = RENDER_IMAGE_FORMAT_PNG;
        *outResolvedPath = duplicatePathWithAppendedExtension(requestedPath, ".png");
        return *outResolvedPath != NULL;
    }

    LOG_ERROR("Unsupported render export format: %s", requestedPath);
    return 0;
}

static int writeRenderImageFile(
    const char* path,
    const uint16_t* rgba16,
    uint32_t width,
    uint32_t height,
    RenderImageFormat format
) {
    if (!path || !rgba16 || width == 0u || height == 0u) return -1;
    if (width > (uint32_t)INT32_MAX || height > (uint32_t)INT32_MAX) {
        LOG_ERROR("Image export dimensions exceed stb limits: %ux%u", width, height);
        return -1;
    }

    size_t rgba8ByteCount = 0u;
    if (!tryComputeImageByteCount(width, height, 4u, &rgba8ByteCount)) {
        LOG_ERROR("Image export size overflow for '%s'", path);
        return -1;
    }

    uint8_t* rgba8 = (uint8_t*)malloc(rgba8ByteCount);
    if (!rgba8) {
        LOG_ERROR("Failed to allocate export buffer for '%s'", path);
        return -1;
    }

    for (size_t index = 0; index < rgba8ByteCount; index++) {
        rgba8[index] = convertUnorm16ToUnorm8(rgba16[index]);
    }

    int writeOk = 0;
    switch (format) {
        case RENDER_IMAGE_FORMAT_PNG:
            writeOk = stbi_write_png(path, (int)width, (int)height, 4, rgba8, (int)(width * 4u));
            break;
        case RENDER_IMAGE_FORMAT_BMP:
            writeOk = stbi_write_bmp(path, (int)width, (int)height, 4, rgba8);
            break;
        case RENDER_IMAGE_FORMAT_TGA:
            writeOk = stbi_write_tga(path, (int)width, (int)height, 4, rgba8);
            break;
        case RENDER_IMAGE_FORMAT_JPEG: {
            size_t rgb8ByteCount = 0u;
            if (!tryComputeImageByteCount(width, height, 3u, &rgb8ByteCount)) {
                free(rgba8);
                LOG_ERROR("JPEG export size overflow for '%s'", path);
                return -1;
            }

            uint8_t* rgb8 = (uint8_t*)malloc(rgb8ByteCount);
            if (!rgb8) {
                free(rgba8);
                LOG_ERROR("Failed to allocate JPEG export buffer for '%s'", path);
                return -1;
            }

            size_t pixelCount = (size_t)width * (size_t)height;
            for (size_t pixelIndex = 0; pixelIndex < pixelCount; pixelIndex++) {
                size_t srcOffset = pixelIndex * 4u;
                size_t dstOffset = pixelIndex * 3u;
                rgb8[dstOffset + 0u] = rgba8[srcOffset + 0u];
                rgb8[dstOffset + 1u] = rgba8[srcOffset + 1u];
                rgb8[dstOffset + 2u] = rgba8[srcOffset + 2u];
            }

            writeOk = stbi_write_jpg(path, (int)width, (int)height, 3, rgb8, kJPEGQuality);
            free(rgb8);
            break;
        }
        default:
            break;
    }

    free(rgba8);

    if (!writeOk) {
        remove(path);
        LOG_ERROR("Render export failed for '%s'", path);
        return -1;
    }

    return 0;
}

static void freeRenderImageExportJob(RenderImageExportJob* job) {
    if (!job) return;
    free(job->path);
    free(job->rgba16);
    free(job);
}

static int renderImageWorkerMain(void* userData) {
    RenderImageExporter* exporter = (RenderImageExporter*)userData;

    for (;;) {
        vkrtMutexLock(&exporter->workerLock);
        while (!exporter->stop && exporter->head == NULL) {
            vkrtCondWait(&exporter->workerCondition, &exporter->workerLock);
        }

        if (exporter->stop && exporter->head == NULL) {
            vkrtMutexUnlock(&exporter->workerLock);
            break;
        }

        RenderImageExportJob* job = exporter->head;
        exporter->head = job->next;
        if (exporter->head == NULL) {
            exporter->tail = NULL;
        }
        vkrtMutexUnlock(&exporter->workerLock);

        int result = writeRenderImageFile(job->path, job->rgba16, job->width, job->height, job->format);
        if (result == 0) {
            LOG_INFO("Saved render image: %s", job->path);
        }
        freeRenderImageExportJob(job);

        vkrtMutexLock(&exporter->workerLock);
        if (exporter->pendingJobCount > 0u) {
            exporter->pendingJobCount--;
        }
        vkrtCondBroadcast(&exporter->workerCondition);
        vkrtMutexUnlock(&exporter->workerLock);
    }

    return 0;
}

static int initializeRenderImageWorkerPrimitivesLocked(RenderImageExporter* exporter) {
    if (exporter->primitivesInitialized) return 0;

    if (vkrtMutexInit(&exporter->workerLock, VKRT_MUTEX_PLAIN) != VKRT_THREAD_SUCCESS) {
        return -1;
    }
    if (vkrtCondInit(&exporter->workerCondition) != VKRT_THREAD_SUCCESS) {
        vkrtMutexDestroy(&exporter->workerLock);
        return -1;
    }
    exporter->primitivesInitialized = 1;
    return 0;
}

static int startRenderImageWorkerLocked(RenderImageExporter* exporter) {
    if (exporter->threadRunning) return 0;
    if (initializeRenderImageWorkerPrimitivesLocked(exporter) != 0) return -1;

    exporter->stop = 0;
    exporter->head = NULL;
    exporter->tail = NULL;
    exporter->pendingJobCount = 0u;

    if (vkrtThreadCreate(&exporter->workerThread, renderImageWorkerMain, exporter) != VKRT_THREAD_SUCCESS) {
        vkrtCondDestroy(&exporter->workerCondition);
        vkrtMutexDestroy(&exporter->workerLock);
        exporter->primitivesInitialized = 0;
        return -1;
    }
    exporter->threadRunning = 1;
    return 0;
}

static int ensureRenderImageWorkerStarted(RenderImageExporter* exporter) {
    if (!exporter->stateLockInitialized) {
        if (vkrtMutexInit(&exporter->stateLock, VKRT_MUTEX_PLAIN) != VKRT_THREAD_SUCCESS) {
            LOG_ERROR("Failed to initialize render image exporter state lock");
            return -1;
        }
        exporter->stateLockInitialized = 1;
    }

    vkrtMutexLock(&exporter->stateLock);
    int result = startRenderImageWorkerLocked(exporter);
    vkrtMutexUnlock(&exporter->stateLock);

    if (result != 0) {
        LOG_ERROR("Failed to initialize render image exporter worker");
        return -1;
    }
    return 0;
}

static int queueRenderImageWrite(
    RenderImageExporter* exporter,
    const char* path,
    uint16_t* rgba16,
    uint32_t width,
    uint32_t height
) {
    if (!path || !rgba16 || width == 0 || height == 0) {
        free(rgba16);
        return -1;
    }

    char* resolvedPath = NULL;
    RenderImageFormat format = RENDER_IMAGE_FORMAT_PNG;
    if (!resolveRenderImagePath(path, &resolvedPath, &format)) {
        free(rgba16);
        return -1;
    }

    if (ensureRenderImageWorkerStarted(exporter) != 0) {
        free(resolvedPath);
        free(rgba16);
        return -1;
    }

    RenderImageExportJob* job = (RenderImageExportJob*)calloc(1, sizeof(RenderImageExportJob));
    if (!job) {
        free(resolvedPath);
        free(rgba16);
        LOG_ERROR("Failed to allocate render image export job");
        return -1;
    }

    job->path = resolvedPath;
    job->rgba16 = rgba16;
    job->width = width;
    job->height = height;
    job->format = format;
    job->next = NULL;

    vkrtMutexLock(&exporter->workerLock);
    if (exporter->stop) {
        vkrtMutexUnlock(&exporter->workerLock);
        freeRenderImageExportJob(job);
        LOG_ERROR("Render image exporter is shutting down");
        return -1;
    }

    while (!exporter->stop && exporter->pendingJobCount >= kMaxPendingImageExportJobs) {
        vkrtCondWait(&exporter->workerCondition, &exporter->workerLock);
    }
    if (exporter->stop) {
        vkrtMutexUnlock(&exporter->workerLock);
        freeRenderImageExportJob(job);
        LOG_ERROR("Render image exporter is shutting down");
        return -1;
    }

    if (exporter->tail) {
        exporter->tail->next = job;
    } else {
        exporter->head = job;
    }
    exporter->tail = job;
    exporter->pendingJobCount++;
    vkrtCondSignal(&exporter->workerCondition);
    vkrtMutexUnlock(&exporter->workerLock);

    return 0;
}

void shutdownRenderImageExporter(VKRT* vkrt) {
    if (!vkrt) return;
    RenderImageExporter* exporter = &vkrt->renderImageExporter;

    if (!exporter->stateLockInitialized) return;

    vkrtMutexLock(&exporter->stateLock);
    if (!exporter->threadRunning) {
        vkrtMutexUnlock(&exporter->stateLock);
        return;
    }

    vkrtMutexLock(&exporter->workerLock);
    exporter->stop = 1;
    vkrtCondBroadcast(&exporter->workerCondition);
    vkrtMutexUnlock(&exporter->workerLock);

    VKRT_Thread workerThread = exporter->workerThread;
    exporter->threadRunning = 0;
    vkrtThreadJoin(workerThread, NULL);

    vkrtMutexLock(&exporter->workerLock);
    RenderImageExportJob* job = exporter->head;
    exporter->head = NULL;
    exporter->tail = NULL;
    exporter->pendingJobCount = 0u;
    exporter->stop = 0;
    vkrtMutexUnlock(&exporter->workerLock);
    vkrtMutexUnlock(&exporter->stateLock);

    while (job) {
        RenderImageExportJob* next = job->next;
        freeRenderImageExportJob(job);
        job = next;
    }

    if (exporter->primitivesInitialized) {
        vkrtCondDestroy(&exporter->workerCondition);
        vkrtMutexDestroy(&exporter->workerLock);
        exporter->primitivesInitialized = 0;
    }

    vkrtMutexDestroy(&exporter->stateLock);
    exporter->stateLockInitialized = 0;
}

int saveCurrentRenderImage(VKRT* vkrt, const char* path) {
    if (!vkrt || !path || !path[0]) return -1;
    if (vkrt->core.outputImage == VK_NULL_HANDLE) {
        LOG_ERROR("Cannot save render image before storage image is initialized");
        return -1;
    }

    uint32_t fullWidth = vkrt->runtime.renderExtent.width;
    uint32_t fullHeight = vkrt->runtime.renderExtent.height;
    if (fullWidth == 0 || fullHeight == 0) {
        fullWidth = vkrt->runtime.swapChainExtent.width;
        fullHeight = vkrt->runtime.swapChainExtent.height;
    }
    if (fullWidth == 0 || fullHeight == 0) {
        LOG_ERROR("Cannot save render image with invalid render size %ux%u", fullWidth, fullHeight);
        return -1;
    }

    uint32_t offsetX = 0;
    uint32_t offsetY = 0;
    uint32_t width = fullWidth;
    uint32_t height = fullHeight;

    VkDeviceSize pixelCount = (VkDeviceSize)width * (VkDeviceSize)height;
    VkDeviceSize readbackBytes = pixelCount * 4 * sizeof(uint16_t);
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    void* mapped = NULL;
    int result = -1;

    if (createBuffer(
        vkrt,
        readbackBytes,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &stagingBuffer,
        &stagingMemory
    ) != VKRT_SUCCESS) {
        return -1;
    }

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (beginSingleTimeCommands(vkrt, &commandBuffer) != VKRT_SUCCESS) {
        vkDestroyBuffer(vkrt->core.device, stagingBuffer, NULL);
        vkFreeMemory(vkrt->core.device, stagingMemory, NULL);
        return -1;
    }
    transitionImageLayout(commandBuffer, vkrt->core.outputImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    VkBufferImageCopy copyRegion = {0};
    copyRegion.bufferOffset = 0;
    copyRegion.bufferRowLength = 0;
    copyRegion.bufferImageHeight = 0;
    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.imageSubresource.mipLevel = 0;
    copyRegion.imageSubresource.baseArrayLayer = 0;
    copyRegion.imageSubresource.layerCount = 1;
    copyRegion.imageOffset = (VkOffset3D){(int32_t)offsetX, (int32_t)offsetY, 0};
    copyRegion.imageExtent = (VkExtent3D){width, height, 1};

    vkCmdCopyImageToBuffer(
        commandBuffer,
        vkrt->core.outputImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        stagingBuffer,
        1,
        &copyRegion
    );

    transitionImageLayout(commandBuffer, vkrt->core.outputImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    if (endSingleTimeCommands(vkrt, commandBuffer) != VKRT_SUCCESS) {
        vkDestroyBuffer(vkrt->core.device, stagingBuffer, NULL);
        vkFreeMemory(vkrt->core.device, stagingMemory, NULL);
        return -1;
    }

    if (vkMapMemory(vkrt->core.device, stagingMemory, 0, readbackBytes, 0, &mapped) == VK_SUCCESS) {
        uint16_t* readbackCopy = (uint16_t*)malloc((size_t)readbackBytes);
        if (!readbackCopy) {
            LOG_ERROR("Failed to allocate render snapshot buffer");
        } else {
            memcpy(readbackCopy, mapped, (size_t)readbackBytes);
            result = queueRenderImageWrite(&vkrt->renderImageExporter, path, readbackCopy, width, height);
            if (result == 0) {
                LOG_TRACE("Queued render image save: %s", path);
            } else {
                LOG_ERROR("Failed to queue render image export: %s", path);
            }
        }
        vkUnmapMemory(vkrt->core.device, stagingMemory);
    } else {
        LOG_ERROR("Failed to map render readback buffer");
    }

    vkDestroyBuffer(vkrt->core.device, stagingBuffer, NULL);
    vkFreeMemory(vkrt->core.device, stagingMemory, NULL);
    return result;
}
