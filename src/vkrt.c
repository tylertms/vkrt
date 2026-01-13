#include <stdlib.h>
#include <string.h>

#include "command.h"
#include "descriptor.h"
#include "device.h"
#include "instance.h"
#include "object.h"
#include "scene.h"
#include "structure.h"
#include "surface.h"
#include "swapchain.h"
#include "pipeline.h"
#include "validation.h"
#include "vkrt.h"

int VKRT_init(VKRT *vkrt) {
    if (!vkrt) return -1;

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    vkrt->vsync = 1;
    vkrt->window = glfwCreateWindow(WIDTH, HEIGHT, "VKRT", 0, 0);
    glfwSetWindowUserPointer(vkrt->window, vkrt);
    glfwSetFramebufferSizeCallback(vkrt->window, VKRT_framebufferResizedCallback);

    createInstance(vkrt);
    setupDebugMessenger(vkrt);
    createSurface(vkrt);
    pickPhysicalDevice(vkrt);
    createLogicalDevice(vkrt);
    createQueryPool(vkrt);
    createSwapChain(vkrt);
    createImageViews(vkrt);
    createRenderPass(vkrt);
    createFramebuffers(vkrt);
    createCommandPool(vkrt);
    createDescriptorSetLayout(vkrt);
    createRayTracingPipeline(vkrt);
    createStorageImage(vkrt);
    createSceneUniform(vkrt);
    createDescriptorPool(vkrt);
    createDescriptorSet(vkrt);
    createShaderBindingTable(vkrt);
    createCommandBuffers(vkrt);
    createSyncObjects(vkrt);
    
    if (vkrt->gui.init) {
        vkrt->gui.init(vkrt);
    }

    return 0;
}

void VKRT_registerGUI(VKRT* vkrt, void (*init)(VKRT*), void (*deinit)(VKRT*), void (*draw)(VKRT*)) {
    if (!vkrt) return;
    vkrt->gui.init = init;
    vkrt->gui.deinit = deinit;
    vkrt->gui.draw = draw;
}

void VKRT_deinit(VKRT *vkrt) {
    if (!vkrt) return;

    vkDeviceWaitIdle(vkrt->device);

    if (vkrt->gui.deinit) {
        vkrt->gui.deinit(vkrt);
    }

    cleanupSwapChain(vkrt);

    vkDestroyRenderPass(vkrt->device, vkrt->renderPass, NULL);

    vkDestroyBuffer(vkrt->device, vkrt->shaderBindingTableBuffer, NULL);
    vkFreeMemory(vkrt->device, vkrt->shaderBindingTableMemory, NULL);

    PFN_vkDestroyAccelerationStructureKHR pvkDestroyAccelerationStructureKHR = (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(vkrt->device, "vkDestroyAccelerationStructureKHR");
    pvkDestroyAccelerationStructureKHR(vkrt->device, vkrt->topLevelAccelerationStructure.structure, NULL);
    vkDestroyBuffer(vkrt->device, vkrt->topLevelAccelerationStructure.buffer, NULL);
    vkFreeMemory(vkrt->device, vkrt->topLevelAccelerationStructure.memory, NULL);

    for (uint32_t i = 0; i < vkrt->meshData.count; i++) {
        pvkDestroyAccelerationStructureKHR(vkrt->device, vkrt->meshes[i].bottomLevelAccelerationStructure.structure, NULL);
        vkDestroyBuffer(vkrt->device, vkrt->meshes[i].bottomLevelAccelerationStructure.buffer, NULL);
        vkFreeMemory(vkrt->device, vkrt->meshes[i].bottomLevelAccelerationStructure.memory, NULL);
    }

    vkDestroyBuffer(vkrt->device, vkrt->vertexData.buffer, NULL);
    vkFreeMemory(vkrt->device, vkrt->vertexData.memory, NULL);
    vkDestroyBuffer(vkrt->device, vkrt->indexData.buffer, NULL);
    vkFreeMemory(vkrt->device, vkrt->indexData.memory, NULL);
    vkDestroyBuffer(vkrt->device, vkrt->meshData.buffer, NULL);
    vkFreeMemory(vkrt->device, vkrt->meshData.memory, NULL);

    vkDestroyBuffer(vkrt->device, vkrt->sceneDataBuffer, NULL);
    vkFreeMemory(vkrt->device, vkrt->sceneDataMemory, NULL);

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

    vkDestroyQueryPool(vkrt->device, vkrt->timestampPool, NULL);

    vkDestroyDevice(vkrt->device, NULL);

    if (enableValidationLayers) {
        DestroyDebugUtilsMessengerEXT(vkrt->instance, vkrt->debugMessenger, NULL);
    }

    vkDestroySurfaceKHR(vkrt->instance, vkrt->surface, NULL);
    vkDestroyInstance(vkrt->instance, NULL);

    glfwDestroyWindow(vkrt->window);
    glfwTerminate();
}

int VKRT_shouldDeinit(VKRT* vkrt) {
    return vkrt ? glfwWindowShouldClose(vkrt->window) : 1;
}

void VKRT_poll(VKRT* vkrt) {
    if (!vkrt) return;
    glfwPollEvents();
}

void VKRT_draw(VKRT* vkrt) {
    if (vkrt) drawFrame(vkrt);
}

void VKRT_addMesh(VKRT* vkrt, const char* path) {
    if (!vkrt || !path) return;
    loadObject(vkrt, path);
}

void VKRT_updateTLAS(VKRT* vkrt) {
    if (!vkrt) return;
    createTopLevelAccelerationStructure(vkrt);
}

void VKRT_pollCameraMovement(VKRT* vkrt) {
    if (!vkrt) return;
    pollCameraMovement(vkrt);
}

void VKRT_setDefaultStyle() {
    setDefaultStyle();
}

void VKRT_getImGuiVulkanInitInfo(VKRT* vkrt, ImGui_ImplVulkan_InitInfo* info) {
    if (!info) return;

    info->Instance = vkrt->instance;
    info->PhysicalDevice = vkrt->physicalDevice;
    info->Device = vkrt->device;
    info->Queue = vkrt->graphicsQueue;
    info->QueueFamily = vkrt->indices.graphics;
    info->PipelineCache = VK_NULL_HANDLE;
    info->DescriptorPool = vkrt->descriptorPool;
    info->Allocator = VK_NULL_HANDLE;
    uint32_t imgCount = (uint32_t)vkrt->swapChainImageCount;
    uint32_t minImgCount = (imgCount > 1u) ? (imgCount - 1u) : imgCount;
    info->MinImageCount = minImgCount;
    info->ImageCount = imgCount;
    info->CheckVkResultFn = VK_NULL_HANDLE;
    info->RenderPass = vkrt->renderPass;
}

static void VKRT_framebufferResizedCallback(GLFWwindow* window, int width, int height) {
    (void)width;
    (void)height;

    VKRT* vkrt = (VKRT*)glfwGetWindowUserPointer(window);
    vkrt->framebufferResized = VK_TRUE;
}
