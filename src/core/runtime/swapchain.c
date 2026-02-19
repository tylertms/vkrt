#include "swapchain.h"
#include "command.h"
#include "descriptor.h"
#include "device.h"
#include "scene.h"

#include <stdio.h>
#include <stdlib.h>

void createSwapChain(VKRT* vkrt) {
    SwapChainSupportDetails supportDetails = querySwapChainSupport(vkrt);

    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(&supportDetails);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(&supportDetails, vkrt->runtime.vsync);
    VkExtent2D extent = chooseSwapExtent(vkrt, &supportDetails);

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
        perror("ERROR: Failed to create swapchain");
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
        perror("ERROR: Failed to allocate render-finished semaphores");
        exit(EXIT_FAILURE);
    }

    VkSemaphoreCreateInfo semaphoreCreateInfo = {0};
    semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (size_t i = 0; i < vkrt->runtime.swapChainImageCount; i++) {
        if (vkCreateSemaphore(vkrt->core.device, &semaphoreCreateInfo, NULL, &vkrt->runtime.renderFinishedSemaphores[i]) != VK_SUCCESS) {
            perror("ERROR: Failed to recreate render-finished semaphores");
            exit(EXIT_FAILURE);
        }
    }

    updateDescriptorSet(vkrt);
    createFramebuffers(vkrt);
    VKRT_setRenderViewport(vkrt, 0, 0, vkrt->runtime.swapChainExtent.width, vkrt->runtime.swapChainExtent.height);

    vkrt->core.sceneData->frameNumber = -1;
}

void cleanupSwapChain(VKRT* vkrt) {
    for (size_t i = 0; i < vkrt->runtime.swapChainImageCount; i++) {
        vkDestroyFramebuffer(vkrt->core.device, vkrt->runtime.framebuffers[i], NULL);
        vkDestroyImageView(vkrt->core.device, vkrt->runtime.swapChainImageViews[i], NULL);
    }

    vkDestroySwapchainKHR(vkrt->core.device, vkrt->runtime.swapChain, NULL);

    free(vkrt->runtime.swapChainImageViews);
    free(vkrt->runtime.swapChainImages);

    vkDestroyImageView(vkrt->core.device, vkrt->core.storageImageView, NULL);
    vkDestroyImage(vkrt->core.device, vkrt->core.storageImage, NULL);
    vkFreeMemory(vkrt->core.device, vkrt->core.storageImageMemory, NULL);
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
            perror("ERROR: Failed to create swapchain image views");
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
            perror("ERROR: Failed to create framebuffer");
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
        perror("ERROR: No swapchain surface formats are available");
        exit(EXIT_FAILURE);
    }

    for (uint32_t i = 0; i < supportDetails->formatCount; i++) {
        VkSurfaceFormatKHR format = supportDetails->formats[i];
        if (format.format == VK_FORMAT_R32G32B32A32_SFLOAT && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }

    return supportDetails->formats[0];
}

VkPresentModeKHR chooseSwapPresentMode(SwapChainSupportDetails* supportDetails, uint8_t vsync) {
    if (vsync) {
        return VK_PRESENT_MODE_FIFO_KHR;
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
