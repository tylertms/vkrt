#include "buffer.h"
#include "command.h"
#include "descriptor.h"
#include "device.h"
#include "procs.h"
#include "instance.h"
#include "pipeline.h"
#include "scene.h"
#include "accel.h"
#include "surface.h"
#include "swapchain.h"
#include "validation.h"
#include "debug.h"
#include "vkrt.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static inline void logStepTime(const char* stepName, uint64_t startTime) {
    LOG_TRACE("%s in %.3f ms", stepName, (double)(getMicroseconds() - startTime) / 1e3);
}

void VKRT_defaultCreateInfo(VKRT_CreateInfo* createInfo) {
    if (!createInfo) return;

    *createInfo = (VKRT_CreateInfo){
        .width = WIDTH,
        .height = HEIGHT,
        .title = "VKRT",
        .vsync = 1,
        .shaders = {
            .rgenPath = "./shaders/rgen.spv",
            .rmissPath = "./shaders/rmiss.spv",
            .rchitPath = "./shaders/rchit.spv",
        },
    };
}

int VKRT_initWithCreateInfo(VKRT* vkrt, const VKRT_CreateInfo* createInfo) {
    if (!vkrt || !createInfo) return -1;

    uint64_t initStartTime = getMicroseconds();
    uint64_t stepStartTime = initStartTime;

    if (!glfwInit()) {
        LOG_ERROR("Failed to initialize GLFW");
        return -1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    logStepTime("GLFW setup complete", stepStartTime);

    vkrt->runtime.vsync = createInfo->vsync;
    vkrt->core.shaders = createInfo->shaders;
    vkrt->core.descriptorSetReady = VK_FALSE;

    const char* title = createInfo->title ? createInfo->title : "VKRT";
    uint32_t width = createInfo->width ? createInfo->width : WIDTH;
    uint32_t height = createInfo->height ? createInfo->height : HEIGHT;

    if (!vkrt->core.shaders.rgenPath) vkrt->core.shaders.rgenPath = "./shaders/rgen.spv";
    if (!vkrt->core.shaders.rmissPath) vkrt->core.shaders.rmissPath = "./shaders/rmiss.spv";
    if (!vkrt->core.shaders.rchitPath) vkrt->core.shaders.rchitPath = "./shaders/rchit.spv";

    stepStartTime = getMicroseconds();
    vkrt->runtime.window = glfwCreateWindow((int)width, (int)height, title, 0, 0);
    if (!vkrt->runtime.window) {
        LOG_ERROR("Failed to create GLFW window");
        glfwTerminate();
        return -1;
    }

    glfwSetWindowUserPointer(vkrt->runtime.window, vkrt);
    glfwSetFramebufferSizeCallback(vkrt->runtime.window, VKRT_framebufferResizedCallback);
    logStepTime("Window setup complete", stepStartTime);

    stepStartTime = getMicroseconds();
    createInstance(vkrt);
    logStepTime("Vulkan instance created", stepStartTime);

    stepStartTime = getMicroseconds();
    setupDebugMessenger(vkrt);
    logStepTime("Debug messenger setup complete", stepStartTime);

    stepStartTime = getMicroseconds();
    createSurface(vkrt);
    logStepTime("Surface created", stepStartTime);

    stepStartTime = getMicroseconds();
    pickPhysicalDevice(vkrt);
    logStepTime("Physical device selection complete", stepStartTime);

    stepStartTime = getMicroseconds();
    createLogicalDevice(vkrt);
    logStepTime("Logical device created", stepStartTime);

    stepStartTime = getMicroseconds();
    loadDeviceProcs(vkrt);
    logStepTime("Device procedures loaded", stepStartTime);

    stepStartTime = getMicroseconds();
    createQueryPool(vkrt);
    logStepTime("Query pool created", stepStartTime);

    stepStartTime = getMicroseconds();
    createSwapChain(vkrt);
    createImageViews(vkrt);
    createRenderPass(vkrt);
    createFramebuffers(vkrt);
    logStepTime("Swapchain and framebuffers ready", stepStartTime);

    stepStartTime = getMicroseconds();
    createCommandPool(vkrt);
    createDescriptorSetLayout(vkrt);
    logStepTime("Command pool and descriptor layout ready", stepStartTime);

    stepStartTime = getMicroseconds();
    createRayTracingPipeline(vkrt);
    logStepTime("Ray tracing pipeline ready", stepStartTime);

    stepStartTime = getMicroseconds();
    createStorageImage(vkrt);
    logStepTime("Storage image ready", stepStartTime);

    stepStartTime = getMicroseconds();
    createSceneUniform(vkrt);
    logStepTime("Scene uniform ready", stepStartTime);

    stepStartTime = getMicroseconds();
    createDescriptorPool(vkrt);
    logStepTime("Descriptor pool ready", stepStartTime);

    stepStartTime = getMicroseconds();
    createDescriptorSet(vkrt);
    logStepTime("Descriptor set ready", stepStartTime);

    stepStartTime = getMicroseconds();
    createShaderBindingTable(vkrt);
    logStepTime("Shader binding table ready", stepStartTime);

    stepStartTime = getMicroseconds();
    createCommandBuffers(vkrt);
    createSyncObjects(vkrt);
    logStepTime("Command buffers and sync objects ready", stepStartTime);

    stepStartTime = getMicroseconds();
    if (vkrt->appHooks.init) {
        vkrt->appHooks.init(vkrt, vkrt->appHooks.userData);
    }
    logStepTime("Application initialization complete", stepStartTime);

    LOG_INFO("VKRT initialization complete in %.3f ms", (double)(getMicroseconds() - initStartTime) / 1e3);
    return 0;
}

int VKRT_init(VKRT* vkrt) {
    VKRT_CreateInfo createInfo = {0};
    VKRT_defaultCreateInfo(&createInfo);
    return VKRT_initWithCreateInfo(vkrt, &createInfo);
}

void VKRT_registerAppHooks(VKRT* vkrt, VKRT_AppHooks hooks) {
    if (!vkrt) return;
    vkrt->appHooks = hooks;
}

void VKRT_deinit(VKRT* vkrt) {
    if (!vkrt) return;

    uint64_t deinitStartTime = getMicroseconds();
    uint64_t stepStartTime = deinitStartTime;

    if (vkrt->core.device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(vkrt->core.device);
        logStepTime("Device idle wait complete", stepStartTime);

        stepStartTime = getMicroseconds();
        if (vkrt->appHooks.deinit) {
            vkrt->appHooks.deinit(vkrt, vkrt->appHooks.userData);
        }
        logStepTime("Application shutdown complete", stepStartTime);

        stepStartTime = getMicroseconds();
        if (vkrt->runtime.swapChain != VK_NULL_HANDLE) {
            cleanupSwapChain(vkrt);
        }
        logStepTime("Swapchain cleanup complete", stepStartTime);

        stepStartTime = getMicroseconds();
        if (vkrt->runtime.renderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(vkrt->core.device, vkrt->runtime.renderPass, NULL);
        }

        if (vkrt->core.shaderBindingTableBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(vkrt->core.device, vkrt->core.shaderBindingTableBuffer, NULL);
        }
        if (vkrt->core.shaderBindingTableMemory != VK_NULL_HANDLE) {
            vkFreeMemory(vkrt->core.device, vkrt->core.shaderBindingTableMemory, NULL);
        }
        logStepTime("Render pass and shader binding table cleanup complete", stepStartTime);

        stepStartTime = getMicroseconds();
        if (vkrt->core.procs.vkDestroyAccelerationStructureKHR) {
            if (vkrt->core.topLevelAccelerationStructure.structure != VK_NULL_HANDLE) {
                vkrt->core.procs.vkDestroyAccelerationStructureKHR(vkrt->core.device, vkrt->core.topLevelAccelerationStructure.structure, NULL);
            }
            if (vkrt->core.topLevelAccelerationStructure.buffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(vkrt->core.device, vkrt->core.topLevelAccelerationStructure.buffer, NULL);
            }
            if (vkrt->core.topLevelAccelerationStructure.memory != VK_NULL_HANDLE) {
                vkFreeMemory(vkrt->core.device, vkrt->core.topLevelAccelerationStructure.memory, NULL);
            }
        }

        for (uint32_t i = 0; i < vkrt->core.meshData.count; i++) {
            if (vkrt->core.meshes[i].ownsGeometry && vkrt->core.procs.vkDestroyAccelerationStructureKHR &&
                vkrt->core.meshes[i].bottomLevelAccelerationStructure.structure != VK_NULL_HANDLE) {
                vkrt->core.procs.vkDestroyAccelerationStructureKHR(vkrt->core.device, vkrt->core.meshes[i].bottomLevelAccelerationStructure.structure, NULL);
            }
            if (vkrt->core.meshes[i].ownsGeometry && vkrt->core.meshes[i].bottomLevelAccelerationStructure.buffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(vkrt->core.device, vkrt->core.meshes[i].bottomLevelAccelerationStructure.buffer, NULL);
            }
            if (vkrt->core.meshes[i].ownsGeometry && vkrt->core.meshes[i].bottomLevelAccelerationStructure.memory != VK_NULL_HANDLE) {
                vkFreeMemory(vkrt->core.device, vkrt->core.meshes[i].bottomLevelAccelerationStructure.memory, NULL);
            }
            if (vkrt->core.meshes[i].ownsGeometry) {
                free(vkrt->core.meshes[i].vertices);
                free(vkrt->core.meshes[i].indices);
            }
        }
        free(vkrt->core.meshes);
        vkrt->core.meshes = NULL;
        logStepTime("Acceleration structures and mesh sources cleaned", stepStartTime);

        stepStartTime = getMicroseconds();
        if (vkrt->core.vertexData.buffer != VK_NULL_HANDLE) vkDestroyBuffer(vkrt->core.device, vkrt->core.vertexData.buffer, NULL);
        if (vkrt->core.vertexData.memory != VK_NULL_HANDLE) vkFreeMemory(vkrt->core.device, vkrt->core.vertexData.memory, NULL);
        if (vkrt->core.indexData.buffer != VK_NULL_HANDLE) vkDestroyBuffer(vkrt->core.device, vkrt->core.indexData.buffer, NULL);
        if (vkrt->core.indexData.memory != VK_NULL_HANDLE) vkFreeMemory(vkrt->core.device, vkrt->core.indexData.memory, NULL);
        if (vkrt->core.meshData.buffer != VK_NULL_HANDLE) vkDestroyBuffer(vkrt->core.device, vkrt->core.meshData.buffer, NULL);
        if (vkrt->core.meshData.memory != VK_NULL_HANDLE) vkFreeMemory(vkrt->core.device, vkrt->core.meshData.memory, NULL);
        if (vkrt->core.materialData.buffer != VK_NULL_HANDLE) vkDestroyBuffer(vkrt->core.device, vkrt->core.materialData.buffer, NULL);
        if (vkrt->core.materialData.memory != VK_NULL_HANDLE) vkFreeMemory(vkrt->core.device, vkrt->core.materialData.memory, NULL);
        if (vkrt->core.sceneDataBuffer != VK_NULL_HANDLE) vkDestroyBuffer(vkrt->core.device, vkrt->core.sceneDataBuffer, NULL);
        if (vkrt->core.sceneDataMemory != VK_NULL_HANDLE) vkFreeMemory(vkrt->core.device, vkrt->core.sceneDataMemory, NULL);
        logStepTime("Scene and mesh buffer cleanup complete", stepStartTime);

        stepStartTime = getMicroseconds();
        if (vkrt->core.descriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(vkrt->core.device, vkrt->core.descriptorPool, NULL);
        if (vkrt->core.descriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(vkrt->core.device, vkrt->core.descriptorSetLayout, NULL);
        if (vkrt->core.rayTracingPipeline != VK_NULL_HANDLE) vkDestroyPipeline(vkrt->core.device, vkrt->core.rayTracingPipeline, NULL);
        if (vkrt->core.pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(vkrt->core.device, vkrt->core.pipelineLayout, NULL);
        logStepTime("Descriptor and pipeline cleanup complete", stepStartTime);

        stepStartTime = getMicroseconds();
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            if (vkrt->runtime.imageAvailableSemaphores[i] != VK_NULL_HANDLE) {
                vkDestroySemaphore(vkrt->core.device, vkrt->runtime.imageAvailableSemaphores[i], NULL);
            }
            if (vkrt->runtime.inFlightFences[i] != VK_NULL_HANDLE) {
                vkDestroyFence(vkrt->core.device, vkrt->runtime.inFlightFences[i], NULL);
            }
        }

        if (vkrt->runtime.renderFinishedSemaphores) {
            for (size_t i = 0; i < vkrt->runtime.swapChainImageCount; i++) {
                if (vkrt->runtime.renderFinishedSemaphores[i] != VK_NULL_HANDLE) {
                    vkDestroySemaphore(vkrt->core.device, vkrt->runtime.renderFinishedSemaphores[i], NULL);
                }
            }
            free(vkrt->runtime.renderFinishedSemaphores);
            vkrt->runtime.renderFinishedSemaphores = NULL;
        }
        logStepTime("Synchronization object cleanup complete", stepStartTime);

        stepStartTime = getMicroseconds();
        if (vkrt->runtime.commandPool != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(vkrt->core.device, vkrt->runtime.commandPool, COUNT_OF(vkrt->runtime.commandBuffers), vkrt->runtime.commandBuffers);
            vkDestroyCommandPool(vkrt->core.device, vkrt->runtime.commandPool, NULL);
        }
        if (vkrt->runtime.timestampPool != VK_NULL_HANDLE) {
            vkDestroyQueryPool(vkrt->core.device, vkrt->runtime.timestampPool, NULL);
        }
        logStepTime("Command and query resource cleanup complete", stepStartTime);

        stepStartTime = getMicroseconds();
        vkDestroyDevice(vkrt->core.device, NULL);
        logStepTime("Vulkan device shutdown complete", stepStartTime);
    } else {
        for (uint32_t i = 0; i < vkrt->core.meshData.count; i++) {
            if (vkrt->core.meshes[i].ownsGeometry) {
                free(vkrt->core.meshes[i].vertices);
                free(vkrt->core.meshes[i].indices);
            }
        }
        free(vkrt->core.meshes);
        vkrt->core.meshes = NULL;
        free(vkrt->runtime.renderFinishedSemaphores);
        vkrt->runtime.renderFinishedSemaphores = NULL;
    }

    stepStartTime = getMicroseconds();
    if (vkrt->core.instance != VK_NULL_HANDLE) {
        if (enableValidationLayers && vkrt->core.debugMessenger != VK_NULL_HANDLE) {
            destroyDebugUtilsMessengerEXT(vkrt->core.instance, vkrt->core.debugMessenger, NULL);
        }
        if (vkrt->runtime.surface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(vkrt->core.instance, vkrt->runtime.surface, NULL);
        }
        vkDestroyInstance(vkrt->core.instance, NULL);
    }
    logStepTime("Vulkan instance shutdown complete", stepStartTime);

    stepStartTime = getMicroseconds();
    if (vkrt->runtime.window) {
        glfwDestroyWindow(vkrt->runtime.window);
        vkrt->runtime.window = NULL;
    }
    glfwTerminate();
    logStepTime("GLFW shutdown complete", stepStartTime);

    LOG_INFO("VKRT deinitialization complete in %.3f ms", (double)(getMicroseconds() - deinitStartTime) / 1e3);
}

int VKRT_shouldDeinit(VKRT* vkrt) {
    return (vkrt && vkrt->runtime.window) ? glfwWindowShouldClose(vkrt->runtime.window) : 1;
}

void VKRT_poll(VKRT* vkrt) {
    if (!vkrt) return;
    glfwPollEvents();
}

void VKRT_framebufferResizedCallback(GLFWwindow* window, int width, int height) {
    (void)width;
    (void)height;

    VKRT* vkrt = (VKRT*)glfwGetWindowUserPointer(window);
    vkrt->runtime.framebufferResized = VK_TRUE;
}
