#include "swapchain.h"
#include "command.h"
#include "descriptor.h"
#include "device.h"
#include "interface.h"

#include <stdio.h>
#include <stdlib.h>

void createSwapChain(VKRT* vkrt) {
    SwapChainSupportDetails supportDetails = querySwapChainSupport(vkrt);

    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(&supportDetails);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(&supportDetails, vkrt->vsync);
    VkExtent2D extent = chooseSwapExtent(vkrt, &supportDetails);

    free(supportDetails.formats);
    free(supportDetails.presentModes);

    uint32_t imageCount = supportDetails.capabilities.minImageCount + 1;
    if (supportDetails.capabilities.maxImageCount && imageCount > supportDetails.capabilities.maxImageCount) {
        imageCount = supportDetails.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR swapChainCreateInfo = {0};
    swapChainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapChainCreateInfo.surface = vkrt->surface;
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

    if (vkCreateSwapchainKHR(vkrt->device, &swapChainCreateInfo, NULL, &vkrt->swapChain) != VK_SUCCESS) {
        perror("ERROR: Failed to create swapchain");
        exit(EXIT_FAILURE);
    }

    vkGetSwapchainImagesKHR(vkrt->device, vkrt->swapChain, &imageCount, NULL);
    vkrt->swapChainImages = (VkImage*)malloc(imageCount * sizeof(VkImage));
    vkrt->swapChainImageCount = imageCount;
    vkGetSwapchainImagesKHR(vkrt->device, vkrt->swapChain, &imageCount, vkrt->swapChainImages);

    vkrt->swapChainImageFormat = surfaceFormat.format;
    vkrt->swapChainExtent = extent;
}

void recreateSwapChain(VKRT* vkrt) {
    glfwGetFramebufferSize(vkrt->window, 
        (int*)&vkrt->camera.width, 
        (int*)&vkrt->camera.height);

    while (vkrt->camera.width == 0 || vkrt->camera.height == 0) {
        glfwGetFramebufferSize(vkrt->window, 
            (int*)&vkrt->camera.width, 
            (int*)&vkrt->camera.height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(vkrt->device);

    cleanupSwapChain(vkrt);

    createSwapChain(vkrt);
    createImageViews(vkrt);
    createStorageImage(vkrt);
    updateDescriptorSet(vkrt);
    createFramebuffers(vkrt);
    updateMatricesFromCamera(vkrt);
}

void cleanupSwapChain(VKRT* vkrt) {
    for (size_t i = 0; i < vkrt->swapChainImageCount; i++) {
        vkDestroyFramebuffer(vkrt->device, vkrt->framebuffers[i], NULL);
        vkDestroyImageView(vkrt->device, vkrt->swapChainImageViews[i], NULL);
    }

    vkDestroySwapchainKHR(vkrt->device, vkrt->swapChain, NULL);

    free(vkrt->swapChainImageViews);
    free(vkrt->swapChainImages);
    
    vkDestroyImageView(vkrt->device, vkrt->storageImageView, NULL);
    vkDestroyImage(vkrt->device, vkrt->storageImage, NULL);
    vkFreeMemory(vkrt->device, vkrt->storageImageMemory, NULL);
}

void createImageViews(VKRT* vkrt) {
    vkrt->swapChainImageViews = (VkImageView*)malloc(vkrt->swapChainImageCount * sizeof(VkImageView));

    for (size_t i = 0; i < vkrt->swapChainImageCount; i++) {
        VkImageViewCreateInfo imageViewCreateInfo = {0};
        imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        imageViewCreateInfo.image = vkrt->swapChainImages[i];
        imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        imageViewCreateInfo.format = vkrt->swapChainImageFormat;
        imageViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
        imageViewCreateInfo.subresourceRange.levelCount = 1;
        imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
        imageViewCreateInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(vkrt->device, &imageViewCreateInfo, NULL, &vkrt->swapChainImageViews[i]) != VK_SUCCESS) {
            perror("ERROR: Failed to create swapchain image views");
            exit(EXIT_FAILURE);
        }
    }
}

void createFramebuffers(VKRT* vkrt) {
    vkrt->framebuffers = (VkFramebuffer*)malloc(vkrt->swapChainImageCount * sizeof(VkFramebuffer));

    for (size_t i = 0; i < vkrt->swapChainImageCount; i++) {
        VkFramebufferCreateInfo framebufferCreateInfo = {0};
        framebufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferCreateInfo.renderPass = vkrt->renderPass;
        framebufferCreateInfo.attachmentCount = 1;
        framebufferCreateInfo.pAttachments = &vkrt->swapChainImageViews[i];
        framebufferCreateInfo.width = vkrt->swapChainExtent.width;
        framebufferCreateInfo.height = vkrt->swapChainExtent.height;
        framebufferCreateInfo.layers = 1;

        if (vkCreateFramebuffer(vkrt->device, &framebufferCreateInfo, NULL, &vkrt->framebuffers[i]) != VK_SUCCESS) {
            perror("ERROR: Failed to create framebuffer");
            exit(EXIT_FAILURE);
        }
    }
}

SwapChainSupportDetails querySwapChainSupport(VKRT* vkrt) {
    SwapChainSupportDetails supportDetails;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vkrt->physicalDevice, vkrt->surface, &supportDetails.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(vkrt->physicalDevice, vkrt->surface, &formatCount, NULL);

    if (formatCount) {
        supportDetails.formats = (VkSurfaceFormatKHR*)malloc(formatCount * sizeof(VkSurfaceFormatKHR));
        vkGetPhysicalDeviceSurfaceFormatsKHR(vkrt->physicalDevice, vkrt->surface, &formatCount, supportDetails.formats);
        supportDetails.formatCount = formatCount;
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(vkrt->physicalDevice, vkrt->surface, &presentModeCount, NULL);

    if (presentModeCount) {
        supportDetails.presentModes = (VkPresentModeKHR*)malloc(presentModeCount * sizeof(VkPresentModeKHR));
        vkGetPhysicalDeviceSurfacePresentModesKHR(vkrt->physicalDevice, vkrt->surface, &presentModeCount, supportDetails.presentModes);
        supportDetails.presentModeCount = presentModeCount;
    }

    return supportDetails;
}

VkSurfaceFormatKHR chooseSwapSurfaceFormat(SwapChainSupportDetails* supportDetails) {
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
        glfwGetFramebufferSize(vkrt->window, &width, &height);

        VkExtent2D actualExtent = {
            (uint32_t)width,
            (uint32_t)height};

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
