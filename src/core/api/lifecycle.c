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
#include "sync.h"
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

static void destroyBufferAndMemory(VKRT* vkrt, VkBuffer* buffer, VkDeviceMemory* memory) {
    if (!vkrt || vkrt->core.device == VK_NULL_HANDLE || !buffer || !memory) return;

    if (*buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vkrt->core.device, *buffer, NULL);
        *buffer = VK_NULL_HANDLE;
    }
    if (*memory != VK_NULL_HANDLE) {
        vkFreeMemory(vkrt->core.device, *memory, NULL);
        *memory = VK_NULL_HANDLE;
    }
}

static void destroyAccelerationStructureResources(VKRT* vkrt, AccelerationStructure* accelerationStructure) {
    if (!vkrt || !accelerationStructure || vkrt->core.device == VK_NULL_HANDLE) return;

    if (vkrt->core.procs.vkDestroyAccelerationStructureKHR &&
        accelerationStructure->structure != VK_NULL_HANDLE) {
        vkrt->core.procs.vkDestroyAccelerationStructureKHR(
            vkrt->core.device,
            accelerationStructure->structure,
            NULL);
    }
    accelerationStructure->structure = VK_NULL_HANDLE;

    destroyBufferAndMemory(vkrt, &accelerationStructure->buffer, &accelerationStructure->memory);
    accelerationStructure->deviceAddress = 0;
    accelerationStructure->needsRebuild = 0;
}

static void releaseMeshHostGeometry(VKRT* vkrt) {
    if (!vkrt) return;
    if (!vkrt->core.meshes) {
        vkrt->core.meshData.count = 0;
        return;
    }

    for (uint32_t i = 0; i < vkrt->core.meshData.count; i++) {
        if (!vkrt->core.meshes[i].ownsGeometry) continue;
        free(vkrt->core.meshes[i].vertices);
        free(vkrt->core.meshes[i].indices);
        vkrt->core.meshes[i].vertices = NULL;
        vkrt->core.meshes[i].indices = NULL;
    }

    free(vkrt->core.meshes);
    vkrt->core.meshes = NULL;
    vkrt->core.meshData.count = 0;
}

static void cleanupSwapChainAndStorageResources(VKRT* vkrt) {
    if (!vkrt || vkrt->core.device == VK_NULL_HANDLE) return;

    if (vkrt->runtime.swapChain != VK_NULL_HANDLE) {
        cleanupSwapChain(vkrt);
    }
    destroyStorageImage(vkrt);
}

static void cleanupRayTracingAndRenderPassResources(VKRT* vkrt) {
    if (!vkrt || vkrt->core.device == VK_NULL_HANDLE) return;

    if (vkrt->runtime.renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(vkrt->core.device, vkrt->runtime.renderPass, NULL);
        vkrt->runtime.renderPass = VK_NULL_HANDLE;
    }

    destroyBufferAndMemory(vkrt, &vkrt->core.shaderBindingTableBuffer, &vkrt->core.shaderBindingTableMemory);
}

static void cleanupSceneAndAccelerationResources(VKRT* vkrt) {
    if (!vkrt || vkrt->core.device == VK_NULL_HANDLE) return;

    destroyAccelerationStructureResources(vkrt, &vkrt->core.topLevelAccelerationStructure);

    for (uint32_t i = 0; i < vkrt->core.meshData.count; i++) {
        if (!vkrt->core.meshes[i].ownsGeometry) continue;
        destroyAccelerationStructureResources(vkrt, &vkrt->core.meshes[i].bottomLevelAccelerationStructure);
    }

    releaseMeshHostGeometry(vkrt);

    destroyBufferAndMemory(vkrt, &vkrt->core.vertexData.buffer, &vkrt->core.vertexData.memory);
    destroyBufferAndMemory(vkrt, &vkrt->core.indexData.buffer, &vkrt->core.indexData.memory);
    destroyBufferAndMemory(vkrt, &vkrt->core.meshData.buffer, &vkrt->core.meshData.memory);

    if (vkrt->core.pickData && vkrt->core.pickBuffer.memory != VK_NULL_HANDLE) {
        vkUnmapMemory(vkrt->core.device, vkrt->core.pickBuffer.memory);
        vkrt->core.pickData = NULL;
    }
    destroyBufferAndMemory(vkrt, &vkrt->core.pickBuffer.buffer, &vkrt->core.pickBuffer.memory);

    destroyBufferAndMemory(vkrt, &vkrt->core.materialData.buffer, &vkrt->core.materialData.memory);
    destroyBufferAndMemory(vkrt, &vkrt->core.emissiveMeshData.buffer, &vkrt->core.emissiveMeshData.memory);
    destroyBufferAndMemory(vkrt, &vkrt->core.emissiveTriangleData.buffer, &vkrt->core.emissiveTriangleData.memory);

    if (vkrt->core.sceneData && vkrt->core.sceneDataMemory != VK_NULL_HANDLE) {
        vkUnmapMemory(vkrt->core.device, vkrt->core.sceneDataMemory);
        vkrt->core.sceneData = NULL;
    }
    destroyBufferAndMemory(vkrt, &vkrt->core.sceneDataBuffer, &vkrt->core.sceneDataMemory);
}

static void cleanupDescriptorAndPipelineResources(VKRT* vkrt) {
    if (!vkrt || vkrt->core.device == VK_NULL_HANDLE) return;

    if (vkrt->core.descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vkrt->core.device, vkrt->core.descriptorPool, NULL);
        vkrt->core.descriptorPool = VK_NULL_HANDLE;
    }
    if (vkrt->core.descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vkrt->core.device, vkrt->core.descriptorSetLayout, NULL);
        vkrt->core.descriptorSetLayout = VK_NULL_HANDLE;
    }
    if (vkrt->core.rayTracingPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vkrt->core.device, vkrt->core.rayTracingPipeline, NULL);
        vkrt->core.rayTracingPipeline = VK_NULL_HANDLE;
    }
    if (vkrt->core.pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vkrt->core.device, vkrt->core.pipelineLayout, NULL);
        vkrt->core.pipelineLayout = VK_NULL_HANDLE;
    }
}

static void cleanupSynchronizationResources(VKRT* vkrt) {
    if (!vkrt || vkrt->core.device == VK_NULL_HANDLE) return;

    for (size_t i = 0; i < VKRT_MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkrt->runtime.imageAvailableSemaphores[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(vkrt->core.device, vkrt->runtime.imageAvailableSemaphores[i], NULL);
            vkrt->runtime.imageAvailableSemaphores[i] = VK_NULL_HANDLE;
        }
        if (vkrt->runtime.inFlightFences[i] != VK_NULL_HANDLE) {
            vkDestroyFence(vkrt->core.device, vkrt->runtime.inFlightFences[i], NULL);
            vkrt->runtime.inFlightFences[i] = VK_NULL_HANDLE;
        }
    }

    if (resetRenderFinishedSemaphores(vkrt, vkrt->runtime.swapChainImageCount, 0) != VKRT_SUCCESS) {
        LOG_ERROR("Failed to clean up render-finished semaphores");
    }
}

static void cleanupCommandAndQueryResources(VKRT* vkrt) {
    if (!vkrt || vkrt->core.device == VK_NULL_HANDLE) return;

    if (vkrt->runtime.commandPool != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(
            vkrt->core.device,
            vkrt->runtime.commandPool,
            VKRT_ARRAY_COUNT(vkrt->runtime.commandBuffers),
            vkrt->runtime.commandBuffers);
        vkDestroyCommandPool(vkrt->core.device, vkrt->runtime.commandPool, NULL);
        vkrt->runtime.commandPool = VK_NULL_HANDLE;
    }
    if (vkrt->runtime.timestampPool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(vkrt->core.device, vkrt->runtime.timestampPool, NULL);
        vkrt->runtime.timestampPool = VK_NULL_HANDLE;
    }
}

static void cleanupHostOnlyResources(VKRT* vkrt) {
    if (!vkrt) return;
    releaseMeshHostGeometry(vkrt);
    resetRenderFinishedSemaphores(vkrt, vkrt->runtime.swapChainImageCount, 0);
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
    vkrt->runtime.swapChainFormatLogInitialized = VK_FALSE;
    vkrt->runtime.lastLoggedSwapChainFormat = VK_FORMAT_UNDEFINED;
    vkrt->runtime.lastLoggedSwapChainColorSpace = VK_COLOR_SPACE_MAX_ENUM_KHR;
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
        cleanupSwapChainAndStorageResources(vkrt);
        logStepTime("Swapchain cleanup complete", stepStartTime);

        stepStartTime = getMicroseconds();
        cleanupRayTracingAndRenderPassResources(vkrt);
        logStepTime("Render pass and shader binding table cleanup complete", stepStartTime);

        stepStartTime = getMicroseconds();
        cleanupSceneAndAccelerationResources(vkrt);
        logStepTime("Scene and acceleration resource cleanup complete", stepStartTime);

        stepStartTime = getMicroseconds();
        cleanupDescriptorAndPipelineResources(vkrt);
        logStepTime("Descriptor and pipeline cleanup complete", stepStartTime);

        stepStartTime = getMicroseconds();
        cleanupSynchronizationResources(vkrt);
        logStepTime("Synchronization object cleanup complete", stepStartTime);

        stepStartTime = getMicroseconds();
        cleanupCommandAndQueryResources(vkrt);
        logStepTime("Command and query resource cleanup complete", stepStartTime);

        stepStartTime = getMicroseconds();
        vkDestroyDevice(vkrt->core.device, NULL);
        vkrt->core.device = VK_NULL_HANDLE;
        logStepTime("Vulkan device shutdown complete", stepStartTime);
    } else {
        cleanupHostOnlyResources(vkrt);
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
