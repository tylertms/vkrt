#include "export.h"

#include "buffer.h"
#include "command.h"
#include "debug.h"

#define SPNG_STATIC
#include "spng.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

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

static char* duplicateString(const char* value) {
    if (!value) return NULL;
    size_t length = strlen(value);
    char* out = (char*)malloc(length + 1);
    if (!out) return NULL;
    memcpy(out, value, length + 1);
    return out;
}

static pthread_once_t gPNGWorkerInitOnce = PTHREAD_ONCE_INIT;
static pthread_mutex_t gPNGWorkerLock;
static pthread_cond_t gPNGWorkerCondition;
static pthread_t gPNGWorkerThread;
static PNGEncodeJob* gPNGWorkerHead = NULL;
static PNGEncodeJob* gPNGWorkerTail = NULL;
static int gPNGWorkerStop = 0;
static int gPNGWorkerInitialized = 0;
static int gPNGWorkerInitFailed = 0;

static void freePNGEncodeJob(PNGEncodeJob* job) {
    if (!job) return;
    free(job->path);
    free(job->rgba16);
    free(job);
}

static void* pngWorkerMain(void* userData) {
    (void)userData;

    for (;;) {
        pthread_mutex_lock(&gPNGWorkerLock);
        while (!gPNGWorkerStop && gPNGWorkerHead == NULL) {
            pthread_cond_wait(&gPNGWorkerCondition, &gPNGWorkerLock);
        }

        if (gPNGWorkerStop && gPNGWorkerHead == NULL) {
            pthread_mutex_unlock(&gPNGWorkerLock);
            break;
        }

        PNGEncodeJob* job = gPNGWorkerHead;
        gPNGWorkerHead = job->next;
        if (gPNGWorkerHead == NULL) {
            gPNGWorkerTail = NULL;
        }
        pthread_mutex_unlock(&gPNGWorkerLock);

        int result = writePNGFile(job->path, job->rgba16, job->width, job->height);
        if (result == 0) {
            LOG_INFO("Saved render PNG: %s", job->path);
        }
        freePNGEncodeJob(job);
    }

    return NULL;
}

static void initializePNGWorkerOnce(void) {
    if (pthread_mutex_init(&gPNGWorkerLock, NULL) != 0) {
        gPNGWorkerInitFailed = 1;
        return;
    }
    if (pthread_cond_init(&gPNGWorkerCondition, NULL) != 0) {
        pthread_mutex_destroy(&gPNGWorkerLock);
        gPNGWorkerInitFailed = 1;
        return;
    }
    gPNGWorkerStop = 0;
    gPNGWorkerHead = NULL;
    gPNGWorkerTail = NULL;

    if (pthread_create(&gPNGWorkerThread, NULL, pngWorkerMain, NULL) != 0) {
        pthread_cond_destroy(&gPNGWorkerCondition);
        pthread_mutex_destroy(&gPNGWorkerLock);
        gPNGWorkerInitFailed = 1;
        return;
    }

    gPNGWorkerInitialized = 1;
}

static int ensurePNGWorkerStarted(void) {
    pthread_once(&gPNGWorkerInitOnce, initializePNGWorkerOnce);
    if (!gPNGWorkerInitialized || gPNGWorkerInitFailed) {
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

    job->path = duplicateString(path);
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

    pthread_mutex_lock(&gPNGWorkerLock);
    if (gPNGWorkerStop) {
        pthread_mutex_unlock(&gPNGWorkerLock);
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
    pthread_cond_signal(&gPNGWorkerCondition);
    pthread_mutex_unlock(&gPNGWorkerLock);

    return 0;
}

void shutdownRenderPNGExporter(void) {
    if (!gPNGWorkerInitialized) return;

    pthread_mutex_lock(&gPNGWorkerLock);
    gPNGWorkerStop = 1;
    pthread_cond_broadcast(&gPNGWorkerCondition);
    pthread_mutex_unlock(&gPNGWorkerLock);

    pthread_join(gPNGWorkerThread, NULL);

    pthread_mutex_lock(&gPNGWorkerLock);
    PNGEncodeJob* job = gPNGWorkerHead;
    gPNGWorkerHead = NULL;
    gPNGWorkerTail = NULL;
    pthread_mutex_unlock(&gPNGWorkerLock);

    while (job) {
        PNGEncodeJob* next = job->next;
        freePNGEncodeJob(job);
        job = next;
    }

    pthread_cond_destroy(&gPNGWorkerCondition);
    pthread_mutex_destroy(&gPNGWorkerLock);
    gPNGWorkerInitialized = 0;
}

int saveCurrentRenderPNG(VKRT* vkrt, const char* path) {
    if (!vkrt || !path || !path[0]) return -1;
    if (vkrt->core.storageImage == VK_NULL_HANDLE) {
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

    createBuffer(vkrt,
        readbackBytes,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &stagingBuffer,
        &stagingMemory);

    VkCommandBuffer commandBuffer = beginSingleTimeCommands(vkrt);
    transitionImageLayout(commandBuffer, vkrt->core.storageImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

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

    vkCmdCopyImageToBuffer(commandBuffer,
        vkrt->core.storageImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        stagingBuffer,
        1,
        &copyRegion);

    transitionImageLayout(commandBuffer, vkrt->core.storageImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    endSingleTimeCommands(vkrt, commandBuffer);

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
