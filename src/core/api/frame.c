#include "buffer.h"
#include "command.h"
#include "descriptor.h"
#include "scene.h"
#include "accel.h"
#include "swapchain.h"
#include "vkrt.h"
#include "debug.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

void rebuildMaterialBuffer(VKRT* vkrt);

void VKRT_beginFrame(VKRT* vkrt) {
    if (!vkrt) return;

    vkrt->runtime.frameAcquired = VK_FALSE;
    vkrt->runtime.frameSubmitted = VK_FALSE;
    vkrt->runtime.framePresented = VK_FALSE;

    vkWaitForFences(vkrt->core.device, 1, &vkrt->runtime.inFlightFences[vkrt->runtime.currentFrame], VK_TRUE, UINT64_MAX);

    if (vkrt->core.topLevelAccelerationStructure.needsRebuild) {
        vkDeviceWaitIdle(vkrt->core.device);
        createTopLevelAccelerationStructure(vkrt);
        updateDescriptorSet(vkrt);
        vkrt->core.topLevelAccelerationStructure.needsRebuild = 0;
        resetSceneData(vkrt);
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
        recreateSwapChain(vkrt);
        return;
    }

    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        LOG_ERROR("Failed to acquire next swapchain image");
        exit(EXIT_FAILURE);
    }

    vkrt->runtime.frameAcquired = VK_TRUE;
}

void VKRT_updateScene(VKRT* vkrt) {
    if (!vkrt || !vkrt->runtime.frameAcquired) return;

    if (vkrt->core.materialDataDirty) {
        rebuildMaterialBuffer(vkrt);
        updateDescriptorSet(vkrt);
    }
}

void VKRT_trace(VKRT* vkrt) {
    if (!vkrt || !vkrt->runtime.frameAcquired) return;
    vkResetFences(vkrt->core.device, 1, &vkrt->runtime.inFlightFences[vkrt->runtime.currentFrame]);

    vkResetCommandBuffer(vkrt->runtime.commandBuffers[vkrt->runtime.currentFrame], 0);
    recordCommandBuffer(vkrt, vkrt->runtime.frameImageIndex);

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
        exit(EXIT_FAILURE);
    }

    vkrt->runtime.frameSubmitted = VK_TRUE;
}

void VKRT_present(VKRT* vkrt) {
    if (!vkrt || !vkrt->runtime.frameSubmitted) return;
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
        recreateSwapChain(vkrt);
        return;
    }

    if (result != VK_SUCCESS) {
        LOG_ERROR("Failed to present draw queue");
        exit(EXIT_FAILURE);
    }

    vkrt->runtime.framePresented = VK_TRUE;
}

void VKRT_endFrame(VKRT* vkrt) {
    if (!vkrt) return;

    if (vkrt->runtime.framePresented) {
        uint32_t renderedSPP = vkrt->core.sceneData->samplesPerPixel;
        recordFrameTime(vkrt);
        updateAutoSPP(vkrt);
        if (vkrt->core.descriptorSetReady && !vkrt->core.accumulationNeedsReset) {
            vkrt->state.accumulationFrame++;
            vkrt->state.totalSamples += renderedSPP;
            vkrt->core.sceneData->frameNumber++;
        }
    }

    if (vkrt->runtime.frameSubmitted) {
        vkrt->runtime.currentFrame = (vkrt->runtime.currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }
}

void VKRT_draw(VKRT* vkrt) {
    if (!vkrt) return;
    VKRT_beginFrame(vkrt);
    VKRT_updateScene(vkrt);
    VKRT_trace(vkrt);
    VKRT_present(vkrt);
    VKRT_endFrame(vkrt);
}
