#include "buffer.h"
#include "color.h"
#include "command/record.h"
#include "config.h"
#include "constants.h"
#include "scene.h"
#include "vkrt_internal.h"
#include "vkrt_types.h"
#include "vulkan/vulkan_core.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    K_AUTO_EXPOSURE_GRID_DIMENSION = 16u,
    K_AUTO_EXPOSURE_SAMPLE_COUNT = K_AUTO_EXPOSURE_GRID_DIMENSION * K_AUTO_EXPOSURE_GRID_DIMENSION,
};

static const float kAutoExposureKey = 0.18f;
static const float kAutoExposureSmoothing = 0.18f;
static const float kAutoExposureAdaptationStrength = 0.65f;

static float filterAutoExposureLuminance(float previousFiltered, float averageLuminance) {
    if (previousFiltered <= 0.0f) {
        return averageLuminance;
    }
    return (previousFiltered * (1.0f - kAutoExposureSmoothing)) + (averageLuminance * kAutoExposureSmoothing);
}

static int computeAutoExposureTarget(float filteredLuminance, float* outExposure) {
    if (!outExposure || !isfinite(filteredLuminance) || filteredLuminance <= 0.0f) return 0;

    float adaptedLuminance = powf(filteredLuminance, kAutoExposureAdaptationStrength);
    adaptedLuminance = fmaxf(adaptedLuminance, 1e-4f);
    float nextExposure = kAutoExposureKey / adaptedLuminance;
    if (!isfinite(nextExposure)) return 0;

    *outExposure = nextExposure;
    return 1;
}

static void clearAutoExposureReadback(VKRT_AutoExposureReadback* readback) {
    if (!readback) return;
    memset(readback, 0, sizeof(*readback));
}

static int computeAutoExposureAverageLuminance(const float* samples, float* outAverageLuminance) {
    if (!samples || !outAverageLuminance) return 0;

    float luminanceSum = 0.0f;
    uint32_t count = 0u;
    for (uint32_t i = 0; i < K_AUTO_EXPOSURE_SAMPLE_COUNT; i++) {
        float luminance = linearSRGBLuminance(&samples[(size_t)(i * 4u)]);
        if (!isfinite(luminance)) continue;
        luminanceSum += luminance;
        count++;
    }

    if (count == 0u) return 0;
    *outAverageLuminance = luminanceSum / (float)count;
    return isfinite(*outAverageLuminance) && *outAverageLuminance > 0.0f;
}

VKRT_Result createAutoExposureReadbacks(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    VkDeviceSize readbackBytes = (VkDeviceSize)K_AUTO_EXPOSURE_SAMPLE_COUNT * 4u * sizeof(float);
    for (uint32_t i = 0; i < VKRT_MAX_FRAMES_IN_FLIGHT; i++) {
        VKRT_AutoExposureReadback* readback = &vkrt->renderControl.autoExposure.readbacks[i];
        clearAutoExposureReadback(readback);

        VKRT_Result result = createBuffer(
            vkrt,
            readbackBytes,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &readback->buffer.buffer,
            &readback->buffer.memory
        );
        if (result != VKRT_SUCCESS) {
            destroyAutoExposureReadbacks(vkrt);
            return result;
        }

        if (vkMapMemory(
                vkrt->core.device,
                readback->buffer.memory,
                0,
                readbackBytes,
                0,
                (void**)&readback->mappedSamples
            ) != VK_SUCCESS ||
            !readback->mappedSamples) {
            destroyAutoExposureReadbacks(vkrt);
            return VKRT_ERROR_OPERATION_FAILED;
        }

        readback->buffer.deviceAddress = 0;
        readback->buffer.count = K_AUTO_EXPOSURE_SAMPLE_COUNT;
        readback->pending = 0u;
    }

    return VKRT_SUCCESS;
}

void destroyAutoExposureReadbacks(VKRT* vkrt) {
    if (!vkrt || vkrt->core.device == VK_NULL_HANDLE) return;

    for (uint32_t i = 0; i < VKRT_MAX_FRAMES_IN_FLIGHT; i++) {
        VKRT_AutoExposureReadback* readback = &vkrt->renderControl.autoExposure.readbacks[i];
        if (readback->mappedSamples && readback->buffer.memory != VK_NULL_HANDLE) {
            vkUnmapMemory(vkrt->core.device, readback->buffer.memory);
        }
        if (readback->buffer.buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(vkrt->core.device, readback->buffer.buffer, NULL);
        }
        if (readback->buffer.memory != VK_NULL_HANDLE) {
            vkFreeMemory(vkrt->core.device, readback->buffer.memory, NULL);
        }
        clearAutoExposureReadback(readback);
    }
}

void recordAutoExposureReadback(
    VKRT* vkrt,
    VkCommandBuffer commandBuffer,
    VkImage accumulationImage,
    VkExtent2D renderExtent
) {
    if (!vkrt || commandBuffer == VK_NULL_HANDLE || accumulationImage == VK_NULL_HANDLE) return;
    if (!vkrt->sceneSettings.autoExposureEnabled || vkrt->sceneSettings.debugMode != VKRT_DEBUG_MODE_NONE) return;
    if (renderExtent.width == 0u || renderExtent.height == 0u) return;

    VKRT_AutoExposureReadback* readback = &vkrt->renderControl.autoExposure.readbacks[vkrt->runtime.currentFrame];
    if (readback->buffer.buffer == VK_NULL_HANDLE) return;

    transitionImageLayout(
        commandBuffer,
        accumulationImage,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
    );

    VkBufferImageCopy copyRegions[K_AUTO_EXPOSURE_SAMPLE_COUNT];
    uint32_t regionIndex = 0u;
    for (uint32_t y = 0; y < K_AUTO_EXPOSURE_GRID_DIMENSION; y++) {
        for (uint32_t x = 0; x < K_AUTO_EXPOSURE_GRID_DIMENSION; x++) {
            uint32_t sampleX = (((2u * x) + 1u) * renderExtent.width) / (2u * K_AUTO_EXPOSURE_GRID_DIMENSION);
            uint32_t sampleY = (((2u * y) + 1u) * renderExtent.height) / (2u * K_AUTO_EXPOSURE_GRID_DIMENSION);
            if (sampleX >= renderExtent.width) sampleX = renderExtent.width - 1u;
            if (sampleY >= renderExtent.height) sampleY = renderExtent.height - 1u;

            copyRegions[regionIndex] = (VkBufferImageCopy){
                .bufferOffset = (VkDeviceSize)regionIndex * 4u * sizeof(float),
                .bufferRowLength = 0,
                .bufferImageHeight = 0,
                .imageSubresource =
                    {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .mipLevel = 0,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
                .imageOffset = {(int32_t)sampleX, (int32_t)sampleY, 0},
                .imageExtent = {1u, 1u, 1u},
            };
            regionIndex++;
        }
    }

    vkCmdCopyImageToBuffer(
        commandBuffer,
        accumulationImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        readback->buffer.buffer,
        K_AUTO_EXPOSURE_SAMPLE_COUNT,
        copyRegions
    );

    transitionImageLayout(
        commandBuffer,
        accumulationImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_GENERAL
    );
    readback->pending = 1u;
}

void resolveAutoExposureReadback(VKRT* vkrt, uint32_t frameIndex) {
    if (!vkrt || frameIndex >= VKRT_MAX_FRAMES_IN_FLIGHT) return;

    VKRT_AutoExposureReadback* readback = &vkrt->renderControl.autoExposure.readbacks[frameIndex];
    if (!readback->pending) return;
    readback->pending = 0u;

    if (!vkrt->sceneSettings.autoExposureEnabled || vkrt->sceneSettings.debugMode != VKRT_DEBUG_MODE_NONE) {
        return;
    }

    float averageLuminance = 0.0f;
    if (!computeAutoExposureAverageLuminance(readback->mappedSamples, &averageLuminance)) {
        return;
    }

    vkrt->renderControl.autoExposure.filteredLuminance =
        filterAutoExposureLuminance(vkrt->renderControl.autoExposure.filteredLuminance, averageLuminance);
    if (!isfinite(vkrt->renderControl.autoExposure.filteredLuminance) ||
        vkrt->renderControl.autoExposure.filteredLuminance <= 0.0f) {
        return;
    }

    float nextExposure = 0.0f;
    if (!computeAutoExposureTarget(vkrt->renderControl.autoExposure.filteredLuminance, &nextExposure) ||
        fabsf(vkrt->sceneSettings.exposure - nextExposure) < 1e-4f) {
        return;
    }

    vkrt->sceneSettings.exposure = nextExposure;
    syncSceneStateData(vkrt);
}
