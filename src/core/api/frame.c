#include "command/record.h"
#include "geometry.h"
#include "rebuild.h"
#include "lighting.h"
#include "state.h"
#include "descriptor.h"
#include "export.h"
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

static void resolveCompletedSelection(VKRT* vkrt) {
    if (!vkrt || !vkrt->core.selectionPending || !vkrt->core.selectionData) return;
    if (!vkrt->core.selectionSubmitted) return;
    if (vkrt->core.selectionPendingFrame != vkrt->runtime.currentFrame) return;

    vkrt->core.selectionResultMeshIndex = vkrt->core.selectionData->hitMeshIndex;
    vkrt->core.selectionResultReady = 1;
    vkrt->core.selectionPending = 0;
    vkrt->core.selectionSubmitted = 0;
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

    syncCompletedViewportDenoise(vkrt);
    processPendingViewportDenoise(vkrt);

    vkrt->runtime.frameAcquired = VK_FALSE;
    vkrt->runtime.frameOffscreen = VK_FALSE;
    vkrt->runtime.frameSubmitted = VK_FALSE;
    vkrt->runtime.framePresented = VK_FALSE;
    vkrt->runtime.frameTraced = VK_FALSE;
    vkrt->runtime.frameSelectionTraced = VK_FALSE;

    VkResult fenceResult = vkWaitForFences(vkrt->core.device, 1, &vkrt->runtime.inFlightFences[vkrt->runtime.currentFrame], VK_TRUE, UINT64_MAX);
    if (fenceResult == VK_ERROR_DEVICE_LOST) {
        LOG_ERROR("Device lost while waiting for fence");
        return VKRT_ERROR_DEVICE_LOST;
    }
    if (fenceResult != VK_SUCCESS) {
        LOG_ERROR("Failed to wait for in-flight fence (%d)", (int)fenceResult);
        return VKRT_ERROR_OPERATION_FAILED;
    }
    resolveAutoExposureReadback(vkrt, vkrt->runtime.currentFrame);
    resolveCompletedSelection(vkrt);
    vkrtCleanupFrameSceneUpdate(vkrt, vkrt->runtime.currentFrame);
    recordFrameTime(vkrt, vkrt->runtime.currentFrame);

    uint32_t framebufferWidth = 0;
    uint32_t framebufferHeight = 0;
    if (!queryDrawableFramebufferExtent(vkrt, &framebufferWidth, &framebufferHeight)) {
        if (VKRT_renderPhaseIsSampling(vkrt->renderStatus.renderPhase)) {
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

    if (result == VK_ERROR_DEVICE_LOST) {
        LOG_ERROR("Device lost while acquiring swapchain image");
        return VKRT_ERROR_DEVICE_LOST;
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
    VkBool32 textureDirty = vkrt->core.textureResourceRevision != vkrt->core.textureRevision;
    VkBool32 sceneDirty = vkrt->core.sceneResourceRevision != vkrt->core.sceneRevision;
    VkBool32 selectionDirty = vkrt->core.selectionResourceRevision != vkrt->core.selectionRevision;
    VkBool32 lightDirty = vkrt->core.lightResourceRevision != vkrt->core.lightRevision;
    VkBool32 lightsRebuilt = VK_FALSE;
    if (materialDirty || sceneDirty || selectionDirty || lightDirty) {
        VKRT_Result result = vkrtWaitForAllInFlightFrames(vkrt);
        if (result != VKRT_SUCCESS) {
            return result;
        }
    }

    if (materialDirty || textureDirty || sceneDirty || selectionDirty || lightDirty) {
        invalidateDescriptorSets(vkrt);
    }

    if (materialDirty) {
        VKRT_Result result = vkrtSceneRebuildMaterialBuffer(vkrt);
        if (result != VKRT_SUCCESS) {
            return result;
        }
        lightsRebuilt = VK_TRUE;
    }

    VKRT_Result result = vkrtScenePreparePendingGeometryUploads(vkrt);
    if (result != VKRT_SUCCESS) {
        return result;
    }

    result = prepareBottomLevelAccelerationStructureBuilds(vkrt);
    if (result != VKRT_SUCCESS) {
        return result;
    }

    if (lightDirty) {
        if (!lightsRebuilt) {
            result = vkrtSceneRebuildLightBuffers(vkrt);
            if (result != VKRT_SUCCESS) {
                return result;
            }
            lightsRebuilt = VK_TRUE;
        }
    }

    if (lightsRebuilt && !sceneDirty) {
        result = vkrtSceneRebuildMeshInfoBuffer(vkrt);
        if (result != VKRT_SUCCESS) {
            return result;
        }
    }

    if (sceneDirty) {
        result = vkrtSceneRebuildTopLevelAccelerationStructures(vkrt);
        if (result != VKRT_SUCCESS) {
            return result;
        }
    } else if (selectionDirty) {
        result = createSelectionTopLevelAccelerationStructure(vkrt);
        if (result != VKRT_SUCCESS) {
            return result;
        }
    }

    syncCurrentFrameSceneData(vkrt);
    result = updateDescriptorSet(vkrt);
    if (result != VKRT_SUCCESS) {
        return result;
    }
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_trace(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    if (!vkrt->runtime.frameAcquired && !vkrt->runtime.frameOffscreen) return VKRT_SUCCESS;
    VKRT_Result result = vkrtConvertVkResult(vkResetCommandBuffer(
        vkrt->runtime.commandBuffers[vkrt->runtime.currentFrame],
        0
    ));
    if (result != VKRT_SUCCESS) {
        return result;
    }

    result = recordCommandBuffer(vkrt, vkrt->runtime.frameImageIndex, !vkrt->runtime.frameOffscreen);
    if (result != VKRT_SUCCESS) {
        return result;
    }

    VkSemaphore waitSemaphores[] = {vkrt->runtime.imageAvailableSemaphores[vkrt->runtime.currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_TRANSFER_BIT};
    VkSemaphore signalSemaphores[] = {VK_NULL_HANDLE};
    if (!vkrt->runtime.frameOffscreen) {
        signalSemaphores[0] = vkrt->runtime.renderFinishedSemaphores[vkrt->runtime.frameImageIndex];
    }

    VkSubmitInfo submitInfo = {0};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = vkrt->runtime.frameOffscreen ? 0u : 1u;
    submitInfo.pWaitSemaphores = vkrt->runtime.frameOffscreen ? NULL : waitSemaphores;
    submitInfo.pWaitDstStageMask = vkrt->runtime.frameOffscreen ? NULL : waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &vkrt->runtime.commandBuffers[vkrt->runtime.currentFrame];
    submitInfo.signalSemaphoreCount = vkrt->runtime.frameOffscreen ? 0u : 1u;
    submitInfo.pSignalSemaphores = vkrt->runtime.frameOffscreen ? NULL : signalSemaphores;

    result = vkrtConvertVkResult(vkResetFences(
        vkrt->core.device,
        1,
        &vkrt->runtime.inFlightFences[vkrt->runtime.currentFrame]
    ));
    if (result != VKRT_SUCCESS) {
        return result;
    }

    result = vkrtConvertVkResult(vkQueueSubmit(
        vkrt->core.graphicsQueue,
        1,
        &submitInfo,
        vkrt->runtime.inFlightFences[vkrt->runtime.currentFrame]
    ));
    if (result != VKRT_SUCCESS) {
        LOG_ERROR("Failed to submit draw queue");
        return result;
    }

    vkrt->core.materialResourceRevision = vkrt->core.materialRevision;
    vkrt->core.textureResourceRevision = vkrt->core.textureRevision;
    vkrt->core.sceneResourceRevision = vkrt->core.sceneRevision;
    vkrt->core.selectionResourceRevision = vkrt->core.selectionRevision;
    vkrt->core.lightResourceRevision = vkrt->core.lightRevision;

    for (uint32_t i = 0; i < vkrt->core.meshCount; i++) {
        vkrt->core.meshes[i].geometryUploadPending = 0;
        vkrt->core.meshes[i].blasBuildPending = 0;
    }

    vkrt->runtime.frameTimingPending[vkrt->runtime.currentFrame] = VK_TRUE;

    if (vkrt->core.selectionPending && vkrt->core.selectionPendingFrame == vkrt->runtime.currentFrame) {
        vkrt->core.selectionSubmitted = 1;
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
                                    !VKRT_renderPhaseSamplingFinished(vkrt->renderStatus.renderPhase);

        if (traceContributed) {
            updateAutoSPP(vkrt);
            vkrt->renderStatus.accumulationFrame++;
            vkrt->renderStatus.totalSamples += renderedSPP;
            vkrt->core.sceneData->frameNumber++;

            uint32_t nextReadIndex = vkrt->core.accumulationWriteIndex;
            vkrt->core.accumulationWriteIndex = vkrt->core.accumulationReadIndex;
            vkrt->core.accumulationReadIndex = nextReadIndex;
        }

        if (VKRT_renderPhaseIsSampling(vkrt->renderStatus.renderPhase) &&
            vkrt->renderStatus.renderTargetSamples > 0 &&
            vkrt->renderStatus.totalSamples >= vkrt->renderStatus.renderTargetSamples) {
            VKRT_stopRenderSampling(vkrt);
        }
    }

    if (vkrt->runtime.frameSubmitted) {
        vkrt->runtime.currentFrame = (vkrt->runtime.currentFrame + 1) % VKRT_MAX_FRAMES_IN_FLIGHT;
    }

    return VKRT_SUCCESS;
}

VKRT_Result VKRT_draw(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    VKRT_Result result = VKRT_beginFrame(vkrt);
    if (result != VKRT_SUCCESS) return result;

    result = VKRT_updateScene(vkrt);
    if (result != VKRT_SUCCESS) return result;

    result = VKRT_trace(vkrt);
    if (result != VKRT_SUCCESS) return result;

    result = VKRT_present(vkrt);
    if (result != VKRT_SUCCESS) return result;

    return VKRT_endFrame(vkrt);
}
