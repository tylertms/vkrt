#include "export.h"

#include "buffer.h"
#include "command/pool.h"
#include "command/record.h"
#include "debug.h"
#include "io.h"
#include "platform.h"

#define SPNG_STATIC
#include "spng.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct PNGEncodeJob {
    struct PNGEncodeJob* next;
    char* path;
    uint16_t* rgba16;
    uint32_t width;
    uint32_t height;
} PNGEncodeJob;

static int writePNGFile(const char* path, const uint16_t* rgba16, uint32_t width, uint32_t height) {
    FILE* file = fopen(path, "wb");
    if (!file) {
        LOG_ERROR("Failed to open '%s' for PNG export", path);
        return -1;
    }

    spng_ctx* context = spng_ctx_new(SPNG_CTX_ENCODER);
    if (!context) {
        fclose(file);
        LOG_ERROR("Failed to create PNG encoder context");
        return -1;
    }

    struct spng_ihdr ihdr = {0};
    ihdr.width = width;
    ihdr.height = height;
    ihdr.bit_depth = 16;
    ihdr.color_type = SPNG_COLOR_TYPE_TRUECOLOR_ALPHA;
    ihdr.compression_method = 0;
    ihdr.filter_method = 0;
    ihdr.interlace_method = SPNG_INTERLACE_NONE;

    int err = spng_set_png_file(context, file);
    if (err == SPNG_OK) err = spng_set_ihdr(context, &ihdr);
    if (err == SPNG_OK) err = spng_set_option(context, SPNG_FILTER_CHOICE, SPNG_FILTER_CHOICE_ALL);
    if (err == SPNG_OK) {
        err = spng_encode_image(context, rgba16, (size_t)width * height * 4 * sizeof(uint16_t), SPNG_FMT_PNG, SPNG_ENCODE_FINALIZE);
    }

    spng_ctx_free(context);
    fclose(file);

    if (err != SPNG_OK) {
        remove(path);
        LOG_ERROR("PNG export failed for '%s': %s", path, spng_strerror(err));
        return -1;
    }

    return 0;
}

static VKRT_Mutex gPNGWorkerStateLock;
static VKRT_Mutex gPNGWorkerLock;
static VKRT_Cond gPNGWorkerCondition;
static VKRT_Thread gPNGWorkerThread;
static PNGEncodeJob* gPNGWorkerHead = NULL;
static PNGEncodeJob* gPNGWorkerTail = NULL;
static int gPNGWorkerStop = 0;
static int gPNGWorkerPrimitivesInitialized = 0;
static int gPNGWorkerThreadRunning = 0;
static int gPNGWorkerStateLockInitialized = 0;

static void freePNGEncodeJob(PNGEncodeJob* job) {
    if (!job) return;
    free(job->path);
    free(job->rgba16);
    free(job);
}

static int pngWorkerMain(void* userData) {
    (void)userData;

    for (;;) {
        vkrtMutexLock(&gPNGWorkerLock);
        while (!gPNGWorkerStop && gPNGWorkerHead == NULL) {
            vkrtCondWait(&gPNGWorkerCondition, &gPNGWorkerLock);
        }

        if (gPNGWorkerStop && gPNGWorkerHead == NULL) {
            vkrtMutexUnlock(&gPNGWorkerLock);
            break;
        }

        PNGEncodeJob* job = gPNGWorkerHead;
        gPNGWorkerHead = job->next;
        if (gPNGWorkerHead == NULL) {
            gPNGWorkerTail = NULL;
        }
        vkrtMutexUnlock(&gPNGWorkerLock);

        int result = writePNGFile(job->path, job->rgba16, job->width, job->height);
        if (result == 0) {
            LOG_INFO("Saved render PNG: %s", job->path);
        }
        freePNGEncodeJob(job);
    }

    return 0;
}

static int initializePNGWorkerPrimitivesLocked(void) {
    if (gPNGWorkerPrimitivesInitialized) return 0;

    if (vkrtMutexInit(&gPNGWorkerLock, VKRT_MUTEX_PLAIN) != VKRT_THREAD_SUCCESS) {
        return -1;
    }
    if (vkrtCondInit(&gPNGWorkerCondition) != VKRT_THREAD_SUCCESS) {
        vkrtMutexDestroy(&gPNGWorkerLock);
        return -1;
    }
    gPNGWorkerPrimitivesInitialized = 1;
    return 0;
}

static int startPNGWorkerLocked(void) {
    if (gPNGWorkerThreadRunning) return 0;
    if (initializePNGWorkerPrimitivesLocked() != 0) return -1;

    gPNGWorkerStop = 0;
    gPNGWorkerHead = NULL;
    gPNGWorkerTail = NULL;

    if (vkrtThreadCreate(&gPNGWorkerThread, pngWorkerMain, NULL) != VKRT_THREAD_SUCCESS) {
        return -1;
    }
    gPNGWorkerThreadRunning = 1;
    return 0;
}

static int ensurePNGWorkerStarted(void) {
    if (!gPNGWorkerStateLockInitialized) {
        if (vkrtMutexInit(&gPNGWorkerStateLock, VKRT_MUTEX_PLAIN) != VKRT_THREAD_SUCCESS) {
            LOG_ERROR("Failed to initialize PNG exporter state lock");
            return -1;
        }
        gPNGWorkerStateLockInitialized = 1;
    }

    vkrtMutexLock(&gPNGWorkerStateLock);
    int result = startPNGWorkerLocked();
    vkrtMutexUnlock(&gPNGWorkerStateLock);

    if (result != 0) {
        LOG_ERROR("Failed to initialize PNG exporter worker");
        return -1;
    }
    return 0;
}

static int queuePNGWrite(const char* path, uint16_t* rgba16, uint32_t width, uint32_t height) {
    if (!path || !rgba16 || width == 0 || height == 0) {
        free(rgba16);
        return -1;
    }

    if (ensurePNGWorkerStarted() != 0) {
        free(rgba16);
        return -1;
    }

    PNGEncodeJob* job = (PNGEncodeJob*)calloc(1, sizeof(PNGEncodeJob));
    if (!job) {
        free(rgba16);
        LOG_ERROR("Failed to allocate PNG export job");
        return -1;
    }

    job->path = stringDuplicate(path);
    if (!job->path) {
        free(rgba16);
        free(job);
        LOG_ERROR("Failed to allocate PNG export path");
        return -1;
    }

    job->rgba16 = rgba16;
    job->width = width;
    job->height = height;
    job->next = NULL;

    vkrtMutexLock(&gPNGWorkerLock);
    if (gPNGWorkerStop) {
        vkrtMutexUnlock(&gPNGWorkerLock);
        freePNGEncodeJob(job);
        LOG_ERROR("PNG exporter is shutting down");
        return -1;
    }

    if (gPNGWorkerTail) {
        gPNGWorkerTail->next = job;
    } else {
        gPNGWorkerHead = job;
    }
    gPNGWorkerTail = job;
    vkrtCondSignal(&gPNGWorkerCondition);
    vkrtMutexUnlock(&gPNGWorkerLock);

    return 0;
}

void shutdownRenderPNGExporter(void) {
    if (!gPNGWorkerStateLockInitialized) return;

    vkrtMutexLock(&gPNGWorkerStateLock);
    if (!gPNGWorkerThreadRunning) {
        vkrtMutexUnlock(&gPNGWorkerStateLock);
        return;
    }

    vkrtMutexLock(&gPNGWorkerLock);
    gPNGWorkerStop = 1;
    vkrtCondBroadcast(&gPNGWorkerCondition);
    vkrtMutexUnlock(&gPNGWorkerLock);

    VKRT_Thread workerThread = gPNGWorkerThread;
    gPNGWorkerThreadRunning = 0;
    vkrtThreadJoin(workerThread, NULL);

    vkrtMutexLock(&gPNGWorkerLock);
    PNGEncodeJob* job = gPNGWorkerHead;
    gPNGWorkerHead = NULL;
    gPNGWorkerTail = NULL;
    gPNGWorkerStop = 0;
    vkrtMutexUnlock(&gPNGWorkerLock);
    vkrtMutexUnlock(&gPNGWorkerStateLock);

    while (job) {
        PNGEncodeJob* next = job->next;
        freePNGEncodeJob(job);
        job = next;
    }

    if (gPNGWorkerPrimitivesInitialized) {
        vkrtCondDestroy(&gPNGWorkerCondition);
        vkrtMutexDestroy(&gPNGWorkerLock);
        gPNGWorkerPrimitivesInitialized = 0;
    }

    vkrtMutexDestroy(&gPNGWorkerStateLock);
    gPNGWorkerStateLockInitialized = 0;

}

int saveCurrentRenderPNG(VKRT* vkrt, const char* path) {
    if (!vkrt || !path || !path[0]) return -1;
    if (vkrt->core.outputImage == VK_NULL_HANDLE) {
        LOG_ERROR("Cannot save PNG before storage image is initialized");
        return -1;
    }

    uint32_t fullWidth = vkrt->runtime.renderExtent.width;
    uint32_t fullHeight = vkrt->runtime.renderExtent.height;
    if (fullWidth == 0 || fullHeight == 0) {
        fullWidth = vkrt->runtime.swapChainExtent.width;
        fullHeight = vkrt->runtime.swapChainExtent.height;
    }
    if (fullWidth == 0 || fullHeight == 0) {
        LOG_ERROR("Cannot save PNG with invalid render size %ux%u", fullWidth, fullHeight);
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
            LOG_ERROR("Failed to allocate PNG snapshot buffer");
        } else {
            memcpy(readbackCopy, mapped, (size_t)readbackBytes);
            result = queuePNGWrite(path, readbackCopy, width, height);
            if (result == 0) {
                LOG_TRACE("Queued render PNG save: %s", path);
            } else {
                LOG_ERROR("Failed to queue PNG export: %s", path);
            }
        }
        vkUnmapMemory(vkrt->core.device, stagingMemory);
    } else {
        LOG_ERROR("Failed to map PNG readback buffer");
    }

    if (stagingBuffer != VK_NULL_HANDLE) vkDestroyBuffer(vkrt->core.device, stagingBuffer, NULL);
    if (stagingMemory != VK_NULL_HANDLE) vkFreeMemory(vkrt->core.device, stagingMemory, NULL);
    return result;
}
