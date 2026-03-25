#include "export.h"

#include "buffer.h"
#include "command/pool.h"
#include "command/record.h"
#include "debug.h"
#include "exr.h"
#include "io.h"
#include "platform.h"

#include <spng.h>
#include <turbojpeg.h>

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
    RENDER_IMAGE_FORMAT_EXR,
} RenderImageFormat;

typedef struct RenderImageExportJob {
    struct RenderImageExportJob* next;
    char* path;
    void* pixels;
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
    if (equalsIgnoreCaseASCII(extension, ".exr")) {
        *outFormat = RENDER_IMAGE_FORMAT_EXR;
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

static int openBinaryFileForWrite(const char* path, FILE** outFile) {
    if (!path || !path[0] || !outFile) return 0;

    FILE* file = NULL;
#if defined(_WIN32)
    if (fopen_s(&file, path, "wb") != 0) file = NULL;
#else
    file = fopen(path, "wb");
#endif
    if (!file) {
        LOG_ERROR("Failed to open export file: %s", path);
        return 0;
    }

    *outFile = file;
    return 1;
}

static int writeFileBytes(FILE* file, const void* data, size_t size) {
    if (!file || (!data && size != 0u)) return 0;
    return fwrite(data, 1u, size, file) == size;
}

static int writePNGFile(const char* path, const uint16_t* rgba16, uint32_t width, uint32_t height) {
    size_t rgba16ByteCount = 0u;
    if (!tryComputeImageByteCount(width, height, 8u, &rgba16ByteCount)) {
        LOG_ERROR("PNG export size overflow for '%s'", path);
        return 0;
    }

    FILE* file = NULL;
    if (!openBinaryFileForWrite(path, &file)) {
        return 0;
    }

    spng_ctx* context = spng_ctx_new(SPNG_CTX_ENCODER);
    if (!context) {
        fclose(file);
        LOG_ERROR("PNG encoder initialization failed for '%s'", path);
        return 0;
    }

    int error = spng_set_png_file(context, file);
    if (error == 0) {
        struct spng_ihdr header = {
            .width = width,
            .height = height,
            .bit_depth = 16u,
            .color_type = SPNG_COLOR_TYPE_TRUECOLOR_ALPHA,
            .compression_method = 0u,
            .filter_method = 0u,
            .interlace_method = SPNG_INTERLACE_NONE,
        };
        error = spng_set_ihdr(context, &header);
    }
    if (error == 0) {
        error = spng_encode_image(context, rgba16, rgba16ByteCount, SPNG_FMT_PNG, SPNG_ENCODE_FINALIZE);
    }

    spng_ctx_free(context);
    fclose(file);

    if (error != 0) {
        LOG_ERROR("PNG export failed for '%s' (%s)", path, spng_strerror(error));
        return 0;
    }

    return 1;
}

static int writeJPEGFile(const char* path, const uint8_t* rgba8, uint32_t width, uint32_t height) {
    if (width > (uint32_t)INT_MAX || height > (uint32_t)INT_MAX) {
        LOG_ERROR("JPEG export dimensions exceed codec limits: %ux%u", width, height);
        return 0;
    }

    tjhandle handle = tjInitCompress();
    if (!handle) {
        LOG_ERROR("JPEG encoder initialization failed for '%s' (%s)", path, tjGetErrorStr());
        return 0;
    }

    unsigned char* encodedBytes = NULL;
    unsigned long encodedSize = 0ul;
    if (tjCompress2(
        handle,
        rgba8,
        (int)width,
        0,
        (int)height,
        TJPF_RGBA,
        &encodedBytes,
        &encodedSize,
        TJSAMP_444,
        kJPEGQuality,
        TJFLAG_ACCURATEDCT
    ) != 0) {
        LOG_ERROR("JPEG export failed for '%s' (%s)", path, tjGetErrorStr2(handle));
        tjDestroy(handle);
        return 0;
    }

    FILE* file = NULL;
    int writeOk = openBinaryFileForWrite(path, &file) && writeFileBytes(file, encodedBytes, (size_t)encodedSize);
    if (file) fclose(file);

    tjFree(encodedBytes);
    tjDestroy(handle);

    if (!writeOk) {
        LOG_ERROR("Failed to write JPEG export file: %s", path);
        return 0;
    }

    return 1;
}

static int writeRenderImageFile(
    const char* path,
    const void* pixels,
    uint32_t width,
    uint32_t height,
    RenderImageFormat format
) {
    if (!path || !pixels || width == 0u || height == 0u) return -1;
    if (width > (uint32_t)INT32_MAX || height > (uint32_t)INT32_MAX) {
        LOG_ERROR("Image export dimensions exceed codec limits: %ux%u", width, height);
        return -1;
    }

    if (format == RENDER_IMAGE_FORMAT_EXR) {
        if (!vkrtWriteEXRFromRGBA32F(path, (const float*)pixels, width, height)) {
            remove(path);
            LOG_ERROR("Render export failed for '%s'", path);
            return -1;
        }
        return 0;
    }

    const uint16_t* rgba16 = (const uint16_t*)pixels;
    if (format == RENDER_IMAGE_FORMAT_PNG) {
        if (!writePNGFile(path, rgba16, width, height)) {
            remove(path);
            LOG_ERROR("Render export failed for '%s'", path);
            return -1;
        }
        return 0;
    }
    if (format != RENDER_IMAGE_FORMAT_JPEG) {
        LOG_ERROR("Unsupported render export format for '%s'", path);
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

    int writeOk = writeJPEGFile(path, rgba8, width, height);
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
    free(job->pixels);
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

        int result = writeRenderImageFile(job->path, job->pixels, job->width, job->height, job->format);
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
    char* resolvedPath,
    void* pixels,
    uint32_t width,
    uint32_t height,
    RenderImageFormat format
) {
    if (!resolvedPath || !resolvedPath[0] || !pixels || width == 0 || height == 0) {
        free(resolvedPath);
        free(pixels);
        return -1;
    }
    if (format < RENDER_IMAGE_FORMAT_PNG || format > RENDER_IMAGE_FORMAT_EXR) {
        free(resolvedPath);
        free(pixels);
        return -1;
    }

    if (ensureRenderImageWorkerStarted(exporter) != 0) {
        free(resolvedPath);
        free(pixels);
        return -1;
    }

    RenderImageExportJob* job = (RenderImageExportJob*)calloc(1, sizeof(RenderImageExportJob));
    if (!job) {
        free(resolvedPath);
        free(pixels);
        LOG_ERROR("Failed to allocate render image export job");
        return -1;
    }

    job->path = resolvedPath;
    job->pixels = pixels;
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
    char* resolvedPath = NULL;
    RenderImageFormat requestedFormat = RENDER_IMAGE_FORMAT_PNG;
    if (!resolveRenderImagePath(path, &resolvedPath, &requestedFormat)) {
        return -1;
    }

    VkImage sourceImage = requestedFormat == RENDER_IMAGE_FORMAT_EXR
        ? vkrt->core.accumulationImages[vkrt->core.accumulationReadIndex]
        : vkrt->core.outputImage;
    if (sourceImage == VK_NULL_HANDLE) {
        free(resolvedPath);
        LOG_ERROR("Cannot save render image before the source image is initialized");
        return -1;
    }

    uint32_t fullWidth = vkrt->runtime.renderExtent.width;
    uint32_t fullHeight = vkrt->runtime.renderExtent.height;
    if (fullWidth == 0 || fullHeight == 0) {
        fullWidth = vkrt->runtime.swapChainExtent.width;
        fullHeight = vkrt->runtime.swapChainExtent.height;
    }
    if (fullWidth == 0 || fullHeight == 0) {
        free(resolvedPath);
        LOG_ERROR("Cannot save render image with invalid render size %ux%u", fullWidth, fullHeight);
        return -1;
    }

    uint32_t offsetX = 0;
    uint32_t offsetY = 0;
    uint32_t width = fullWidth;
    uint32_t height = fullHeight;

    VkDeviceSize pixelCount = (VkDeviceSize)width * (VkDeviceSize)height;
    VkDeviceSize readbackBytes = pixelCount * 4u *
        (requestedFormat == RENDER_IMAGE_FORMAT_EXR ? sizeof(float) : sizeof(uint16_t));
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
        free(resolvedPath);
        return -1;
    }

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (beginSingleTimeCommands(vkrt, &commandBuffer) != VKRT_SUCCESS) {
        free(resolvedPath);
        vkDestroyBuffer(vkrt->core.device, stagingBuffer, NULL);
        vkFreeMemory(vkrt->core.device, stagingMemory, NULL);
        return -1;
    }
    transitionImageLayout(commandBuffer, sourceImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

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
        sourceImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        stagingBuffer,
        1,
        &copyRegion
    );

    transitionImageLayout(commandBuffer, sourceImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    if (endSingleTimeCommands(vkrt, commandBuffer) != VKRT_SUCCESS) {
        free(resolvedPath);
        vkDestroyBuffer(vkrt->core.device, stagingBuffer, NULL);
        vkFreeMemory(vkrt->core.device, stagingMemory, NULL);
        return -1;
    }

    if (vkMapMemory(vkrt->core.device, stagingMemory, 0, readbackBytes, 0, &mapped) == VK_SUCCESS) {
        void* readbackCopy = malloc((size_t)readbackBytes);
        if (!readbackCopy) {
            LOG_ERROR("Failed to allocate render snapshot buffer");
        } else {
            memcpy(readbackCopy, mapped, (size_t)readbackBytes);
            result = queueRenderImageWrite(
                &vkrt->renderImageExporter,
                resolvedPath,
                readbackCopy,
                width,
                height,
                requestedFormat
            );
            if (result == 0) {
                LOG_TRACE("Queued render image save: %s", resolvedPath);
                resolvedPath = NULL;
            } else {
                LOG_ERROR("Failed to queue render image export: %s", resolvedPath);
            }
        }
        vkUnmapMemory(vkrt->core.device, stagingMemory);
    } else {
        LOG_ERROR("Failed to map render readback buffer");
    }

    vkDestroyBuffer(vkrt->core.device, stagingBuffer, NULL);
    vkFreeMemory(vkrt->core.device, stagingMemory, NULL);
    free(resolvedPath);
    return result;
}
