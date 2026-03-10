#include "command/record.h"
#include "geometry.h"
#include "rebuild.h"
#include "lighting.h"
#include "state.h"
#include "descriptor.h"
#include "scene.h"
#include "swapchain.h"
#include "accel/accel.h"
#include "vkrt_internal.h"
#include "debug.h"

#include <stdint.h>
#include <stdlib.h>

static int queryDrawableFramebufferExtent(const VKRT* vkrt, uint32_t* outWidth, uint32_t* outHeight) {
    if (!vkrt || !vkrt->runtime.window || !outWidth || !outHeight) return 0;

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(vkrt->runtime.window, &width, &height);
    if (width <= 0 || height <= 0) return 0;

    *outWidth = (uint32_t)width;
    *outHeight = (uint32_t)height;
    return 1;
}

static void invalidateDescriptorSets(VKRT* vkrt) {
    if (!vkrt) return;
    for (uint32_t i = 0; i < VKRT_MAX_FRAMES_IN_FLIGHT; i++) {
        vkrt->core.descriptorSetReady[i] = VK_FALSE;
    }
}

static void resolveCompletedPick(VKRT* vkrt) {
    if (!vkrt || !vkrt->core.pickPending || !vkrt->core.pickData) return;
    if (!vkrt->core.pickSubmitted) return;
    if (vkrt->core.pickPendingFrame != vkrt->runtime.currentFrame) return;

    vkrt->core.pickResultMeshIndex = vkrt->core.pickData->hitMeshIndex;
    vkrt->core.pickResultReady = 1;
    vkrt->core.pickPending = 0;
    vkrt->core.pickSubmitted = 0;
}

static uint32_t queryRenderedSamplesPerPixel(const VKRT* vkrt) {
    if (!vkrt) return 1u;

    uint32_t frameIndex = vkrt->runtime.currentFrame;
    if (frameIndex < VKRT_MAX_FRAMES_IN_FLIGHT) {
        const SceneData* frameSceneData = vkrt->core.sceneFrameData[frameIndex];
        if (frameSceneData && frameSceneData->samplesPerPixel > 0u) {
            return frameSceneData->samplesPerPixel;
        }
    }

    if (vkrt->core.sceneData && vkrt->core.sceneData->samplesPerPixel > 0u) {
        return vkrt->core.sceneData->samplesPerPixel;
    }

    return vkrt->sceneSettings.samplesPerPixel > 0u ? vkrt->sceneSettings.samplesPerPixel : 1u;
}

VKRT_Result VKRT_beginFrame(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    vkrt->runtime.frameAcquired = VK_FALSE;
    vkrt->runtime.frameOffscreen = VK_FALSE;
    vkrt->runtime.frameSubmitted = VK_FALSE;
    vkrt->runtime.framePresented = VK_FALSE;
    vkrt->runtime.frameTraced = VK_FALSE;
    vkrt->runtime.frameSelectionTraced = VK_FALSE;

    vkWaitForFences(vkrt->core.device, 1, &vkrt->runtime.inFlightFences[vkrt->runtime.currentFrame], VK_TRUE, UINT64_MAX);
    resolveCompletedPick(vkrt);
    vkrtCleanupFrameSceneUpdate(vkrt, vkrt->runtime.currentFrame);
    recordFrameTime(vkrt, vkrt->runtime.currentFrame);

    uint32_t framebufferWidth = 0;
    uint32_t framebufferHeight = 0;
    if (!queryDrawableFramebufferExtent(vkrt, &framebufferWidth, &framebufferHeight)) {
        if (vkrt->renderStatus.renderModeActive && !vkrt->renderStatus.renderModeFinished) {
            vkrt->runtime.frameOffscreen = VK_TRUE;
        }
        return VKRT_SUCCESS;
    }

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
    if (!vkrt->runtime.frameAcquired && !vkrt->runtime.frameOffscreen) return VKRT_SUCCESS;

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
        if (vkrtSceneRebuildMaterialBuffer(vkrt) != VKRT_SUCCESS) {
            return VKRT_ERROR_OPERATION_FAILED;
        }
    }

    if (vkrtScenePreparePendingGeometryUploads(vkrt) != VKRT_SUCCESS) {
        return VKRT_ERROR_OPERATION_FAILED;
    }

    if (prepareBottomLevelAccelerationStructureBuilds(vkrt) != VKRT_SUCCESS) {
        return VKRT_ERROR_OPERATION_FAILED;
    }

    if (sceneDirty) {
        if (!materialDirty && lightDirty && vkrtSceneRebuildLightBuffers(vkrt) != VKRT_SUCCESS) {
            return VKRT_ERROR_OPERATION_FAILED;
        }
        if (vkrtSceneRebuildTopLevelScene(vkrt) != VKRT_SUCCESS) {
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
    if (!vkrt->runtime.frameAcquired && !vkrt->runtime.frameOffscreen) return VKRT_SUCCESS;
    vkResetFences(vkrt->core.device, 1, &vkrt->runtime.inFlightFences[vkrt->runtime.currentFrame]);

    vkResetCommandBuffer(vkrt->runtime.commandBuffers[vkrt->runtime.currentFrame], 0);
    if (recordCommandBuffer(vkrt, vkrt->runtime.frameImageIndex, !vkrt->runtime.frameOffscreen) != VKRT_SUCCESS) {
        return VKRT_ERROR_OPERATION_FAILED;
    }

    VkSemaphore waitSemaphores[] = {vkrt->runtime.imageAvailableSemaphores[vkrt->runtime.currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_TRANSFER_BIT};
    VkSemaphore signalSemaphores[] = {vkrt->runtime.renderFinishedSemaphores[vkrt->runtime.frameImageIndex]};

    VkSubmitInfo submitInfo = {0};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = vkrt->runtime.frameOffscreen ? 0u : 1u;
    submitInfo.pWaitSemaphores = vkrt->runtime.frameOffscreen ? NULL : waitSemaphores;
    submitInfo.pWaitDstStageMask = vkrt->runtime.frameOffscreen ? NULL : waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &vkrt->runtime.commandBuffers[vkrt->runtime.currentFrame];
    submitInfo.signalSemaphoreCount = vkrt->runtime.frameOffscreen ? 0u : 1u;
    submitInfo.pSignalSemaphores = vkrt->runtime.frameOffscreen ? NULL : signalSemaphores;

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

    if (vkrt->core.pickPending && vkrt->core.pickPendingFrame == vkrt->runtime.currentFrame) {
        vkrt->core.pickSubmitted = 1;
    }

    if (vkrt->runtime.frameSelectionTraced) {
        vkrt->core.selectionMaskDirty = VK_FALSE;
    }

    vkrt->runtime.frameSubmitted = VK_TRUE;

    return VKRT_SUCCESS;
}

VKRT_Result VKRT_present(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    if (!vkrt->runtime.frameSubmitted) return VKRT_SUCCESS;
    if (vkrt->runtime.frameOffscreen) {
        vkrt->runtime.framePresented = VK_TRUE;
        return VKRT_SUCCESS;
    }
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
        uint32_t renderedSPP = queryRenderedSamplesPerPixel(vkrt);
        VkBool32 traceContributed = vkrt->core.descriptorSetReady[vkrt->runtime.currentFrame] &&
                                    !vkrt->core.accumulationNeedsReset &&
                                    vkrt->runtime.frameTraced &&
                                    !(vkrt->renderStatus.renderModeActive && vkrt->renderStatus.renderModeFinished);

        if (traceContributed) {
            updateAutoSPP(vkrt);
            vkrt->renderStatus.accumulationFrame++;
            vkrt->renderStatus.totalSamples += renderedSPP;
            vkrt->core.sceneData->frameNumber++;

            uint32_t nextReadIndex = vkrt->core.accumulationWriteIndex;
            vkrt->core.accumulationWriteIndex = vkrt->core.accumulationReadIndex;
            vkrt->core.accumulationReadIndex = nextReadIndex;
        }

        if (vkrt->renderStatus.renderModeActive &&
            !vkrt->renderStatus.renderModeFinished &&
            vkrt->renderStatus.renderTargetSamples > 0 &&
            vkrt->renderStatus.totalSamples >= vkrt->renderStatus.renderTargetSamples) {
            vkrt->renderStatus.renderModeFinished = 1;
            if (vkrt->runtime.presentModePreference != vkrt->runtime.savedPresentModePreference) {
                vkrt->runtime.presentModePreference = vkrt->runtime.savedPresentModePreference;
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
