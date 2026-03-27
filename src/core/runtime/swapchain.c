#include "swapchain.h"

#include "GLFW/glfw3.h"
#include "config.h"
#include "debug.h"
#include "descriptor.h"
#include "images.h"
#include "scene.h"
#include "state.h"
#include "sync.h"
#include "vkrt.h"
#include "vkrt_types.h"
#include "vulkan/vulkan_core.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static const char* swapChainFormatName(VkFormat format) {
    switch (format) {
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            return "VK_FORMAT_R16G16B16A16_SFLOAT";
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
            return "VK_FORMAT_A2B10G10R10_UNORM_PACK32";
        case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
            return "VK_FORMAT_A2R10G10B10_UNORM_PACK32";
        case VK_FORMAT_B8G8R8A8_UNORM:
            return "VK_FORMAT_B8G8R8A8_UNORM";
        case VK_FORMAT_R8G8B8A8_UNORM:
            return "VK_FORMAT_R8G8B8A8_UNORM";
        case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
            return "VK_FORMAT_A8B8G8R8_UNORM_PACK32";
        default:
            return "VK_FORMAT_OTHER";
    }
}

static const char* swapChainColorSpaceName(VkColorSpaceKHR colorSpace) {
    switch (colorSpace) {
        case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR:
            return "VK_COLOR_SPACE_SRGB_NONLINEAR_KHR";
        case VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT:
            return "VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT";
        default:
            return "VK_COLOR_SPACE_OTHER";
    }
}

static const char* presentModeName(VkPresentModeKHR presentMode) {
    switch (presentMode) {
        case VK_PRESENT_MODE_IMMEDIATE_KHR:
            return "immediate";
        case VK_PRESENT_MODE_MAILBOX_KHR:
            return "mailbox";
        case VK_PRESENT_MODE_FIFO_KHR:
            return "fifo";
        case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
            return "fifo_relaxed";
        default:
            return "other";
    }
}

static const VkPresentModeKHR kInteractivePresentModeRanking[] = {
    VK_PRESENT_MODE_FIFO_KHR,
    VK_PRESENT_MODE_MAILBOX_KHR,
    VK_PRESENT_MODE_FIFO_RELAXED_KHR,
};

static const VkPresentModeKHR kRenderPresentModeRanking[] = {
    VK_PRESENT_MODE_IMMEDIATE_KHR,
    VK_PRESENT_MODE_MAILBOX_KHR,
    VK_PRESENT_MODE_FIFO_RELAXED_KHR,
    VK_PRESENT_MODE_FIFO_KHR,
};

static const char* presentProfileName(VkBool32 useRenderPresentProfile) {
    return useRenderPresentProfile ? "render" : "interactive";
}

typedef struct SwapChainState {
    VkSwapchainKHR swapChain;
    VkImage* swapChainImages;
    VkImageView* swapChainImageViews;
    VkSemaphore* renderFinishedSemaphores;
    size_t swapChainImageCount;
    uint32_t swapChainMinImageCount;
    VkFormat swapChainImageFormat;
    VkExtent2D swapChainExtent;
    VkPresentModeKHR presentMode;
    uint32_t displayWidth;
    uint32_t displayHeight;
    float displayRefreshHz;
} SwapChainState;

static int hasPresentMode(const SwapChainSupportDetails* supportDetails, VkPresentModeKHR presentMode) {
    if (!supportDetails) return 0;
    for (uint32_t i = 0; i < supportDetails->presentModeCount; i++) {
        if (supportDetails->presentModes[i] == presentMode) return 1;
    }
    return 0;
}

static uint32_t collectPresentModeCandidates(
    const SwapChainSupportDetails* supportDetails,
    VkBool32 useRenderPresentProfile,
    VkPresentModeKHR* outModes
) {
    if (!supportDetails || !outModes) return 0;

    const VkPresentModeKHR* ranking =
        useRenderPresentProfile ? kRenderPresentModeRanking : kInteractivePresentModeRanking;
    uint32_t rankingCount = useRenderPresentProfile ? (uint32_t)VKRT_ARRAY_COUNT(kRenderPresentModeRanking)
                                                    : (uint32_t)VKRT_ARRAY_COUNT(kInteractivePresentModeRanking);
    uint32_t writeCount = 0;

    for (uint32_t i = 0; i < rankingCount; i++) {
        VkPresentModeKHR mode = ranking[i];
        if (!hasPresentMode(supportDetails, mode)) continue;

        int duplicate = 0;
        for (uint32_t j = 0; j < writeCount; j++) {
            if (outModes[j] == mode) {
                duplicate = 1;
                break;
            }
        }
        if (!duplicate) {
            outModes[writeCount++] = mode;
        }
    }

    if (writeCount == 0) {
        outModes[writeCount++] = VK_PRESENT_MODE_FIFO_KHR;
    }

    return writeCount;
}

VkBool32 vkrtUsesRenderPresentProfile(const VKRT* vkrt) {
    return vkrt && VKRT_renderPhaseIsSampling(vkrt->renderStatus.renderPhase);
}

void vkrtRefreshPresentModeIfNeeded(VKRT* vkrt, VkBool32 previousUsesRenderPresentProfile) {
    if (!vkrt) return;
    if (vkrtUsesRenderPresentProfile(vkrt) != previousUsesRenderPresentProfile) {
        vkrt->runtime.framebufferResized = VK_TRUE;
    }
}

void vkrtQueryDisplayMetrics(GLFWwindow* window, uint32_t* outWidth, uint32_t* outHeight, float* outRefreshHz) {
    GLFWmonitor* monitor = window ? glfwGetWindowMonitor(window) : NULL;
    if (!monitor) monitor = glfwGetPrimaryMonitor();
    if (!monitor) {
        if (outWidth) *outWidth = VKRT_DEFAULT_WIDTH;
        if (outHeight) *outHeight = VKRT_DEFAULT_HEIGHT;
        if (outRefreshHz) *outRefreshHz = 60.0f;
        return;
    }

    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    if (!mode) {
        if (outWidth) *outWidth = VKRT_DEFAULT_WIDTH;
        if (outHeight) *outHeight = VKRT_DEFAULT_HEIGHT;
        if (outRefreshHz) *outRefreshHz = 60.0f;
        return;
    }

    if (outWidth) *outWidth = mode->width > 0 ? (uint32_t)mode->width : VKRT_DEFAULT_WIDTH;
    if (outHeight) *outHeight = mode->height > 0 ? (uint32_t)mode->height : VKRT_DEFAULT_HEIGHT;
    if (outRefreshHz) *outRefreshHz = mode->refreshRate > 0 ? (float)mode->refreshRate : 60.0f;
}

static void clearSwapChainBindings(VKRT* vkrt) {
    if (!vkrt) return;

    vkrt->runtime.swapChain = VK_NULL_HANDLE;
    vkrt->runtime.swapChainImages = NULL;
    vkrt->runtime.swapChainImageViews = NULL;
    vkrt->runtime.renderFinishedSemaphores = NULL;
    vkrt->runtime.swapChainImageCount = 0;
    vkrt->runtime.swapChainMinImageCount = 0;
    vkrt->runtime.swapChainImageFormat = VK_FORMAT_UNDEFINED;
    vkrt->runtime.swapChainExtent = (VkExtent2D){0};
}

static void captureSwapChainState(const VKRT* vkrt, SwapChainState* outState) {
    if (!outState) return;

    *outState = (SwapChainState){0};
    if (!vkrt) return;

    outState->swapChain = vkrt->runtime.swapChain;
    outState->swapChainImages = vkrt->runtime.swapChainImages;
    outState->swapChainImageViews = vkrt->runtime.swapChainImageViews;
    outState->renderFinishedSemaphores = vkrt->runtime.renderFinishedSemaphores;
    outState->swapChainImageCount = vkrt->runtime.swapChainImageCount;
    outState->swapChainMinImageCount = vkrt->runtime.swapChainMinImageCount;
    outState->swapChainImageFormat = vkrt->runtime.swapChainImageFormat;
    outState->swapChainExtent = vkrt->runtime.swapChainExtent;
    outState->presentMode = vkrt->runtime.presentMode;
    outState->displayWidth = vkrt->runtime.displayWidth;
    outState->displayHeight = vkrt->runtime.displayHeight;
    outState->displayRefreshHz = vkrt->runtime.displayRefreshHz;
}

static void applySwapChainState(VKRT* vkrt, const SwapChainState* state) {
    if (!vkrt) return;

    clearSwapChainBindings(vkrt);
    if (!state) return;

    vkrt->runtime.swapChain = state->swapChain;
    vkrt->runtime.swapChainImages = state->swapChainImages;
    vkrt->runtime.swapChainImageViews = state->swapChainImageViews;
    vkrt->runtime.renderFinishedSemaphores = state->renderFinishedSemaphores;
    vkrt->runtime.swapChainImageCount = state->swapChainImageCount;
    vkrt->runtime.swapChainMinImageCount = state->swapChainMinImageCount;
    vkrt->runtime.swapChainImageFormat = state->swapChainImageFormat;
    vkrt->runtime.swapChainExtent = state->swapChainExtent;
    vkrt->runtime.presentMode = state->presentMode;
    vkrt->runtime.displayWidth = state->displayWidth;
    vkrt->runtime.displayHeight = state->displayHeight;
    vkrt->runtime.displayRefreshHz = state->displayRefreshHz;
}

static void destroySemaphoreList(VKRT* vkrt, VkSemaphore* semaphores, size_t semaphoreCount) {
    if (!vkrt || !semaphores || vkrt->core.device == VK_NULL_HANDLE) return;

    for (size_t i = 0; i < semaphoreCount; i++) {
        if (semaphores[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(vkrt->core.device, semaphores[i], NULL);
        }
    }
}

static void destroySwapChainState(VKRT* vkrt, SwapChainState* state) {
    if (!vkrt || !state) return;

    if (vkrt->core.device != VK_NULL_HANDLE) {
        for (size_t i = 0; i < state->swapChainImageCount; i++) {
            if (state->swapChainImageViews) {
                vkDestroyImageView(vkrt->core.device, state->swapChainImageViews[i], NULL);
            }
        }

        if (state->swapChain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(vkrt->core.device, state->swapChain, NULL);
        }

        destroySemaphoreList(vkrt, state->renderFinishedSemaphores, state->swapChainImageCount);
    }

    free((void*)state->swapChainImageViews);
    free((void*)state->swapChainImages);
    free((void*)state->renderFinishedSemaphores);
    *state = (SwapChainState){0};
}

static void logSelectedSwapChainFormat(
    VKRT* vkrt,
    VkSurfaceFormatKHR surfaceFormat,
    VkPresentModeKHR preferredPresentMode,
    VkBool32 useRenderPresentProfile
) {
    if (!vkrt) return;

    if (!vkrt->runtime.swapChainFormatLogInitialized ||
        vkrt->runtime.lastLoggedSwapChainFormat != surfaceFormat.format ||
        vkrt->runtime.lastLoggedSwapChainColorSpace != surfaceFormat.colorSpace) {
        LOG_INFO(
            "Swapchain format selected: %s (%d), color space: %s (%d)",
            swapChainFormatName(surfaceFormat.format),
            (int)surfaceFormat.format,
            swapChainColorSpaceName(surfaceFormat.colorSpace),
            (int)surfaceFormat.colorSpace
        );
        vkrt->runtime.swapChainFormatLogInitialized = VK_TRUE;
        vkrt->runtime.lastLoggedSwapChainFormat = surfaceFormat.format;
        vkrt->runtime.lastLoggedSwapChainColorSpace = surfaceFormat.colorSpace;
    }

    LOG_TRACE(
        "Swapchain present mode selected. Profile: %s, preferred: %s",
        presentProfileName(useRenderPresentProfile),
        presentModeName(preferredPresentMode)
    );
}

static VkSwapchainCreateInfoKHR buildSwapChainCreateInfo(
    const VKRT* vkrt,
    const SwapChainSupportDetails* supportDetails,
    VkSurfaceFormatKHR surfaceFormat,
    VkExtent2D extent,
    uint32_t imageCount,
    VkSwapchainKHR oldSwapchain,
    uint32_t outQueueFamilyIndices[2]
) {
    VkSwapchainCreateInfoKHR createInfo = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = vkrt->runtime.surface,
        .minImageCount = imageCount,
        .imageFormat = surfaceFormat.format,
        .imageColorSpace = surfaceFormat.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1u,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .preTransform = supportDetails->capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
        .oldSwapchain = oldSwapchain,
    };

    outQueueFamilyIndices[0] = vkrt->core.indices.graphics;
    outQueueFamilyIndices[1] = vkrt->core.indices.present;

    if (vkrt->core.indices.graphics != vkrt->core.indices.present) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2u;
        createInfo.pQueueFamilyIndices = outQueueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    return createInfo;
}

static VKRT_Result createSwapChainHandle(
    VKRT* vkrt,
    VkBool32 useRenderPresentProfile,
    const VkPresentModeKHR* presentModes,
    uint32_t presentModeCount,
    VkSwapchainCreateInfoKHR* createInfo,
    SwapChainState* outState,
    VkResult* outCreateVkResult
) {
    VkResult createResult = VK_ERROR_INITIALIZATION_FAILED;

    if (!vkrt || !presentModes || !createInfo || !outState) return VKRT_ERROR_INVALID_ARGUMENT;

    for (uint32_t i = 0; i < presentModeCount; i++) {
        outState->presentMode = presentModes[i];
        createInfo->presentMode = presentModes[i];
        createResult = vkCreateSwapchainKHR(vkrt->core.device, createInfo, NULL, &outState->swapChain);
        if (createResult == VK_SUCCESS) {
            LOG_TRACE(
                "Swapchain present mode active. Profile: %s, actual: %s",
                presentProfileName(useRenderPresentProfile),
                presentModeName(presentModes[i])
            );
            if (outCreateVkResult) *outCreateVkResult = VK_SUCCESS;
            return VKRT_SUCCESS;
        }

        LOG_ERROR(
            "Failed to create swapchain with present mode %s (%d)",
            presentModeName(presentModes[i]),
            (int)createResult
        );
        outState->swapChain = VK_NULL_HANDLE;
    }

    if (outCreateVkResult) *outCreateVkResult = createResult;
    return VKRT_ERROR_OPERATION_FAILED;
}

static VKRT_Result populateSwapChainImages(VKRT* vkrt, SwapChainState* outState, uint32_t imageCount) {
    if (!vkrt || !outState) return VKRT_ERROR_INVALID_ARGUMENT;

    if (vkGetSwapchainImagesKHR(vkrt->core.device, outState->swapChain, &imageCount, NULL) != VK_SUCCESS ||
        imageCount == 0u) {
        destroySwapChainState(vkrt, outState);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    outState->swapChainImages = (VkImage*)malloc(imageCount * sizeof(VkImage));
    if (!outState->swapChainImages) {
        destroySwapChainState(vkrt, outState);
        return VKRT_ERROR_OUT_OF_MEMORY;
    }
    outState->swapChainImageCount = imageCount;

    if (vkGetSwapchainImagesKHR(vkrt->core.device, outState->swapChain, &imageCount, outState->swapChainImages) !=
        VK_SUCCESS) {
        destroySwapChainState(vkrt, outState);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    return VKRT_SUCCESS;
}

static VKRT_Result recreateSwapChainRuntimeResources(
    VKRT* vkrt,
    GPUImageState* previousImages,
    GPUImageState* nextImages
) {
    VkBool32 renderPhaseActive = VK_FALSE;
    VKRT_Result result = VKRT_SUCCESS;

    if (!vkrt || !previousImages || !nextImages) return VKRT_ERROR_INVALID_ARGUMENT;

    renderPhaseActive = VKRT_renderPhaseIsActive(vkrt->renderStatus.renderPhase);
    result = createImageViews(vkrt);
    if (result != VKRT_SUCCESS) return result;

    result = resetRenderFinishedSemaphores(vkrt, 0, vkrt->runtime.swapChainImageCount);
    if (result != VKRT_SUCCESS) return result;

    if (!renderPhaseActive) {
        result = createGPUImageState(vkrt, vkrt->runtime.swapChainExtent, nextImages);
        if (result != VKRT_SUCCESS) return result;
        applyGPUImageState(vkrt, nextImages);
    }

    result = updateAllDescriptorSets(vkrt);
    if (result == VKRT_SUCCESS || renderPhaseActive) return result;

    applyGPUImageState(vkrt, previousImages);
    updateAllDescriptorSets(vkrt);
    return result;
}

static void finalizeRecreatedSwapChain(
    VKRT* vkrt,
    SwapChainState* previousSwapChain,
    GPUImageState* previousImages,
    uint32_t preservedViewportX,
    uint32_t preservedViewportY,
    uint32_t preservedViewportWidth,
    uint32_t preservedViewportHeight
) {
    VkBool32 renderPhaseActive = VK_FALSE;

    if (!vkrt || !previousSwapChain || !previousImages) return;

    renderPhaseActive = VKRT_renderPhaseIsActive(vkrt->renderStatus.renderPhase);
    destroySwapChainState(vkrt, previousSwapChain);
    if (!renderPhaseActive) destroyGPUImageState(vkrt, previousImages);

    if (renderPhaseActive) {
        VKRT_setRenderViewport(
            vkrt,
            preservedViewportX,
            preservedViewportY,
            preservedViewportWidth,
            preservedViewportHeight
        );
    } else {
        VKRT_setRenderViewport(vkrt, 0, 0, vkrt->runtime.swapChainExtent.width, vkrt->runtime.swapChainExtent.height);
    }

    vkrt->renderControl.timing.lastFrameTimestamp = 0u;
    vkrt->renderControl.autoSPP.controlMs = 0.0f;
    if (!renderPhaseActive) resetSceneData(vkrt);
    vkrt->runtime.framebufferResized = VK_FALSE;
}

static VKRT_Result restorePreviousSwapChainState(
    VKRT* vkrt,
    SwapChainState* previousSwapChain,
    GPUImageState* previousImages,
    GPUImageState* nextImages,
    VkExtent2D previousRenderExtent,
    VkBool32 previousSwapChainRetired,
    VKRT_Result result
) {
    VkBool32 renderPhaseActive = VK_FALSE;
    SwapChainState failedSwapChain = {0};

    if (!vkrt || !previousSwapChain || !previousImages || !nextImages) return result;

    captureSwapChainState(vkrt, &failedSwapChain);
    clearSwapChainBindings(vkrt);
    destroySwapChainState(vkrt, &failedSwapChain);

    vkrt->runtime.renderExtent = previousRenderExtent;
    renderPhaseActive = VKRT_renderPhaseIsActive(vkrt->renderStatus.renderPhase);
    if (!renderPhaseActive) {
        applyGPUImageState(vkrt, previousImages);
        updateAllDescriptorSets(vkrt);
        destroyGPUImageState(vkrt, nextImages);
    }

    if (previousSwapChainRetired) {
        destroySwapChainState(vkrt, previousSwapChain);
        return result;
    }

    applySwapChainState(vkrt, previousSwapChain);
    return result;
}

static VKRT_Result createSwapChainWithOld(VKRT* vkrt, VkSwapchainKHR oldSwapchain, VkResult* outCreateVkResult) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    if (outCreateVkResult) *outCreateVkResult = VK_SUCCESS;

    SwapChainSupportDetails supportDetails = {
        .capabilities = {.currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR},
    };
    if (querySwapChainSupport(vkrt, &supportDetails) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;

    VkSurfaceFormatKHR surfaceFormat = {0};
    if (chooseSwapSurfaceFormat(&supportDetails, &surfaceFormat) != VKRT_SUCCESS) {
        free(supportDetails.formats);
        free(supportDetails.presentModes);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    VkBool32 useRenderPresentProfile = vkrtUsesRenderPresentProfile(vkrt);
    VkPresentModeKHR presentModes[VKRT_ARRAY_COUNT(kRenderPresentModeRanking)] = {0};
    uint32_t presentModeCount = collectPresentModeCandidates(&supportDetails, useRenderPresentProfile, presentModes);
    VkExtent2D extent = chooseSwapExtent(vkrt, &supportDetails);
    vkrtQueryDisplayMetrics(
        vkrt->runtime.window,
        &vkrt->runtime.displayWidth,
        &vkrt->runtime.displayHeight,
        &vkrt->runtime.displayRefreshHz
    );
    logSelectedSwapChainFormat(vkrt, surfaceFormat, presentModes[0], useRenderPresentProfile);

    uint32_t imageCount = supportDetails.capabilities.minImageCount + 1;
    if (supportDetails.capabilities.maxImageCount && imageCount > supportDetails.capabilities.maxImageCount) {
        imageCount = supportDetails.capabilities.maxImageCount;
    }

    SwapChainState nextState = {0};
    nextState.displayWidth = vkrt->runtime.displayWidth;
    nextState.displayHeight = vkrt->runtime.displayHeight;
    nextState.displayRefreshHz = vkrt->runtime.displayRefreshHz;
    nextState.swapChainMinImageCount = supportDetails.capabilities.minImageCount;
    nextState.swapChainImageFormat = surfaceFormat.format;
    nextState.swapChainExtent = extent;

    uint32_t queueFamilyIndices[2] = {0u, 0u};
    VkSwapchainCreateInfoKHR swapChainCreateInfo = buildSwapChainCreateInfo(
        vkrt,
        &supportDetails,
        surfaceFormat,
        extent,
        imageCount,
        oldSwapchain,
        queueFamilyIndices
    );

    free(supportDetails.formats);
    free(supportDetails.presentModes);

    if (createSwapChainHandle(
            vkrt,
            useRenderPresentProfile,
            presentModes,
            presentModeCount,
            &swapChainCreateInfo,
            &nextState,
            outCreateVkResult
        ) != VKRT_SUCCESS) {
        return VKRT_ERROR_OPERATION_FAILED;
    }
    if (populateSwapChainImages(vkrt, &nextState, imageCount) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;

    applySwapChainState(vkrt, &nextState);
    if (!VKRT_renderPhaseIsActive(vkrt->renderStatus.renderPhase) || vkrt->runtime.renderExtent.width == 0 ||
        vkrt->runtime.renderExtent.height == 0) {
        vkrt->runtime.renderExtent = extent;
    }

    return VKRT_SUCCESS;
}

VKRT_Result createSwapChain(VKRT* vkrt) {
    return createSwapChainWithOld(vkrt, VK_NULL_HANDLE, NULL);
}

VKRT_Result recreateSwapChain(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    uint32_t preservedViewportX = vkrt->runtime.displayViewportRect[0];
    uint32_t preservedViewportY = vkrt->runtime.displayViewportRect[1];
    uint32_t preservedViewportWidth = vkrt->runtime.displayViewportRect[2];
    uint32_t preservedViewportHeight = vkrt->runtime.displayViewportRect[3];

    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(vkrt->runtime.window, &framebufferWidth, &framebufferHeight);

    if (framebufferWidth == 0 || framebufferHeight == 0) {
        return VKRT_SUCCESS;
    }

    VKRT_Result result = vkrtConvertVkResult(vkDeviceWaitIdle(vkrt->core.device));
    if (result != VKRT_SUCCESS) {
        return result;
    }

    SwapChainState previousSwapChain = {0};
    GPUImageState previousImages = {0};
    GPUImageState nextImages = {0};
    captureSwapChainState(vkrt, &previousSwapChain);
    captureGPUImageState(vkrt, &previousImages);
    VkExtent2D previousRenderExtent = vkrt->runtime.renderExtent;
    VkBool32 previousSwapChainRetired = VK_FALSE;

    clearSwapChainBindings(vkrt);

    VkResult swapChainCreateVkResult = VK_SUCCESS;
    result = createSwapChainWithOld(vkrt, previousSwapChain.swapChain, &swapChainCreateVkResult);
    if (result != VKRT_SUCCESS && swapChainCreateVkResult == VK_ERROR_NATIVE_WINDOW_IN_USE_KHR) {
        LOG_INFO("Retrying swapchain recreation after releasing the previous swapchain");
        destroySwapChainState(vkrt, &previousSwapChain);
        previousSwapChainRetired = VK_TRUE;
        result = createSwapChain(vkrt);
    }
    if (result != VKRT_SUCCESS) goto restore_previous_state;
    if (previousSwapChain.swapChain != VK_NULL_HANDLE) {
        previousSwapChainRetired = VK_TRUE;
    }

    result = recreateSwapChainRuntimeResources(vkrt, &previousImages, &nextImages);
    if (result != VKRT_SUCCESS) goto restore_previous_state;

    finalizeRecreatedSwapChain(
        vkrt,
        &previousSwapChain,
        &previousImages,
        preservedViewportX,
        preservedViewportY,
        preservedViewportWidth,
        preservedViewportHeight
    );
    return VKRT_SUCCESS;

restore_previous_state:
    return restorePreviousSwapChainState(
        vkrt,
        &previousSwapChain,
        &previousImages,
        &nextImages,
        previousRenderExtent,
        previousSwapChainRetired,
        result
    );
}

void cleanupSwapChain(VKRT* vkrt) {
    if (!vkrt) return;

    for (size_t i = 0; i < vkrt->runtime.swapChainImageCount; i++) {
        if (vkrt->runtime.swapChainImageViews) {
            vkDestroyImageView(vkrt->core.device, vkrt->runtime.swapChainImageViews[i], NULL);
        }
    }

    if (vkrt->runtime.swapChain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(vkrt->core.device, vkrt->runtime.swapChain, NULL);
        vkrt->runtime.swapChain = VK_NULL_HANDLE;
    }

    free((void*)vkrt->runtime.swapChainImageViews);
    free((void*)vkrt->runtime.swapChainImages);
    vkrt->runtime.swapChainImageViews = NULL;
    vkrt->runtime.swapChainImages = NULL;
}

VKRT_Result createImageViews(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    vkrt->runtime.swapChainImageViews = (VkImageView*)malloc(vkrt->runtime.swapChainImageCount * sizeof(VkImageView));
    if (!vkrt->runtime.swapChainImageViews) {
        return VKRT_ERROR_OPERATION_FAILED;
    }

    for (size_t i = 0; i < vkrt->runtime.swapChainImageCount; i++) {
        VkImageViewCreateInfo imageViewCreateInfo = {0};
        imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        imageViewCreateInfo.image = vkrt->runtime.swapChainImages[i];
        imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        imageViewCreateInfo.format = vkrt->runtime.swapChainImageFormat;
        imageViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
        imageViewCreateInfo.subresourceRange.levelCount = 1;
        imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
        imageViewCreateInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(vkrt->core.device, &imageViewCreateInfo, NULL, &vkrt->runtime.swapChainImageViews[i]) !=
            VK_SUCCESS) {
            for (size_t j = 0; j < i; j++) {
                vkDestroyImageView(vkrt->core.device, vkrt->runtime.swapChainImageViews[j], NULL);
            }
            free((void*)vkrt->runtime.swapChainImageViews);
            vkrt->runtime.swapChainImageViews = NULL;
            return VKRT_ERROR_OPERATION_FAILED;
        }
    }

    return VKRT_SUCCESS;
}

VKRT_Result querySwapChainSupport(VKRT* vkrt, SwapChainSupportDetails* outSupportDetails) {
    if (!vkrt || !outSupportDetails) return VKRT_ERROR_INVALID_ARGUMENT;

    SwapChainSupportDetails supportDetails = {
        .capabilities = {.currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR},
    };
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
            vkrt->core.physicalDevice,
            vkrt->runtime.surface,
            &supportDetails.capabilities
        ) != VK_SUCCESS) {
        LOG_ERROR("Failed to query surface capabilities");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    uint32_t formatCount = 0;
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(vkrt->core.physicalDevice, vkrt->runtime.surface, &formatCount, NULL) !=
        VK_SUCCESS) {
        LOG_ERROR("Failed to query surface formats");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    if (formatCount) {
        supportDetails.formats = (VkSurfaceFormatKHR*)malloc(formatCount * sizeof(VkSurfaceFormatKHR));
        if (!supportDetails.formats) {
            return VKRT_ERROR_OPERATION_FAILED;
        }
        if (vkGetPhysicalDeviceSurfaceFormatsKHR(
                vkrt->core.physicalDevice,
                vkrt->runtime.surface,
                &formatCount,
                supportDetails.formats
            ) != VK_SUCCESS) {
            free(supportDetails.formats);
            LOG_ERROR("Failed to query surface formats");
            return VKRT_ERROR_OPERATION_FAILED;
        }
        supportDetails.formatCount = formatCount;
    }

    uint32_t presentModeCount = 0;
    if (vkGetPhysicalDeviceSurfacePresentModesKHR(
            vkrt->core.physicalDevice,
            vkrt->runtime.surface,
            &presentModeCount,
            NULL
        ) != VK_SUCCESS) {
        free(supportDetails.formats);
        LOG_ERROR("Failed to query present modes");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    if (presentModeCount) {
        supportDetails.presentModes = (VkPresentModeKHR*)malloc(presentModeCount * sizeof(VkPresentModeKHR));
        if (!supportDetails.presentModes) {
            free(supportDetails.formats);
            return VKRT_ERROR_OPERATION_FAILED;
        }
        if (vkGetPhysicalDeviceSurfacePresentModesKHR(
                vkrt->core.physicalDevice,
                vkrt->runtime.surface,
                &presentModeCount,
                supportDetails.presentModes
            ) != VK_SUCCESS) {
            free(supportDetails.formats);
            free(supportDetails.presentModes);
            LOG_ERROR("Failed to query present modes");
            return VKRT_ERROR_OPERATION_FAILED;
        }
        supportDetails.presentModeCount = presentModeCount;
    }

    *outSupportDetails = supportDetails;
    return VKRT_SUCCESS;
}

VKRT_Result chooseSwapSurfaceFormat(
    const SwapChainSupportDetails* supportDetails,
    VkSurfaceFormatKHR* outSurfaceFormat
) {
    if (!supportDetails || !outSurfaceFormat) return VKRT_ERROR_INVALID_ARGUMENT;
    if (supportDetails->formatCount == 0 || !supportDetails->formats) {
        LOG_ERROR("No swapchain surface formats are available");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    static const VkFormat preferredFormats[] = {
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_FORMAT_A2B10G10R10_UNORM_PACK32,
        VK_FORMAT_A2R10G10B10_UNORM_PACK32,
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_A8B8G8R8_UNORM_PACK32,
    };

    uint32_t preferredFormatCount = (uint32_t)(sizeof(preferredFormats) / sizeof(preferredFormats[0]));
    for (uint32_t preferredIndex = 0; preferredIndex < preferredFormatCount; preferredIndex++) {
        VkFormat preferredFormat = preferredFormats[preferredIndex];
        for (uint32_t formatIndex = 0; formatIndex < supportDetails->formatCount; formatIndex++) {
            VkSurfaceFormatKHR candidate = supportDetails->formats[formatIndex];
            if (candidate.format == preferredFormat && candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                *outSurfaceFormat = candidate;
                return VKRT_SUCCESS;
            }
        }
    }

    for (uint32_t formatIndex = 0; formatIndex < supportDetails->formatCount; formatIndex++) {
        VkSurfaceFormatKHR candidate = supportDetails->formats[formatIndex];
        if (candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            *outSurfaceFormat = candidate;
            LOG_INFO(
                "Falling back to non-preferred swapchain format: %s (%d), color space: %s "
                "(%d)",
                swapChainFormatName(candidate.format),
                (int)candidate.format,
                swapChainColorSpaceName(candidate.colorSpace),
                (int)candidate.colorSpace
            );
            return VKRT_SUCCESS;
        }
    }

    LOG_ERROR("No VK_COLOR_SPACE_SRGB_NONLINEAR_KHR swapchain surface format is available");
    return VKRT_ERROR_OPERATION_FAILED;
}

static VkPresentModeKHR chooseRankedPresentMode(
    const SwapChainSupportDetails* supportDetails,
    const VkPresentModeKHR* ranking,
    size_t rankingCount
) {
    if (!supportDetails) return VK_PRESENT_MODE_FIFO_KHR;
    for (size_t i = 0; i < rankingCount; i++) {
        if (hasPresentMode(supportDetails, ranking[i])) return ranking[i];
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkPresentModeKHR chooseSwapPresentMode(
    const SwapChainSupportDetails* supportDetails,
    VkBool32 useRenderPresentProfile
) {
    if (useRenderPresentProfile) {
        return chooseRankedPresentMode(
            supportDetails,
            kRenderPresentModeRanking,
            VKRT_ARRAY_COUNT(kRenderPresentModeRanking)
        );
    }

    return chooseRankedPresentMode(
        supportDetails,
        kInteractivePresentModeRanking,
        VKRT_ARRAY_COUNT(kInteractivePresentModeRanking)
    );
}

VkExtent2D chooseSwapExtent(VKRT* vkrt, const SwapChainSupportDetails* supportDetails) {
    VkSurfaceCapabilitiesKHR capabilities = supportDetails->capabilities;
    if (capabilities.currentExtent.width != UINT32_MAX) {
        return capabilities.currentExtent;
    }
    int width;
    int height;
    glfwGetFramebufferSize(vkrt->runtime.window, &width, &height);

    VkExtent2D actualExtent = {(uint32_t)width, (uint32_t)height};

    if (actualExtent.width < capabilities.minImageExtent.width) {
        actualExtent.width = capabilities.minImageExtent.width;
    } else if (actualExtent.width > capabilities.maxImageExtent.width) {
        actualExtent.width = capabilities.maxImageExtent.width;
    }

    if (actualExtent.height < capabilities.minImageExtent.height) {
        actualExtent.height = capabilities.minImageExtent.height;
    } else if (actualExtent.height > capabilities.maxImageExtent.height) {
        actualExtent.height = capabilities.maxImageExtent.height;
    }

    return actualExtent;
}
