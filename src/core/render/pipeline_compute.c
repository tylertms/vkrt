#include "config.h"
#include "debug.h"
#include "pipeline.h"
#include "pipeline_internal.h"
#include "platform.h"
#include "shaders.h"
#include "sync.h"
#include "vkrt_internal.h"
#include "vkrt_types.h"

#include <stddef.h>
#include <stdint.h>
#include <vulkan/vulkan_core.h>

VKRT_Result createComputePipeline(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    uint64_t startTime = getMicroseconds();
    VkShaderModule compModule = VK_NULL_HANDLE;
    if (createShaderModule(vkrt, shaderCompData, shaderCompSize, &compModule) != VKRT_SUCCESS) {
        return VKRT_ERROR_OPERATION_FAILED;
    }

    VkPipelineShaderStageCreateInfo stageInfo = makePipelineShaderStageInfo(VK_SHADER_STAGE_COMPUTE_BIT, compModule);
    VkComputePipelineCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = stageInfo,
        .layout = vkrt->core.pipelineLayout,
    };

    if (vkCreateComputePipelines(vkrt->core.device, VK_NULL_HANDLE, 1, &createInfo, NULL, &vkrt->core.computePipeline) !=
        VK_SUCCESS) {
        LOG_ERROR("Failed to create compute pipeline");
        vkDestroyShaderModule(vkrt->core.device, compModule, NULL);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    vkDestroyShaderModule(vkrt->core.device, compModule, NULL);
    LOG_INFO("Compute pipeline created in %.3f ms", (double)(getMicroseconds() - startTime) / 1e3);
    return VKRT_SUCCESS;
}

VKRT_Result createSyncObjects(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    VkSemaphoreCreateInfo semaphoreCreateInfo = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fenceInfo = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    for (size_t i = 0; i < VKRT_MAX_FRAMES_IN_FLIGHT; i++) {
        vkrt->runtime.imageAvailableSemaphores[i] = VK_NULL_HANDLE;
        vkrt->runtime.inFlightFences[i] = VK_NULL_HANDLE;
    }

    size_t createdImageAvailableCount = 0;
    size_t createdFenceCount = 0;
    for (size_t i = 0; i < VKRT_MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(
                vkrt->core.device,
                &semaphoreCreateInfo,
                NULL,
                &vkrt->runtime.imageAvailableSemaphores[i]
            ) != VK_SUCCESS) {
            LOG_ERROR("Failed to create image-available semaphore");
            goto create_sync_objects_failed;
        }
        createdImageAvailableCount++;

        if (vkCreateFence(vkrt->core.device, &fenceInfo, NULL, &vkrt->runtime.inFlightFences[i]) != VK_SUCCESS) {
            LOG_ERROR("Failed to create in-flight fence");
            goto create_sync_objects_failed;
        }
        createdFenceCount++;
    }

    if (resetRenderFinishedSemaphores(vkrt, vkrt->runtime.swapChainImageCount, vkrt->runtime.swapChainImageCount) !=
        VKRT_SUCCESS) {
        goto create_sync_objects_failed;
    }

    return VKRT_SUCCESS;

create_sync_objects_failed:
    for (size_t i = 0; i < createdImageAvailableCount; i++) {
        if (vkrt->runtime.imageAvailableSemaphores[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(vkrt->core.device, vkrt->runtime.imageAvailableSemaphores[i], NULL);
            vkrt->runtime.imageAvailableSemaphores[i] = VK_NULL_HANDLE;
        }
    }
    for (size_t i = 0; i < createdFenceCount; i++) {
        if (vkrt->runtime.inFlightFences[i] != VK_NULL_HANDLE) {
            vkDestroyFence(vkrt->core.device, vkrt->runtime.inFlightFences[i], NULL);
            vkrt->runtime.inFlightFences[i] = VK_NULL_HANDLE;
        }
    }
    resetRenderFinishedSemaphores(vkrt, vkrt->runtime.swapChainImageCount, 0);
    return VKRT_ERROR_OPERATION_FAILED;
}
