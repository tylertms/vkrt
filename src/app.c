#include "app.h"
#include "buffer.h"
#include "command.h"
#include "descriptor.h"
#include "device.h"
#include "instance.h"
#include "pipeline.h"
#include "surface.h"
#include "swapchain.h"
#include "validation.h"

#include <stdlib.h>

static void framebufferResizedCallback(GLFWwindow* window, int width, int height) {
    VKRT* vkrt = (VKRT*)glfwGetWindowUserPointer(window);
    vkrt->framebufferResized = VK_TRUE;
}

void initWindow(VKRT* vkrt) {
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    vkrt->window = glfwCreateWindow(WIDTH, HEIGHT, "VKRT", 0, 0);
    glfwSetWindowUserPointer(vkrt->window, vkrt);
    glfwSetFramebufferSizeCallback(vkrt->window, framebufferResizedCallback);
}

void initVulkan(VKRT* vkrt) {
    createInstance(vkrt);
    setupDebugMessenger(vkrt);
    createSurface(vkrt);
    pickPhysicalDevice(vkrt);
    createLogicalDevice(vkrt);
    createSwapChain(vkrt);
    createImageViews(vkrt);
    createDescriptorSetLayout(vkrt);
    createRayTracingPipeline(vkrt);
    createCommandPool(vkrt);
    createStorageImage(vkrt);
    createUniformBuffer(vkrt);
    createDescriptorPool(vkrt);
    createDescriptorSet(vkrt);
    createCommandBuffers(vkrt);
    createSyncObjects(vkrt);
}

void deinit(VKRT* vkrt) {
    cleanupSwapChain(vkrt);

    vkDestroyBuffer(vkrt->device, vkrt->uniformBuffer, NULL);
    vkFreeMemory(vkrt->device, vkrt->uniformBufferMemory, NULL);

    vkDestroyDescriptorPool(vkrt->device, vkrt->descriptorPool, NULL);

    vkDestroyDescriptorSetLayout(vkrt->device, vkrt->descriptorSetLayout, NULL);

    vkDestroyPipeline(vkrt->device, vkrt->rayTracingPipeline, NULL);
    vkDestroyPipelineLayout(vkrt->device, vkrt->pipelineLayout, NULL);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(vkrt->device, vkrt->imageAvailableSemaphores[i], NULL);
        vkDestroySemaphore(vkrt->device, vkrt->renderFinishedSemaphores[i], NULL);
        vkDestroyFence(vkrt->device, vkrt->inFlightFences[i], NULL);
    }

    vkDestroyCommandPool(vkrt->device, vkrt->commandPool, NULL);

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