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

static const uint32_t kMaxPendingPNGJobs = 2u;

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

static void freePNGEncodeJob(PNGEncodeJob* job) {
    if (!job) return;
    free(job->path);
    free(job->rgba16);
    free(job);
}

static int pngWorkerMain(void* userData) {
    PNGExporter* exporter = (PNGExporter*)userData;

    for (;;) {
        vkrtMutexLock(&exporter->workerLock);
        while (!exporter->stop && exporter->head == NULL) {
            vkrtCondWait(&exporter->workerCondition, &exporter->workerLock);
        }

        if (exporter->stop && exporter->head == NULL) {
            vkrtMutexUnlock(&exporter->workerLock);
            break;
        }

        PNGEncodeJob* job = exporter->head;
        exporter->head = job->next;
        if (exporter->head == NULL) {
            exporter->tail = NULL;
        }
        vkrtMutexUnlock(&exporter->workerLock);

        int result = writePNGFile(job->path, job->rgba16, job->width, job->height);
        if (result == 0) {
            LOG_INFO("Saved render PNG: %s", job->path);
        }
        freePNGEncodeJob(job);

        vkrtMutexLock(&exporter->workerLock);
        if (exporter->pendingJobCount > 0u) {
            exporter->pendingJobCount--;
        }
        vkrtCondBroadcast(&exporter->workerCondition);
        vkrtMutexUnlock(&exporter->workerLock);
    }

    return 0;
}

static int initializePNGWorkerPrimitivesLocked(PNGExporter* exporter) {
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

static int startPNGWorkerLocked(PNGExporter* exporter) {
    if (exporter->threadRunning) return 0;
    if (initializePNGWorkerPrimitivesLocked(exporter) != 0) return -1;

    exporter->stop = 0;
    exporter->head = NULL;
    exporter->tail = NULL;
    exporter->pendingJobCount = 0u;

    if (vkrtThreadCreate(&exporter->workerThread, pngWorkerMain, exporter) != VKRT_THREAD_SUCCESS) {
        vkrtCondDestroy(&exporter->workerCondition);
        vkrtMutexDestroy(&exporter->workerLock);
        exporter->primitivesInitialized = 0;
        return -1;
    }
    exporter->threadRunning = 1;
    return 0;
}

static int ensurePNGWorkerStarted(PNGExporter* exporter) {
    if (!exporter->stateLockInitialized) {
        if (vkrtMutexInit(&exporter->stateLock, VKRT_MUTEX_PLAIN) != VKRT_THREAD_SUCCESS) {
            LOG_ERROR("Failed to initialize PNG exporter state lock");
            return -1;
        }
        exporter->stateLockInitialized = 1;
    }

    vkrtMutexLock(&exporter->stateLock);
    int result = startPNGWorkerLocked(exporter);
    vkrtMutexUnlock(&exporter->stateLock);

    if (result != 0) {
        LOG_ERROR("Failed to initialize PNG exporter worker");
        return -1;
    }
    return 0;
}

static int queuePNGWrite(PNGExporter* exporter, const char* path, uint16_t* rgba16, uint32_t width, uint32_t height) {
    if (!path || !rgba16 || width == 0 || height == 0) {
        free(rgba16);
        return -1;
    }

    if (ensurePNGWorkerStarted(exporter) != 0) {
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

    vkrtMutexLock(&exporter->workerLock);
    if (exporter->stop) {
        vkrtMutexUnlock(&exporter->workerLock);
        freePNGEncodeJob(job);
        LOG_ERROR("PNG exporter is shutting down");
        return -1;
    }

    while (!exporter->stop && exporter->pendingJobCount >= kMaxPendingPNGJobs) {
        vkrtCondWait(&exporter->workerCondition, &exporter->workerLock);
    }
    if (exporter->stop) {
        vkrtMutexUnlock(&exporter->workerLock);
        freePNGEncodeJob(job);
        LOG_ERROR("PNG exporter is shutting down");
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

void shutdownRenderPNGExporter(VKRT* vkrt) {
    if (!vkrt) return;
    PNGExporter* exporter = &vkrt->pngExporter;

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
    PNGEncodeJob* job = exporter->head;
    exporter->head = NULL;
    exporter->tail = NULL;
    exporter->pendingJobCount = 0u;
    exporter->stop = 0;
    vkrtMutexUnlock(&exporter->workerLock);
    vkrtMutexUnlock(&exporter->stateLock);

    while (job) {
        PNGEncodeJob* next = job->next;
        freePNGEncodeJob(job);
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
            result = queuePNGWrite(&vkrt->pngExporter, path, readbackCopy, width, height);
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
