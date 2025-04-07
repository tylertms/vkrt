#include "app.h"
#include "command.h"
#include "device.h"
#include "instance.h"
#include "pipeline.h"
#include "surface.h"
#include "swapchain.h"
#include "validation.h"

#include <stdlib.h>

void initWindow(VKRT* vkrt) {
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    vkrt->window = glfwCreateWindow(WIDTH, HEIGHT, "VKRT", 0, 0);
}

void initVulkan(VKRT* vkrt) {
    createInstance(vkrt);
    setupDebugMessenger(vkrt);
    createSurface(vkrt);
    pickPhysicalDevice(vkrt);
    createLogicalDevice(vkrt);
    createSwapChain(vkrt);
    createImageViews(vkrt);
    createRenderPass(vkrt);
    createRayTracingPipeline(vkrt);
    createFramebuffers(vkrt);
    createCommandPool(vkrt);
    createCommandBuffer(vkrt);
    createSyncObjects(vkrt);
}

void deinit(VKRT* vkrt) {
    vkDestroySemaphore(vkrt->device, vkrt->imageAvailableSemaphore, NULL);
    vkDestroySemaphore(vkrt->device, vkrt->renderFinishedSemaphore, NULL);
    vkDestroyFence(vkrt->device, vkrt->inFlightFence, NULL);

    vkDestroyCommandPool(vkrt->device, vkrt->commandPool, NULL);
    
    for (size_t i = 0; i < vkrt->swapChainImageCount; i++) {
        vkDestroyFramebuffer(vkrt->device, vkrt->swapChainFramebuffers[i], NULL);
    }

    vkDestroyPipeline(vkrt->device, vkrt->rayTracingPipeline, NULL);
    vkDestroyPipelineLayout(vkrt->device, vkrt->pipelineLayout, NULL);
    vkDestroyRenderPass(vkrt->device, vkrt->renderPass, NULL);

    for (size_t i = 0; i < vkrt->swapChainImageCount; i++) {
        vkDestroyImageView(vkrt->device, vkrt->swapChainImageViews[i], NULL);
    }

    free(vkrt->swapChainFramebuffers);
    free(vkrt->swapChainImageViews);
    free(vkrt->swapChainImages);

    vkDestroySwapchainKHR(vkrt->device, vkrt->swapChain, NULL);

    vkDestroyDevice(vkrt->device, NULL);

    if (enableValidationLayers) {
        DestroyDebugUtilsMessengerEXT(vkrt->instance, vkrt->debugMessenger, NULL);
    }

    vkDestroySurfaceKHR(vkrt->instance, vkrt->surface, NULL);
    vkDestroyInstance(vkrt->instance, NULL);

    glfwDestroyWindow(vkrt->window);

    glfwTerminate();
}

void run(VKRT* vkrt) {
    initWindow(vkrt);
    initVulkan(vkrt);

    while (!glfwWindowShouldClose(vkrt->window)) {
        glfwPollEvents();
        drawFrame(vkrt);
    }

    vkDeviceWaitIdle(vkrt->device);

    deinit(vkrt);
}