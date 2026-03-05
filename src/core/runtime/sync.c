#include "sync.h"
#include "debug.h"

#include <stdlib.h>

VKRT_Result resetRenderFinishedSemaphores(VKRT* vkrt, size_t oldSemaphoreCount, size_t newSemaphoreCount) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    if (vkrt->runtime.renderFinishedSemaphores) {
        if (vkrt->core.device != VK_NULL_HANDLE) {
            for (size_t i = 0; i < oldSemaphoreCount; i++) {
                if (vkrt->runtime.renderFinishedSemaphores[i] != VK_NULL_HANDLE) {
                    vkDestroySemaphore(vkrt->core.device, vkrt->runtime.renderFinishedSemaphores[i], NULL);
                }
            }
        }
        free(vkrt->runtime.renderFinishedSemaphores);
        vkrt->runtime.renderFinishedSemaphores = NULL;
    }

    if (newSemaphoreCount == 0) return VKRT_SUCCESS;
    if (vkrt->core.device == VK_NULL_HANDLE) {
        LOG_ERROR("Cannot create render-finished semaphores without a valid Vulkan device");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    VkSemaphore* semaphores = (VkSemaphore*)calloc(newSemaphoreCount, sizeof(VkSemaphore));
    if (!semaphores) {
        LOG_ERROR("Failed to allocate render-finished semaphore list");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    VkSemaphoreCreateInfo semaphoreCreateInfo = {0};
    semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    size_t createdSemaphoreCount = 0;
    for (size_t i = 0; i < newSemaphoreCount; i++) {
        if (vkCreateSemaphore(vkrt->core.device, &semaphoreCreateInfo, NULL, &semaphores[i]) != VK_SUCCESS) {
            LOG_ERROR("Failed to create render-finished semaphore");
            for (size_t j = 0; j < createdSemaphoreCount; j++) {
                if (semaphores[j] != VK_NULL_HANDLE) {
                    vkDestroySemaphore(vkrt->core.device, semaphores[j], NULL);
                }
            }
            free(semaphores);
            return VKRT_ERROR_OPERATION_FAILED;
        }
        createdSemaphoreCount++;
    }

    vkrt->runtime.renderFinishedSemaphores = semaphores;
    return VKRT_SUCCESS;
}
