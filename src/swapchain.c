#include "swapchain.h"
#include "device.h"
#include <stdlib.h>
#include <stdio.h>

void createSwapChain(VKRT* vkrt) {
    SwapChainSupportDetails supportDetails = querySwapChainSupport(vkrt);

    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(&supportDetails);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(&supportDetails);
    VkExtent2D extent = chooseSwapExtent(vkrt, &supportDetails);

    free(supportDetails.formats);
    free(supportDetails.presentModes);

    uint32_t imageCount = supportDetails.capabilities.minImageCount + 1;
    if (supportDetails.capabilities.maxImageCount && imageCount > supportDetails.capabilities.maxImageCount) {
        imageCount = supportDetails.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo = {0};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = vkrt->surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    QueueFamily indices = findQueueFamilies(vkrt);
    uint32_t queueFamilyIndices[] = {indices.graphics, indices.present};

    if (indices.graphics != indices.present) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0;
        createInfo.pQueueFamilyIndices = NULL;
    }

    createInfo.preTransform = supportDetails.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(vkrt->device, &createInfo, NULL, &vkrt->swapChain) != VK_SUCCESS) {
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

void createImageViews(VKRT* vkrt) {
    vkrt->swapChainImageViews = (VkImageView*)malloc(vkrt->swapChainImageCount * sizeof(VkImageView));
    for (size_t i = 0; i < vkrt->swapChainImageCount; i++) {
        VkImageViewCreateInfo createInfo = {0};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = vkrt->swapChainImages[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = vkrt->swapChainImageFormat;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(vkrt->device, &createInfo, NULL, &vkrt->swapChainImageViews[i]) != VK_SUCCESS) {
            perror("ERROR: Failed to create image views");
            exit(EXIT_FAILURE);
        }
    }
}

SwapChainSupportDetails querySwapChainSupport(VKRT* vkrt) {
    SwapChainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vkrt->physicalDevice, vkrt->surface, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(vkrt->physicalDevice, vkrt->surface, &formatCount, NULL);

    if (formatCount) {
        details.formats = (VkSurfaceFormatKHR*)malloc(formatCount * sizeof(VkSurfaceFormatKHR));
        vkGetPhysicalDeviceSurfaceFormatsKHR(vkrt->physicalDevice, vkrt->surface, &formatCount, details.formats);
        details.formatCount = formatCount;
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(vkrt->physicalDevice, vkrt->surface, &presentModeCount, NULL);

    if (presentModeCount) {
        details.presentModes = (VkPresentModeKHR*)malloc(presentModeCount * sizeof(VkPresentModeKHR));
        vkGetPhysicalDeviceSurfacePresentModesKHR(vkrt->physicalDevice, vkrt->surface, &presentModeCount, details.presentModes);
        details.presentModeCount = presentModeCount;
    }

    return details;
}

VkSurfaceFormatKHR chooseSwapSurfaceFormat(SwapChainSupportDetails* supportDetails) {
    for (uint32_t i = 0; i < supportDetails->formatCount; i++) {
        VkSurfaceFormatKHR format = supportDetails->formats[i];
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }

    return supportDetails->formats[0];
}

VkPresentModeKHR chooseSwapPresentMode(SwapChainSupportDetails* supportDetails) {
    for (uint32_t i = 0; i < supportDetails->presentModeCount; i++) {
        if (supportDetails->presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
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

void createFramebuffers(VKRT* vkrt) {
    vkrt->swapChainFramebuffers = (VkFramebuffer*)malloc(vkrt->swapChainImageCount * sizeof(VkFramebuffer));

    for (size_t i = 0; i < vkrt->swapChainImageCount; i++) {
        VkImageView attachments[] = {
            vkrt->swapChainImageViews[i]
        };

        VkFramebufferCreateInfo framebufferInfo = {0};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = vkrt->renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = vkrt->swapChainExtent.width;
        framebufferInfo.height = vkrt->swapChainExtent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(vkrt->device, &framebufferInfo, NULL, &vkrt->swapChainFramebuffers[i]) != VK_SUCCESS) {
            perror("ERROR: Failed to create framebuffer");
            exit(EXIT_FAILURE);
        }
    }
}