#include "command/record.h"
#include "shared.h"
#include "descriptor.h"
#include "scene.h"
#include "swapchain.h"
#include "accel/accel.h"
#include "vkrt_internal.h"
#include "debug.h"

#include <stdint.h>
#include <stdlib.h>

static void invalidateDescriptorSets(VKRT* vkrt) {
    if (!vkrt) return;
    for (uint32_t i = 0; i < VKRT_MAX_FRAMES_IN_FLIGHT; i++) {
        vkrt->core.descriptorSetReady[i] = VK_FALSE;
    }
}

VKRT_Result VKRT_beginFrame(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    vkrt->runtime.frameAcquired = VK_FALSE;
    vkrt->runtime.frameSubmitted = VK_FALSE;
    vkrt->runtime.framePresented = VK_FALSE;
    vkrt->runtime.frameTraced = VK_FALSE;

    vkWaitForFences(vkrt->core.device, 1, &vkrt->runtime.inFlightFences[vkrt->runtime.currentFrame], VK_TRUE, UINT64_MAX);
    cleanupFrameSceneUpdate(vkrt, vkrt->runtime.currentFrame);
    recordFrameTime(vkrt, vkrt->runtime.currentFrame);

    VkResult result = vkAcquireNextImageKHR(
        vkrt->core.device,
        vkrt->runtime.swapChain,
        UINT64_MAX,
        vkrt->runtime.imageAvailableSemaphores[vkrt->runtime.currentFrame],
        VK_NULL_HANDLE,
        &vkrt->runtime.frameImageIndex
    );

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        return recreateSwapChain(vkrt);
    }

    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        LOG_ERROR("Failed to acquire next swapchain image");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    vkrt->runtime.frameAcquired = VK_TRUE;
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_updateScene(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    if (!vkrt->runtime.frameAcquired) return VKRT_SUCCESS;

    VkBool32 materialDirty = vkrt->core.materialResourceRevision != vkrt->core.materialRevision;
    VkBool32 sceneDirty = vkrt->core.sceneResourceRevision != vkrt->core.sceneRevision;
    VkBool32 lightDirty = vkrt->core.lightResourceRevision != vkrt->core.lightRevision;
    if ((materialDirty || sceneDirty) && vkrtWaitForAllInFlightFrames(vkrt) != VKRT_SUCCESS) {
        return VKRT_ERROR_OPERATION_FAILED;
    }
    if (materialDirty || sceneDirty) {
        invalidateDescriptorSets(vkrt);
    }

    if (materialDirty) {
        if (rebuildMaterialBuffer(vkrt) != VKRT_SUCCESS) {
            return VKRT_ERROR_OPERATION_FAILED;
        }
    }

    if (preparePendingGeometryUploads(vkrt) != VKRT_SUCCESS) {
        return VKRT_ERROR_OPERATION_FAILED;
    }

    if (prepareBottomLevelAccelerationStructureBuilds(vkrt) != VKRT_SUCCESS) {
        return VKRT_ERROR_OPERATION_FAILED;
    }

    if (sceneDirty) {
        if (!materialDirty && lightDirty && rebuildLightBuffers(vkrt) != VKRT_SUCCESS) {
            return VKRT_ERROR_OPERATION_FAILED;
        }
        if (rebuildTopLevelScene(vkrt) != VKRT_SUCCESS) {
            return VKRT_ERROR_OPERATION_FAILED;
        }
    }

    syncCurrentFrameSceneData(vkrt);
    if (updateDescriptorSet(vkrt) != VKRT_SUCCESS) {
        return VKRT_ERROR_OPERATION_FAILED;
    }
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_trace(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    if (!vkrt->runtime.frameAcquired) return VKRT_SUCCESS;
    vkResetFences(vkrt->core.device, 1, &vkrt->runtime.inFlightFences[vkrt->runtime.currentFrame]);

    vkResetCommandBuffer(vkrt->runtime.commandBuffers[vkrt->runtime.currentFrame], 0);
    if (recordCommandBuffer(vkrt, vkrt->runtime.frameImageIndex) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;

    VkSemaphore waitSemaphores[] = {vkrt->runtime.imageAvailableSemaphores[vkrt->runtime.currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_TRANSFER_BIT};
    VkSemaphore signalSemaphores[] = {vkrt->runtime.renderFinishedSemaphores[vkrt->runtime.frameImageIndex]};

    VkSubmitInfo submitInfo = {0};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &vkrt->runtime.commandBuffers[vkrt->runtime.currentFrame];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(vkrt->core.graphicsQueue, 1, &submitInfo, vkrt->runtime.inFlightFences[vkrt->runtime.currentFrame]) != VK_SUCCESS) {
        LOG_ERROR("Failed to submit draw queue");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    vkrt->core.materialResourceRevision = vkrt->core.materialRevision;
    vkrt->core.sceneResourceRevision = vkrt->core.sceneRevision;
    vkrt->core.lightResourceRevision = vkrt->core.lightRevision;
    for (uint32_t i = 0; i < vkrt->core.meshCount; i++) {
        vkrt->core.meshes[i].geometryUploadPending = 0;
        vkrt->core.meshes[i].blasBuildPending = 0;
    }
    vkrt->runtime.frameTimingPending[vkrt->runtime.currentFrame] = VK_TRUE;
    vkrt->runtime.frameSubmitted = VK_TRUE;
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_present(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    if (!vkrt->runtime.frameSubmitted) return VKRT_SUCCESS;
    VkSemaphore signalSemaphores[] = {vkrt->runtime.renderFinishedSemaphores[vkrt->runtime.frameImageIndex]};

    VkPresentInfoKHR presentInfo = {0};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &vkrt->runtime.swapChain;
    presentInfo.pImageIndices = &vkrt->runtime.frameImageIndex;

    VkResult result = vkQueuePresentKHR(vkrt->core.presentQueue, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || vkrt->runtime.framebufferResized) {
        vkrt->runtime.framebufferResized = VK_FALSE;
        return recreateSwapChain(vkrt);
    }

    if (result != VK_SUCCESS) {
        LOG_ERROR("Failed to present draw queue");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    vkrt->runtime.framePresented = VK_TRUE;
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_endFrame(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    if (vkrt->runtime.framePresented) {
        uint32_t renderedSPP = vkrt->core.sceneData->samplesPerPixel;
        VkBool32 traceContributed = vkrt->core.descriptorSetReady[vkrt->runtime.currentFrame] &&
                                    !vkrt->core.accumulationNeedsReset &&
                                    vkrt->runtime.frameTraced &&
                                    !(vkrt->state.renderModeActive && vkrt->state.renderModeFinished);

        if (traceContributed) {
            updateAutoSPP(vkrt);
            vkrt->state.accumulationFrame++;
            vkrt->state.totalSamples += renderedSPP;
            vkrt->core.sceneData->frameNumber++;

            uint32_t nextReadIndex = vkrt->core.accumulationWriteIndex;
            vkrt->core.accumulationWriteIndex = vkrt->core.accumulationReadIndex;
            vkrt->core.accumulationReadIndex = nextReadIndex;
        }

        if (vkrt->state.renderModeActive &&
            !vkrt->state.renderModeFinished &&
            vkrt->state.renderTargetSamples > 0 &&
            vkrt->state.totalSamples >= vkrt->state.renderTargetSamples) {
            vkrt->state.renderModeFinished = 1;
            if (vkrt->runtime.vsync != vkrt->runtime.savedVsync) {
                vkrt->runtime.vsync = vkrt->runtime.savedVsync;
                vkrt->runtime.framebufferResized = VK_TRUE;
            }
        }
    }

    if (vkrt->runtime.frameSubmitted) {
        vkrt->runtime.currentFrame = (vkrt->runtime.currentFrame + 1) % VKRT_MAX_FRAMES_IN_FLIGHT;
    }

    return VKRT_SUCCESS;
}

VKRT_Result VKRT_draw(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    if (VKRT_beginFrame(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;
    if (VKRT_updateScene(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;
    if (VKRT_trace(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;
    if (VKRT_present(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;

    return VKRT_endFrame(vkrt);
}
