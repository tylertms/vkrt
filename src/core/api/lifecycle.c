#include "buffer.h"
#include "command/pool.h"
#include "descriptor.h"
#include "device.h"
#include "procs.h"
#include "image/storage_image.h"
#include "instance.h"
#include "pipeline.h"
#include "scene.h"
#include "accel/accel.h"
#include "surface.h"
#include "swapchain.h"
#include "validation.h"
#include "export.h"
#include "debug.h"
#include "vkrt_internal.h"

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
        .width = VKRT_DEFAULT_WIDTH,
        .height = VKRT_DEFAULT_HEIGHT,
        .title = "VKRT",
        .vsync = 1,
        .shaders = {
            .rgenPath = "./shaders/rgen.spv",
            .rmissPath = "./shaders/rmiss.spv",
            .rchitPath = "./shaders/rchit.spv",
        },
    };
}

VKRT_Result VKRT_create(VKRT** outVkrt) {
    if (!outVkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    VKRT* vkrt = (VKRT*)calloc(1, sizeof(VKRT));
    if (!vkrt) return VKRT_ERROR_OPERATION_FAILED;

    *outVkrt = vkrt;
    return VKRT_SUCCESS;
}

void VKRT_destroy(VKRT* vkrt) {
    if (!vkrt) return;
    free(vkrt);
}

VKRT_Result VKRT_initWithCreateInfo(VKRT* vkrt, const VKRT_CreateInfo* createInfo) {
    if (!vkrt || !createInfo) return VKRT_ERROR_INVALID_ARGUMENT;
    if (vkrt->runtime.window || vkrt->core.instance || vkrt->core.device) {
        LOG_ERROR("VKRT_initWithCreateInfo called on an already initialized instance");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    uint64_t initStartTime = getMicroseconds();
    uint64_t stepStartTime = initStartTime;

    if (!glfwInit()) {
        LOG_ERROR("Failed to initialize GLFW");
        return VKRT_ERROR_OPERATION_FAILED;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    logStepTime("GLFW setup complete", stepStartTime);

    vkrt->runtime.vsync = createInfo->vsync;
    vkrt->runtime.autoSPPFastAdaptFrames = 0;
    vkrt->runtime.swapchainFormatLogInitialized = VK_FALSE;
    vkrt->runtime.lastLoggedSwapchainFormat = VK_FORMAT_UNDEFINED;
    vkrt->runtime.lastLoggedSwapchainColorSpace = VK_COLOR_SPACE_MAX_ENUM_KHR;
    vkrt->runtime.appInitialized = 0;
    vkrt->core.shaders = createInfo->shaders;
    vkrt->core.descriptorSetReady = VK_FALSE;
    vkrt->core.emissiveMeshCount = 0;
    vkrt->core.emissiveTriangleCount = 0;

    const char* title = createInfo->title ? createInfo->title : "VKRT";
    uint32_t width = createInfo->width ? createInfo->width : VKRT_DEFAULT_WIDTH;
    uint32_t height = createInfo->height ? createInfo->height : VKRT_DEFAULT_HEIGHT;

    if (!vkrt->core.shaders.rgenPath) vkrt->core.shaders.rgenPath = "./shaders/rgen.spv";
    if (!vkrt->core.shaders.rmissPath) vkrt->core.shaders.rmissPath = "./shaders/rmiss.spv";
    if (!vkrt->core.shaders.rchitPath) vkrt->core.shaders.rchitPath = "./shaders/rchit.spv";

    stepStartTime = getMicroseconds();
    vkrt->runtime.window = glfwCreateWindow((int)width, (int)height, title, 0, 0);
    if (!vkrt->runtime.window) {
        LOG_ERROR("Failed to create GLFW window");
        goto init_failed;
    }

    glfwSetWindowUserPointer(vkrt->runtime.window, vkrt);
    glfwSetFramebufferSizeCallback(vkrt->runtime.window, VKRT_framebufferResizedCallback);
    logStepTime("Window setup complete", stepStartTime);

    stepStartTime = getMicroseconds();
    if (createInstance(vkrt) != VKRT_SUCCESS) goto init_failed;
    logStepTime("Vulkan instance created", stepStartTime);

    stepStartTime = getMicroseconds();
    if (setupDebugMessenger(vkrt) != VKRT_SUCCESS) goto init_failed;
    logStepTime("Debug messenger setup complete", stepStartTime);

    stepStartTime = getMicroseconds();
    if (createSurface(vkrt) != VKRT_SUCCESS) goto init_failed;
    logStepTime("Surface created", stepStartTime);

    stepStartTime = getMicroseconds();
    if (pickPhysicalDevice(vkrt) != VKRT_SUCCESS) goto init_failed;
    logStepTime("Physical device selection complete", stepStartTime);

    stepStartTime = getMicroseconds();
    if (createLogicalDevice(vkrt) != VKRT_SUCCESS) goto init_failed;
    logStepTime("Logical device created", stepStartTime);

    stepStartTime = getMicroseconds();
    if (loadDeviceProcs(vkrt) != VKRT_SUCCESS) goto init_failed;
    logStepTime("Device procedures loaded", stepStartTime);

    stepStartTime = getMicroseconds();
    if (createQueryPool(vkrt) != VKRT_SUCCESS) goto init_failed;
    logStepTime("Query pool created", stepStartTime);

    stepStartTime = getMicroseconds();
    if (createSwapChain(vkrt) != VKRT_SUCCESS) goto init_failed;
    if (createImageViews(vkrt) != VKRT_SUCCESS) goto init_failed;
    if (createRenderPass(vkrt) != VKRT_SUCCESS) goto init_failed;
    if (createFramebuffers(vkrt) != VKRT_SUCCESS) goto init_failed;
    logStepTime("Swapchain and framebuffers ready", stepStartTime);

    stepStartTime = getMicroseconds();
    if (createCommandPool(vkrt) != VKRT_SUCCESS) goto init_failed;
    if (createDescriptorSetLayout(vkrt) != VKRT_SUCCESS) goto init_failed;
    logStepTime("Command pool and descriptor layout ready", stepStartTime);

    stepStartTime = getMicroseconds();
    if (createRayTracingPipeline(vkrt) != VKRT_SUCCESS) goto init_failed;
    logStepTime("Ray tracing pipeline ready", stepStartTime);

    stepStartTime = getMicroseconds();
    if (createStorageImage(vkrt) != VKRT_SUCCESS) goto init_failed;
    logStepTime("Storage image ready", stepStartTime);

    stepStartTime = getMicroseconds();
    if (createSceneUniform(vkrt) != VKRT_SUCCESS) goto init_failed;
    logStepTime("Scene uniform ready", stepStartTime);

    stepStartTime = getMicroseconds();
    if (createDescriptorPool(vkrt) != VKRT_SUCCESS) goto init_failed;
    logStepTime("Descriptor pool ready", stepStartTime);

    stepStartTime = getMicroseconds();
    if (createDescriptorSet(vkrt) != VKRT_SUCCESS) goto init_failed;
    logStepTime("Descriptor set ready", stepStartTime);

    stepStartTime = getMicroseconds();
    if (createShaderBindingTable(vkrt) != VKRT_SUCCESS) goto init_failed;
    logStepTime("Shader binding table ready", stepStartTime);

    stepStartTime = getMicroseconds();
    if (createCommandBuffers(vkrt) != VKRT_SUCCESS) goto init_failed;
    if (createSyncObjects(vkrt) != VKRT_SUCCESS) goto init_failed;
    logStepTime("Command buffers and sync objects ready", stepStartTime);

    stepStartTime = getMicroseconds();
    if (vkrt->appHooks.init) {
        vkrt->appHooks.init(vkrt, vkrt->appHooks.userData);
    }
    vkrt->runtime.appInitialized = 1;
    logStepTime("Application initialization complete", stepStartTime);

    LOG_INFO("VKRT initialization complete in %.3f ms", (double)(getMicroseconds() - initStartTime) / 1e3);
    return VKRT_SUCCESS;

init_failed:
    VKRT_deinit(vkrt);
    return VKRT_ERROR_OPERATION_FAILED;
}

VKRT_Result VKRT_init(VKRT* vkrt) {
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
        shutdownRenderPNGExporter();
        logStepTime("PNG export worker shutdown complete", stepStartTime);

        stepStartTime = getMicroseconds();
        if (vkrt->runtime.appInitialized && vkrt->appHooks.deinit) {
            vkrt->appHooks.deinit(vkrt, vkrt->appHooks.userData);
        }
        vkrt->runtime.appInitialized = 0;
        logStepTime("Application shutdown complete", stepStartTime);

        stepStartTime = getMicroseconds();
        if (vkrt->runtime.swapChain != VK_NULL_HANDLE) {
            cleanupSwapChain(vkrt);
        }
        destroyStorageImage(vkrt);
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
        if (vkrt->core.pickData && vkrt->core.pickBuffer.memory != VK_NULL_HANDLE) {
            vkUnmapMemory(vkrt->core.device, vkrt->core.pickBuffer.memory);
            vkrt->core.pickData = NULL;
        }
        if (vkrt->core.pickBuffer.buffer != VK_NULL_HANDLE) vkDestroyBuffer(vkrt->core.device, vkrt->core.pickBuffer.buffer, NULL);
        if (vkrt->core.pickBuffer.memory != VK_NULL_HANDLE) vkFreeMemory(vkrt->core.device, vkrt->core.pickBuffer.memory, NULL);
        if (vkrt->core.materialData.buffer != VK_NULL_HANDLE) vkDestroyBuffer(vkrt->core.device, vkrt->core.materialData.buffer, NULL);
        if (vkrt->core.materialData.memory != VK_NULL_HANDLE) vkFreeMemory(vkrt->core.device, vkrt->core.materialData.memory, NULL);
        if (vkrt->core.emissiveMeshData.buffer != VK_NULL_HANDLE) vkDestroyBuffer(vkrt->core.device, vkrt->core.emissiveMeshData.buffer, NULL);
        if (vkrt->core.emissiveMeshData.memory != VK_NULL_HANDLE) vkFreeMemory(vkrt->core.device, vkrt->core.emissiveMeshData.memory, NULL);
        if (vkrt->core.emissiveTriangleData.buffer != VK_NULL_HANDLE) vkDestroyBuffer(vkrt->core.device, vkrt->core.emissiveTriangleData.buffer, NULL);
        if (vkrt->core.emissiveTriangleData.memory != VK_NULL_HANDLE) vkFreeMemory(vkrt->core.device, vkrt->core.emissiveTriangleData.memory, NULL);
        if (vkrt->core.sceneData && vkrt->core.sceneDataMemory != VK_NULL_HANDLE) {
            vkUnmapMemory(vkrt->core.device, vkrt->core.sceneDataMemory);
            vkrt->core.sceneData = NULL;
        }
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
        for (size_t i = 0; i < VKRT_MAX_FRAMES_IN_FLIGHT; i++) {
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
            vkFreeCommandBuffers(vkrt->core.device, vkrt->runtime.commandPool, VKRT_ARRAY_COUNT(vkrt->runtime.commandBuffers), vkrt->runtime.commandBuffers);
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
        vkrt->runtime.appInitialized = 0;
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

    VKRT_AppHooks hooks = vkrt->appHooks;
    memset(&vkrt->core, 0, sizeof(vkrt->core));
    memset(&vkrt->runtime, 0, sizeof(vkrt->runtime));
    memset(&vkrt->state, 0, sizeof(vkrt->state));
    vkrt->appHooks = hooks;

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

    if (!window) return;
    VKRT* vkrt = (VKRT*)glfwGetWindowUserPointer(window);
    if (!vkrt) return;
    vkrt->runtime.framebufferResized = VK_TRUE;
}
