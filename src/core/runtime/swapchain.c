#include "swapchain.h"
#include "command.h"
#include "descriptor.h"
#include "device.h"
#include "scene.h"
#include "debug.h"

#include <stdio.h>
#include <stdlib.h>

static const char* swapchainFormatName(VkFormat format) {
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

static const char* swapchainColorSpaceName(VkColorSpaceKHR colorSpace) {
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

void createSwapChain(VKRT* vkrt) {
    SwapChainSupportDetails supportDetails = querySwapChainSupport(vkrt);

    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(&supportDetails);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(&supportDetails, vkrt->runtime.vsync);
    VkExtent2D extent = chooseSwapExtent(vkrt, &supportDetails);
    vkrt->runtime.presentMode = presentMode;
    vkrt->runtime.displayRefreshHz = queryDisplayRefreshHz(vkrt);
    if (!vkrt->runtime.swapchainFormatLogInitialized ||
        vkrt->runtime.lastLoggedSwapchainFormat != surfaceFormat.format ||
        vkrt->runtime.lastLoggedSwapchainColorSpace != surfaceFormat.colorSpace) {
        LOG_INFO("Swapchain format selected: %s (%d), color space: %s (%d)",
            swapchainFormatName(surfaceFormat.format), (int)surfaceFormat.format,
            swapchainColorSpaceName(surfaceFormat.colorSpace), (int)surfaceFormat.colorSpace);
        vkrt->runtime.swapchainFormatLogInitialized = VK_TRUE;
        vkrt->runtime.lastLoggedSwapchainFormat = surfaceFormat.format;
        vkrt->runtime.lastLoggedSwapchainColorSpace = surfaceFormat.colorSpace;
    }

    free(supportDetails.formats);
    free(supportDetails.presentModes);

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

    QueueFamily indices = findQueueFamilies(vkrt);
    uint32_t queueFamilyIndices[] = {indices.graphics, indices.present};

    if (indices.graphics != indices.present) {
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

    if (vkCreateSwapchainKHR(vkrt->core.device, &swapChainCreateInfo, NULL, &vkrt->runtime.swapChain) != VK_SUCCESS) {
        LOG_ERROR("Failed to create swapchain");
        exit(EXIT_FAILURE);
    }

    vkGetSwapchainImagesKHR(vkrt->core.device, vkrt->runtime.swapChain, &imageCount, NULL);
    vkrt->runtime.swapChainImages = (VkImage*)malloc(imageCount * sizeof(VkImage));
    vkrt->runtime.swapChainImageCount = imageCount;
    vkGetSwapchainImagesKHR(vkrt->core.device, vkrt->runtime.swapChain, &imageCount, vkrt->runtime.swapChainImages);

    vkrt->runtime.swapChainImageFormat = surfaceFormat.format;
    vkrt->runtime.swapChainExtent = extent;
}

void recreateSwapChain(VKRT* vkrt) {
    glfwGetFramebufferSize(vkrt->runtime.window,
        (int*)&vkrt->state.camera.width,
        (int*)&vkrt->state.camera.height);

    while (vkrt->state.camera.width == 0 || vkrt->state.camera.height == 0) {
        glfwGetFramebufferSize(vkrt->runtime.window,
            (int*)&vkrt->state.camera.width,
            (int*)&vkrt->state.camera.height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(vkrt->core.device);

    size_t oldSwapChainImageCount = vkrt->runtime.swapChainImageCount;

    for (size_t i = 0; i < oldSwapChainImageCount; i++) {
        vkDestroySemaphore(vkrt->core.device, vkrt->runtime.renderFinishedSemaphores[i], NULL);
    }
    free(vkrt->runtime.renderFinishedSemaphores);

    cleanupSwapChain(vkrt);

    createSwapChain(vkrt);
    createImageViews(vkrt);
    createStorageImage(vkrt);

    vkrt->runtime.renderFinishedSemaphores = (VkSemaphore*)malloc(vkrt->runtime.swapChainImageCount * sizeof(VkSemaphore));
    if (!vkrt->runtime.renderFinishedSemaphores) {
        LOG_ERROR("Failed to allocate render-finished semaphores");
        exit(EXIT_FAILURE);
    }

    VkSemaphoreCreateInfo semaphoreCreateInfo = {0};
    semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (size_t i = 0; i < vkrt->runtime.swapChainImageCount; i++) {
        if (vkCreateSemaphore(vkrt->core.device, &semaphoreCreateInfo, NULL, &vkrt->runtime.renderFinishedSemaphores[i]) != VK_SUCCESS) {
            LOG_ERROR("Failed to recreate render-finished semaphores");
            exit(EXIT_FAILURE);
        }
    }

    updateDescriptorSet(vkrt);
    createFramebuffers(vkrt);
    VKRT_setRenderViewport(vkrt, 0, 0, vkrt->runtime.swapChainExtent.width, vkrt->runtime.swapChainExtent.height);

    resetSceneData(vkrt);
    vkrt->state.displayRenderTimeMs = 0.0f;
    vkrt->state.displayFrameTimeMs = 0.0f;
    vkrt->state.lastFrameTimestamp = 0;
    vkrt->state.autoSPPControlMs = 0.0f;
    vkrt->state.autoSPPFramesUntilNextAdjust = 0;
    vkrt->runtime.autoSPPFastAdaptFrames = 8;
}

void cleanupSwapChain(VKRT* vkrt) {
    for (size_t i = 0; i < vkrt->runtime.swapChainImageCount; i++) {
        vkDestroyFramebuffer(vkrt->core.device, vkrt->runtime.framebuffers[i], NULL);
        vkDestroyImageView(vkrt->core.device, vkrt->runtime.swapChainImageViews[i], NULL);
    }

    vkDestroySwapchainKHR(vkrt->core.device, vkrt->runtime.swapChain, NULL);

    free(vkrt->runtime.swapChainImageViews);
    free(vkrt->runtime.swapChainImages);

    for (uint32_t i = 0; i < 2; i++) {
        vkDestroyImageView(vkrt->core.device, vkrt->core.accumulationImageViews[i], NULL);
        vkDestroyImage(vkrt->core.device, vkrt->core.accumulationImages[i], NULL);
        vkFreeMemory(vkrt->core.device, vkrt->core.accumulationImageMemories[i], NULL);
        vkrt->core.accumulationImageViews[i] = VK_NULL_HANDLE;
        vkrt->core.accumulationImages[i] = VK_NULL_HANDLE;
        vkrt->core.accumulationImageMemories[i] = VK_NULL_HANDLE;
    }
    if (vkrt->core.storageImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(vkrt->core.device, vkrt->core.storageImageView, NULL);
        vkrt->core.storageImageView = VK_NULL_HANDLE;
    }
    if (vkrt->core.storageImage != VK_NULL_HANDLE) {
        vkDestroyImage(vkrt->core.device, vkrt->core.storageImage, NULL);
        vkrt->core.storageImage = VK_NULL_HANDLE;
    }
    if (vkrt->core.storageImageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(vkrt->core.device, vkrt->core.storageImageMemory, NULL);
        vkrt->core.storageImageMemory = VK_NULL_HANDLE;
    }
}

void createImageViews(VKRT* vkrt) {
    vkrt->runtime.swapChainImageViews = (VkImageView*)malloc(vkrt->runtime.swapChainImageCount * sizeof(VkImageView));

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
            LOG_ERROR("Failed to create swapchain image views");
            exit(EXIT_FAILURE);
        }
    }
}

void createFramebuffers(VKRT* vkrt) {
    vkrt->runtime.framebuffers = (VkFramebuffer*)malloc(vkrt->runtime.swapChainImageCount * sizeof(VkFramebuffer));

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
            LOG_ERROR("Failed to create framebuffer");
            exit(EXIT_FAILURE);
        }
    }
}

SwapChainSupportDetails querySwapChainSupport(VKRT* vkrt) {
    SwapChainSupportDetails supportDetails = {0};

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vkrt->core.physicalDevice, vkrt->runtime.surface, &supportDetails.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(vkrt->core.physicalDevice, vkrt->runtime.surface, &formatCount, NULL);

    if (formatCount) {
        supportDetails.formats = (VkSurfaceFormatKHR*)malloc(formatCount * sizeof(VkSurfaceFormatKHR));
        vkGetPhysicalDeviceSurfaceFormatsKHR(vkrt->core.physicalDevice, vkrt->runtime.surface, &formatCount, supportDetails.formats);
        supportDetails.formatCount = formatCount;
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(vkrt->core.physicalDevice, vkrt->runtime.surface, &presentModeCount, NULL);

    if (presentModeCount) {
        supportDetails.presentModes = (VkPresentModeKHR*)malloc(presentModeCount * sizeof(VkPresentModeKHR));
        vkGetPhysicalDeviceSurfacePresentModesKHR(vkrt->core.physicalDevice, vkrt->runtime.surface, &presentModeCount, supportDetails.presentModes);
        supportDetails.presentModeCount = presentModeCount;
    }

    return supportDetails;
}

VkSurfaceFormatKHR chooseSwapSurfaceFormat(SwapChainSupportDetails* supportDetails) {
    if (supportDetails->formatCount == 0 || !supportDetails->formats) {
        LOG_ERROR("No swapchain surface formats are available");
        exit(EXIT_FAILURE);
    }

    static const VkFormat preferredFormats[] = {
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_FORMAT_A2B10G10R10_UNORM_PACK32,
        VK_FORMAT_A2R10G10B10_UNORM_PACK32,
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_A8B8G8R8_UNORM_PACK32,
    };

    for (uint32_t preferredIndex = 0; preferredIndex < COUNT_OF(preferredFormats); preferredIndex++) {
        VkFormat preferredFormat = preferredFormats[preferredIndex];
        for (uint32_t formatIndex = 0; formatIndex < supportDetails->formatCount; formatIndex++) {
            VkSurfaceFormatKHR candidate = supportDetails->formats[formatIndex];
            if (candidate.format == preferredFormat &&
                candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return candidate;
            }
        }
    }

    LOG_ERROR("No VK_COLOR_SPACE_SRGB_NONLINEAR_KHR surface format matches the supported non-sRGB format list");
    exit(EXIT_FAILURE);
}

VkPresentModeKHR chooseSwapPresentMode(SwapChainSupportDetails* supportDetails, uint8_t vsync) {
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

VkExtent2D chooseSwapExtent(VKRT* vkrt, SwapChainSupportDetails* supportDetails) {
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
