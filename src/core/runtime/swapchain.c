#include "swapchain.h"
#include "images.h"
#include "descriptor.h"
#include "sync.h"
#include "scene.h"
#include "debug.h"

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

static float queryDisplayRefreshHz(VKRT* vkrt) {
    GLFWmonitor* monitor = glfwGetWindowMonitor(vkrt->runtime.window);
    if (!monitor) monitor = glfwGetPrimaryMonitor();
    if (!monitor) return 60.0f;

    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    if (!mode || mode->refreshRate <= 0) return 60.0f;
    return (float)mode->refreshRate;
}

VKRT_Result createSwapChain(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    SwapChainSupportDetails supportDetails = {0};
    if (querySwapChainSupport(vkrt, &supportDetails) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;

    VkSurfaceFormatKHR surfaceFormat = {0};
    if (chooseSwapSurfaceFormat(&supportDetails, &surfaceFormat) != VKRT_SUCCESS) {
        free(supportDetails.formats);
        free(supportDetails.presentModes);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    VkPresentModeKHR presentMode = chooseSwapPresentMode(&supportDetails, vkrt->runtime.vsync);
    VkExtent2D extent = chooseSwapExtent(vkrt, &supportDetails);
    vkrt->runtime.presentMode = presentMode;
    vkrt->runtime.displayRefreshHz = queryDisplayRefreshHz(vkrt);
    if (!vkrt->runtime.swapChainFormatLogInitialized ||
        vkrt->runtime.lastLoggedSwapChainFormat != surfaceFormat.format ||
        vkrt->runtime.lastLoggedSwapChainColorSpace != surfaceFormat.colorSpace) {
        LOG_INFO("Swapchain format selected: %s (%d), color space: %s (%d)",
            swapChainFormatName(surfaceFormat.format), (int)surfaceFormat.format,
            swapChainColorSpaceName(surfaceFormat.colorSpace), (int)surfaceFormat.colorSpace);
        vkrt->runtime.swapChainFormatLogInitialized = VK_TRUE;
        vkrt->runtime.lastLoggedSwapChainFormat = surfaceFormat.format;
        vkrt->runtime.lastLoggedSwapChainColorSpace = surfaceFormat.colorSpace;
    }

    uint32_t imageCount = supportDetails.capabilities.minImageCount + 1;
    if (supportDetails.capabilities.maxImageCount && imageCount > supportDetails.capabilities.maxImageCount) {
        imageCount = supportDetails.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR swapChainCreateInfo = {0};
    swapChainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapChainCreateInfo.surface = vkrt->runtime.surface;
    swapChainCreateInfo.minImageCount = imageCount;
    swapChainCreateInfo.imageFormat = surfaceFormat.format;
    swapChainCreateInfo.imageColorSpace = surfaceFormat.colorSpace;
    swapChainCreateInfo.imageExtent = extent;
    swapChainCreateInfo.imageArrayLayers = 1;
    swapChainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    uint32_t queueFamilyIndices[] = {vkrt->core.indices.graphics, vkrt->core.indices.present};

    if (vkrt->core.indices.graphics != vkrt->core.indices.present) {
        swapChainCreateInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        swapChainCreateInfo.queueFamilyIndexCount = 2;
        swapChainCreateInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        swapChainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapChainCreateInfo.queueFamilyIndexCount = 0;
        swapChainCreateInfo.pQueueFamilyIndices = NULL;
    }

    swapChainCreateInfo.preTransform = supportDetails.capabilities.currentTransform;
    swapChainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapChainCreateInfo.presentMode = presentMode;
    swapChainCreateInfo.clipped = VK_TRUE;
    swapChainCreateInfo.oldSwapchain = VK_NULL_HANDLE;

    free(supportDetails.formats);
    free(supportDetails.presentModes);

    if (vkCreateSwapchainKHR(vkrt->core.device, &swapChainCreateInfo, NULL, &vkrt->runtime.swapChain) != VK_SUCCESS) {
        LOG_ERROR("Failed to create swapchain");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    if (vkGetSwapchainImagesKHR(vkrt->core.device, vkrt->runtime.swapChain, &imageCount, NULL) != VK_SUCCESS || imageCount == 0) {
        vkDestroySwapchainKHR(vkrt->core.device, vkrt->runtime.swapChain, NULL);
        vkrt->runtime.swapChain = VK_NULL_HANDLE;
        return VKRT_ERROR_OPERATION_FAILED;
    }

    vkrt->runtime.swapChainImages = (VkImage*)malloc(imageCount * sizeof(VkImage));
    if (!vkrt->runtime.swapChainImages) {
        vkDestroySwapchainKHR(vkrt->core.device, vkrt->runtime.swapChain, NULL);
        vkrt->runtime.swapChain = VK_NULL_HANDLE;
        return VKRT_ERROR_OPERATION_FAILED;
    }
    vkrt->runtime.swapChainImageCount = imageCount;

    if (vkGetSwapchainImagesKHR(vkrt->core.device, vkrt->runtime.swapChain, &imageCount, vkrt->runtime.swapChainImages) != VK_SUCCESS) {
        free(vkrt->runtime.swapChainImages);
        vkrt->runtime.swapChainImages = NULL;
        vkrt->runtime.swapChainImageCount = 0;
        vkDestroySwapchainKHR(vkrt->core.device, vkrt->runtime.swapChain, NULL);
        vkrt->runtime.swapChain = VK_NULL_HANDLE;
        return VKRT_ERROR_OPERATION_FAILED;
    }

    vkrt->runtime.swapChainImageFormat = surfaceFormat.format;
    vkrt->runtime.swapChainExtent = extent;
    if (!vkrt->state.renderModeActive || vkrt->runtime.renderExtent.width == 0 || vkrt->runtime.renderExtent.height == 0) {
        vkrt->runtime.renderExtent = extent;
    }

    return VKRT_SUCCESS;
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

    while (framebufferWidth == 0 || framebufferHeight == 0) {
        glfwGetFramebufferSize(vkrt->runtime.window, &framebufferWidth, &framebufferHeight);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(vkrt->core.device);

    size_t oldSwapChainImageCount = vkrt->runtime.swapChainImageCount;
    if (resetRenderFinishedSemaphores(vkrt, oldSwapChainImageCount, 0) != VKRT_SUCCESS) {
        return VKRT_ERROR_OPERATION_FAILED;
    }

    cleanupSwapChain(vkrt);

    if (createSwapChain(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;
    if (createImageViews(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;

    if (!vkrt->state.renderModeActive) {
        destroyGPUImages(vkrt);
        if (createGPUImages(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;
    }

    if (resetRenderFinishedSemaphores(
            vkrt,
            0,
            vkrt->runtime.swapChainImageCount) != VKRT_SUCCESS) {
        return VKRT_ERROR_OPERATION_FAILED;
    }

    if (updateAllDescriptorSets(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;
    if (createFramebuffers(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;

    if (vkrt->state.renderModeActive) {
        VKRT_setRenderViewport(vkrt,
            preservedViewportX,
            preservedViewportY,
            preservedViewportWidth,
            preservedViewportHeight);
    } else {
        VKRT_setRenderViewport(vkrt, 0, 0, vkrt->runtime.swapChainExtent.width, vkrt->runtime.swapChainExtent.height);
    }

    vkrt->state.displayRenderTimeMs = 0.0f;
    vkrt->state.displayFrameTimeMs = 0.0f;
    vkrt->state.lastFrameTimestamp = 0;
    vkrt->state.autoSPPControlMs = 0.0f;
    vkrt->state.autoSPPFramesUntilNextAdjust = 0;
    vkrt->runtime.autoSPPFastAdaptFrames = 8;
    if (!vkrt->state.renderModeActive) {
        resetSceneData(vkrt);
    }

    return VKRT_SUCCESS;
}

void cleanupSwapChain(VKRT* vkrt) {
    if (!vkrt) return;

    for (size_t i = 0; i < vkrt->runtime.swapChainImageCount; i++) {
        if (vkrt->runtime.framebuffers) {
            vkDestroyFramebuffer(vkrt->core.device, vkrt->runtime.framebuffers[i], NULL);
        }
        if (vkrt->runtime.swapChainImageViews) {
            vkDestroyImageView(vkrt->core.device, vkrt->runtime.swapChainImageViews[i], NULL);
        }
    }

    if (vkrt->runtime.swapChain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(vkrt->core.device, vkrt->runtime.swapChain, NULL);
        vkrt->runtime.swapChain = VK_NULL_HANDLE;
    }

    free(vkrt->runtime.framebuffers);
    free(vkrt->runtime.swapChainImageViews);
    free(vkrt->runtime.swapChainImages);
    vkrt->runtime.framebuffers = NULL;
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

        if (vkCreateImageView(vkrt->core.device, &imageViewCreateInfo, NULL, &vkrt->runtime.swapChainImageViews[i]) != VK_SUCCESS) {
            for (size_t j = 0; j < i; j++) {
                vkDestroyImageView(vkrt->core.device, vkrt->runtime.swapChainImageViews[j], NULL);
            }
            free(vkrt->runtime.swapChainImageViews);
            vkrt->runtime.swapChainImageViews = NULL;
            return VKRT_ERROR_OPERATION_FAILED;
        }
    }

    return VKRT_SUCCESS;
}

VKRT_Result createFramebuffers(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    vkrt->runtime.framebuffers = (VkFramebuffer*)malloc(vkrt->runtime.swapChainImageCount * sizeof(VkFramebuffer));
    if (!vkrt->runtime.framebuffers) {
        return VKRT_ERROR_OPERATION_FAILED;
    }

    for (size_t i = 0; i < vkrt->runtime.swapChainImageCount; i++) {
        VkFramebufferCreateInfo framebufferCreateInfo = {0};
        framebufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferCreateInfo.renderPass = vkrt->runtime.renderPass;
        framebufferCreateInfo.attachmentCount = 1;
        framebufferCreateInfo.pAttachments = &vkrt->runtime.swapChainImageViews[i];
        framebufferCreateInfo.width = vkrt->runtime.swapChainExtent.width;
        framebufferCreateInfo.height = vkrt->runtime.swapChainExtent.height;
        framebufferCreateInfo.layers = 1;

        if (vkCreateFramebuffer(vkrt->core.device, &framebufferCreateInfo, NULL, &vkrt->runtime.framebuffers[i]) != VK_SUCCESS) {
            for (size_t j = 0; j < i; j++) {
                vkDestroyFramebuffer(vkrt->core.device, vkrt->runtime.framebuffers[j], NULL);
            }
            free(vkrt->runtime.framebuffers);
            vkrt->runtime.framebuffers = NULL;
            return VKRT_ERROR_OPERATION_FAILED;
        }
    }

    return VKRT_SUCCESS;
}

VKRT_Result querySwapChainSupport(VKRT* vkrt, SwapChainSupportDetails* outSupportDetails) {
    if (!vkrt || !outSupportDetails) return VKRT_ERROR_INVALID_ARGUMENT;

    SwapChainSupportDetails supportDetails = {0};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vkrt->core.physicalDevice, vkrt->runtime.surface, &supportDetails.capabilities);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(vkrt->core.physicalDevice, vkrt->runtime.surface, &formatCount, NULL);

    if (formatCount) {
        supportDetails.formats = (VkSurfaceFormatKHR*)malloc(formatCount * sizeof(VkSurfaceFormatKHR));
        if (!supportDetails.formats) {
            return VKRT_ERROR_OPERATION_FAILED;
        }
        vkGetPhysicalDeviceSurfaceFormatsKHR(vkrt->core.physicalDevice, vkrt->runtime.surface, &formatCount, supportDetails.formats);
        supportDetails.formatCount = formatCount;
    }

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(vkrt->core.physicalDevice, vkrt->runtime.surface, &presentModeCount, NULL);

    if (presentModeCount) {
        supportDetails.presentModes = (VkPresentModeKHR*)malloc(presentModeCount * sizeof(VkPresentModeKHR));
        if (!supportDetails.presentModes) {
            free(supportDetails.formats);
            return VKRT_ERROR_OPERATION_FAILED;
        }
        vkGetPhysicalDeviceSurfacePresentModesKHR(
            vkrt->core.physicalDevice,
            vkrt->runtime.surface,
            &presentModeCount,
            supportDetails.presentModes);
        supportDetails.presentModeCount = presentModeCount;
    }

    *outSupportDetails = supportDetails;
    return VKRT_SUCCESS;
}

VKRT_Result chooseSwapSurfaceFormat(const SwapChainSupportDetails* supportDetails, VkSurfaceFormatKHR* outSurfaceFormat) {
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

    for (uint32_t preferredIndex = 0; preferredIndex < VKRT_ARRAY_COUNT(preferredFormats); preferredIndex++) {
        VkFormat preferredFormat = preferredFormats[preferredIndex];
        for (uint32_t formatIndex = 0; formatIndex < supportDetails->formatCount; formatIndex++) {
            VkSurfaceFormatKHR candidate = supportDetails->formats[formatIndex];
            if (candidate.format == preferredFormat &&
                candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                *outSurfaceFormat = candidate;
                return VKRT_SUCCESS;
            }
        }
    }

    for (uint32_t formatIndex = 0; formatIndex < supportDetails->formatCount; formatIndex++) {
        VkSurfaceFormatKHR candidate = supportDetails->formats[formatIndex];
        if (candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            *outSurfaceFormat = candidate;
            LOG_INFO("Falling back to non-preferred swapchain format: %s (%d), color space: %s (%d)",
                swapChainFormatName(candidate.format), (int)candidate.format,
                swapChainColorSpaceName(candidate.colorSpace), (int)candidate.colorSpace);
            return VKRT_SUCCESS;
        }
    }

    *outSurfaceFormat = supportDetails->formats[0];
    LOG_INFO("Falling back to first available swapchain format: %s (%d), color space: %s (%d)",
        swapChainFormatName(outSurfaceFormat->format), (int)outSurfaceFormat->format,
        swapChainColorSpaceName(outSurfaceFormat->colorSpace), (int)outSurfaceFormat->colorSpace);
    return VKRT_SUCCESS;
}

VkPresentModeKHR chooseSwapPresentMode(const SwapChainSupportDetails* supportDetails, uint8_t vsync) {
    if (vsync) {
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    for (uint32_t i = 0; i < supportDetails->presentModeCount; i++) {
        if (supportDetails->presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
            return supportDetails->presentModes[i];
        }
    }

    for (uint32_t i = 0; i < supportDetails->presentModeCount; i++) {
        if (supportDetails->presentModes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) {
            return supportDetails->presentModes[i];
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D chooseSwapExtent(VKRT* vkrt, const SwapChainSupportDetails* supportDetails) {
    VkSurfaceCapabilitiesKHR capabilities = supportDetails->capabilities;
    if (capabilities.currentExtent.width != UINT32_MAX) {
        return capabilities.currentExtent;
    } else {
        int width, height;
        glfwGetFramebufferSize(vkrt->runtime.window, &width, &height);

        VkExtent2D actualExtent = {
            (uint32_t)width,
            (uint32_t)height
        };

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
}
