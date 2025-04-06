#include "swapchain.h"
#include <stdlib.h>

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