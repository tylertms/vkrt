#include "app.h"
#include "buffer.h"
#include "command.h"
#include "descriptor.h"
#include "device.h"
#include "instance.h"
#include "interface.h"
#include "object.h"
#include "pipeline.h"
#include "structure.h"
#include "surface.h"
#include "swapchain.h"
#include "validation.h"

#include <stdlib.h>

static void framebufferResizedCallback(GLFWwindow* window, int width, int height) {
    (void)width;
    (void)height;

    VKRT* vkrt = (VKRT*)glfwGetWindowUserPointer(window);
    vkrt->framebufferResized = VK_TRUE;
}

void initWindow(VKRT* vkrt) {
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    vkrt->window = glfwCreateWindow(WIDTH, HEIGHT, "VKRT", 0, 0);
    glfwSetWindowUserPointer(vkrt->window, vkrt);
    glfwSetFramebufferSizeCallback(vkrt->window, framebufferResizedCallback);

    vkrt->vsync = 1;
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
    createFramebuffers(vkrt);
    createCommandPool(vkrt);
    loadObject(vkrt, "assets/dragon.glb");
    createBottomLevelAccelerationStructure(vkrt);
    createTopLevelAccelerationStructure(vkrt);
    createDescriptorSetLayout(vkrt);
    createRayTracingPipeline(vkrt);
    createStorageImage(vkrt);
    createUniformBuffer(vkrt);
    createDescriptorPool(vkrt);
    createDescriptorSet(vkrt);
    createShaderBindingTable(vkrt);
    createCommandBuffers(vkrt);
    createSyncObjects(vkrt);
    initializeFrameTimers(vkrt);
    setupImGui(vkrt);
    setupSceneUniform(vkrt);
}

void deinit(VKRT* vkrt) {
    deinitImGui(vkrt);

    cleanupSwapChain(vkrt);

    vkDestroyRenderPass(vkrt->device, vkrt->renderPass, NULL);

    vkDestroyBuffer(vkrt->device, vkrt->shaderBindingTableBuffer, NULL);
    vkFreeMemory(vkrt->device, vkrt->shaderBindingTableMemory, NULL);

    PFN_vkDestroyAccelerationStructureKHR vkDestroyAS = (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(vkrt->device, "vkDestroyAccelerationStructureKHR");
    vkDestroyAS(vkrt->device, vkrt->bottomLevelAccelerationStructure, NULL);
    vkDestroyBuffer(vkrt->device, vkrt->bottomLevelAccelerationStructureBuffer, NULL);
    vkFreeMemory(vkrt->device, vkrt->bottomLevelAccelerationStructureMemory, NULL);

    vkDestroyAS(vkrt->device, vkrt->topLevelAccelerationStructure, NULL);
    vkDestroyBuffer(vkrt->device, vkrt->topLevelAccelerationStructureBuffer, NULL);
    vkFreeMemory(vkrt->device, vkrt->topLevelAccelerationStructureMemory, NULL);

    vkDestroyBuffer(vkrt->device, vkrt->vertexBuffer, NULL);
    vkFreeMemory(vkrt->device, vkrt->vertexBufferMemory, NULL);
    vkDestroyBuffer(vkrt->device, vkrt->indexBuffer, NULL);
    vkFreeMemory(vkrt->device, vkrt->indexBufferMemory, NULL);

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

    vkFreeCommandBuffers(vkrt->device, vkrt->commandPool, COUNT_OF(vkrt->commandBuffers), vkrt->commandBuffers);

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