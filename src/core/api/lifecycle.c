#include "buffer.h"
#include "command/pool.h"
#include "descriptor.h"
#include "device.h"
#include "procs.h"
#include "images.h"
#include "instance.h"
#include "pipeline.h"
#include "scene.h"
#include "accel/accel.h"
#include "surface.h"
#include "sync.h"
#include "swapchain.h"
#include "rebuild.h"
#include "state.h"
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

static inline uint32_t defaultWindowExtentFromDisplay(uint32_t displayExtent) {
    return displayExtent > 1u ? (uint32_t)(((uint64_t)displayExtent * 4u) / 5u) : 1u;
}

static uint32_t g_glfwInitRefCount = 0u;

static VKRT_Result acquireGLFW(void) {
    if (g_glfwInitRefCount == 0u && !glfwInit()) {
        LOG_ERROR("Failed to initialize GLFW");
        return VKRT_ERROR_INITIALIZATION_FAILED;
    }

    g_glfwInitRefCount++;
    return VKRT_SUCCESS;
}

static void releaseGLFW(void) {
    if (g_glfwInitRefCount == 0u) return;

    g_glfwInitRefCount--;
    if (g_glfwInitRefCount == 0u) {
        glfwTerminate();
    }
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

static void releaseMeshHostGeometry(VKRT* vkrt) {
    if (!vkrt) return;
    if (!vkrt->core.meshes) {
        vkrt->core.meshCount = 0;
        return;
    }

    for (uint32_t i = 0; i < vkrt->core.meshCount; i++) {
        if (!vkrt->core.meshes[i].ownsGeometry) continue;
        free(vkrt->core.meshes[i].vertices);
        free(vkrt->core.meshes[i].indices);
        vkrt->core.meshes[i].vertices = NULL;
        vkrt->core.meshes[i].indices = NULL;
    }

    free(vkrt->core.meshes);
    vkrt->core.meshes = NULL;
    vkrt->core.meshCount = 0;
}

static void releaseGeometryLayout(VKRT* vkrt) {
    if (!vkrt) return;
    vkrt->core.geometryLayout.vertexCapacity = 0;
    vkrt->core.geometryLayout.indexCapacity = 0;
}

static void cleanupSwapChainAndStorageResources(VKRT* vkrt) {
    if (!vkrt || vkrt->core.device == VK_NULL_HANDLE) return;

    if (vkrt->runtime.swapChain != VK_NULL_HANDLE) {
        cleanupSwapChain(vkrt);
    }
    destroyGPUImages(vkrt);
}

static void cleanupRayTracingAndRenderPassResources(VKRT* vkrt) {
    if (!vkrt || vkrt->core.device == VK_NULL_HANDLE) return;

    if (vkrt->runtime.renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(vkrt->core.device, vkrt->runtime.renderPass, NULL);
        vkrt->runtime.renderPass = VK_NULL_HANDLE;
    }

    destroyBufferAndMemory(vkrt, &vkrt->core.shaderBindingTableBuffer, &vkrt->core.shaderBindingTableMemory);
    destroyBufferAndMemory(vkrt, &vkrt->core.selectionShaderBindingTableBuffer, &vkrt->core.selectionShaderBindingTableMemory);
}

static void cleanupSceneAndAccelerationResources(VKRT* vkrt) {
    if (!vkrt || vkrt->core.device == VK_NULL_HANDLE) return;

    for (uint32_t i = 0; i < VKRT_MAX_FRAMES_IN_FLIGHT; i++) {
        vkrtCleanupFrameSceneUpdate(vkrt, i);
    }
    vkrtDestroyAccelerationStructureResources(vkrt, &vkrt->core.sceneTopLevelAccelerationStructure);
    vkrtDestroyAccelerationStructureResources(vkrt, &vkrt->core.selectionTopLevelAccelerationStructure);
    destroyBufferAndMemory(vkrt, &vkrt->core.sceneMeshData.buffer, &vkrt->core.sceneMeshData.memory);
    destroyBufferAndMemory(vkrt, &vkrt->core.sceneMaterialData.buffer, &vkrt->core.sceneMaterialData.memory);
    destroyBufferAndMemory(vkrt, &vkrt->core.sceneEmissiveMeshData.buffer, &vkrt->core.sceneEmissiveMeshData.memory);
    destroyBufferAndMemory(vkrt, &vkrt->core.sceneEmissiveTriangleData.buffer, &vkrt->core.sceneEmissiveTriangleData.memory);
    destroyBufferAndMemory(vkrt, &vkrt->core.sceneMeshAliasQ.buffer, &vkrt->core.sceneMeshAliasQ.memory);
    destroyBufferAndMemory(vkrt, &vkrt->core.sceneMeshAliasIdx.buffer, &vkrt->core.sceneMeshAliasIdx.memory);
    destroyBufferAndMemory(vkrt, &vkrt->core.sceneTriAliasQ.buffer, &vkrt->core.sceneTriAliasQ.memory);
    destroyBufferAndMemory(vkrt, &vkrt->core.sceneTriAliasIdx.buffer, &vkrt->core.sceneTriAliasIdx.memory);

    for (uint32_t i = 0; i < vkrt->core.meshCount; i++) {
        if (!vkrt->core.meshes[i].ownsGeometry) continue;
        vkrtDestroyAccelerationStructureResources(vkrt, &vkrt->core.meshes[i].bottomLevelAccelerationStructure);
    }

    releaseMeshHostGeometry(vkrt);

    destroyBufferAndMemory(vkrt, &vkrt->core.vertexData.buffer, &vkrt->core.vertexData.memory);
    destroyBufferAndMemory(vkrt, &vkrt->core.indexData.buffer, &vkrt->core.indexData.memory);

    if (vkrt->core.selectionData && vkrt->core.selection.memory != VK_NULL_HANDLE) {
        vkUnmapMemory(vkrt->core.device, vkrt->core.selection.memory);
        vkrt->core.selectionData = NULL;
    }
    destroyBufferAndMemory(vkrt, &vkrt->core.selection.buffer, &vkrt->core.selection.memory);

    for (uint32_t i = 0; i < VKRT_MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkrt->core.sceneFrameData[i] && vkrt->core.sceneDataMemories[i] != VK_NULL_HANDLE) {
            vkUnmapMemory(vkrt->core.device, vkrt->core.sceneDataMemories[i]);
            vkrt->core.sceneFrameData[i] = NULL;
        }
        destroyBufferAndMemory(vkrt, &vkrt->core.sceneDataBuffers[i], &vkrt->core.sceneDataMemories[i]);
    }
    vkrt->core.sceneData = NULL;
}

static void cleanupDescriptorAndPipelineResources(VKRT* vkrt) {
    if (!vkrt || vkrt->core.device == VK_NULL_HANDLE) return;

    if (vkrt->core.overlayDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vkrt->core.device, vkrt->core.overlayDescriptorPool, NULL);
        vkrt->core.overlayDescriptorPool = VK_NULL_HANDLE;
    }
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
    if (vkrt->core.selectionRayTracingPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vkrt->core.device, vkrt->core.selectionRayTracingPipeline, NULL);
        vkrt->core.selectionRayTracingPipeline = VK_NULL_HANDLE;
    }
    if (vkrt->core.computePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vkrt->core.device, vkrt->core.computePipeline, NULL);
        vkrt->core.computePipeline = VK_NULL_HANDLE;
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
            vkrt->runtime.commandBuffers
        );
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
    releaseGeometryLayout(vkrt);
    resetRenderFinishedSemaphores(vkrt, vkrt->runtime.swapChainImageCount, 0);
}

void VKRT_defaultCreateInfo(VKRT_CreateInfo* createInfo) {
    if (!createInfo) return;

    *createInfo = (VKRT_CreateInfo){
        .width = 0,
        .height = 0,
        .title = "VKRT",
        .startMaximized = 1,
        .startFullscreen = 0,
        .headless = 0,
        .disableSER = 0,
        .preferredDeviceIndex = -1,
        .preferredDeviceName = NULL,
    };
}

VKRT_Result VKRT_create(VKRT** outVkrt) {
    if (!outVkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    VKRT* vkrt = (VKRT*)calloc(1, sizeof(VKRT));
    if (!vkrt) return VKRT_ERROR_OUT_OF_MEMORY;

    *outVkrt = vkrt;
    return VKRT_SUCCESS;
}

void VKRT_destroy(VKRT* vkrt) {
    if (!vkrt) return;
    VKRT_deinit(vkrt);
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

    if (acquireGLFW() != VKRT_SUCCESS) return VKRT_ERROR_INITIALIZATION_FAILED;
    vkrt->runtime.glfwInitialized = 1u;
    if (!createInfo->headless) {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    }
    logStepTime("GLFW setup complete", stepStartTime);

    vkrt->runtime.swapChainFormatLogInitialized = VK_FALSE;
    vkrt->runtime.lastLoggedSwapChainFormat = VK_FORMAT_UNDEFINED;
    vkrt->runtime.lastLoggedSwapChainColorSpace = VK_COLOR_SPACE_MAX_ENUM_KHR;
    vkrt->runtime.appInitialized = 0;
    vkrt->runtime.headless = createInfo->headless ? VK_TRUE : VK_FALSE;
    vkrt->runtime.disableSER = createInfo->disableSER ? 1u : 0u;
    for (uint32_t i = 0; i < VKRT_MAX_FRAMES_IN_FLIGHT; i++) {
        vkrt->core.descriptorSetReady[i] = VK_FALSE;
    }
    vkrt->core.sceneResourceRevision = 0;
    vkrt->core.materialResourceRevision = 0;
    vkrt->core.sceneRevision = 1;
    vkrt->core.materialRevision = 1;
    vkrt->core.emissiveMeshCount = 0;
    vkrt->core.emissiveTriangleCount = 0;
    vkrt->renderStatus.renderModeActive = 0;
    vkrt->renderStatus.renderModeFinished = 0;
    vkrt->renderStatus.renderTargetSamples = 0;

    const char* title = createInfo->title ? createInfo->title : "VKRT";
    uint32_t displayWidth = VKRT_DEFAULT_WIDTH;
    uint32_t displayHeight = VKRT_DEFAULT_HEIGHT;
    vkrtQueryDisplayMetrics(NULL, &displayWidth, &displayHeight, NULL);
    vkrt->runtime.displayWidth = displayWidth;
    vkrt->runtime.displayHeight = displayHeight;

    uint32_t width = createInfo->startFullscreen ? displayWidth : createInfo->width;
    uint32_t height = createInfo->startFullscreen ? displayHeight : createInfo->height;
    if (width == 0) width = createInfo->startMaximized ? displayWidth : defaultWindowExtentFromDisplay(displayWidth);
    if (height == 0) height = createInfo->startMaximized ? displayHeight : defaultWindowExtentFromDisplay(displayHeight);
    if (width == 0) width = VKRT_DEFAULT_WIDTH;
    if (height == 0) height = VKRT_DEFAULT_HEIGHT;
    if (vkrt->runtime.headless) {
        vkrt->runtime.swapChainExtent = (VkExtent2D){width, height};
        vkrt->runtime.renderExtent = (VkExtent2D){width, height};
        vkrt->runtime.displayViewportRect[0] = 0u;
        vkrt->runtime.displayViewportRect[1] = 0u;
        vkrt->runtime.displayViewportRect[2] = width;
        vkrt->runtime.displayViewportRect[3] = height;
    }

    if (!vkrt->runtime.headless) {
        stepStartTime = getMicroseconds();
        GLFWmonitor* fullscreenMonitor = createInfo->startFullscreen ? glfwGetPrimaryMonitor() : NULL;
        glfwWindowHint(GLFW_MAXIMIZED, (!createInfo->startFullscreen && createInfo->startMaximized) ? GLFW_TRUE : GLFW_FALSE);
        vkrt->runtime.window = glfwCreateWindow((int)width, (int)height, title, fullscreenMonitor, 0);
        if (!vkrt->runtime.window) {
            LOG_ERROR("Failed to create GLFW window");
            goto init_failed;
        }

        glfwSetWindowUserPointer(vkrt->runtime.window, vkrt);
        glfwSetFramebufferSizeCallback(vkrt->runtime.window, VKRT_framebufferResizedCallback);

        logStepTime("Window setup complete", stepStartTime);
    }

    stepStartTime = getMicroseconds();
    if (createInstance(vkrt, !vkrt->runtime.headless) != VKRT_SUCCESS) goto init_failed;
    logStepTime("Vulkan instance created", stepStartTime);

    stepStartTime = getMicroseconds();
    if (setupDebugMessenger(vkrt) != VKRT_SUCCESS) goto init_failed;
    logStepTime("Debug messenger setup complete", stepStartTime);

    if (!vkrt->runtime.headless) {
        stepStartTime = getMicroseconds();
        if (createSurface(vkrt) != VKRT_SUCCESS) goto init_failed;
        logStepTime("Surface created", stepStartTime);
    }

    stepStartTime = getMicroseconds();
    if (pickPhysicalDevice(vkrt, createInfo) != VKRT_SUCCESS) goto init_failed;
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

    if (!vkrt->runtime.headless) {
        glfwPollEvents();

        stepStartTime = getMicroseconds();
        if (createSwapChain(vkrt) != VKRT_SUCCESS) goto init_failed;
        if (createImageViews(vkrt) != VKRT_SUCCESS) goto init_failed;
        if (createRenderPass(vkrt, &vkrt->runtime.renderPass) != VKRT_SUCCESS) goto init_failed;
        if (createFramebuffers(vkrt) != VKRT_SUCCESS) goto init_failed;
        logStepTime("Swapchain and framebuffers ready", stepStartTime);
    }

    stepStartTime = getMicroseconds();
    if (createCommandPool(vkrt) != VKRT_SUCCESS) goto init_failed;
    if (createDescriptorSetLayout(vkrt) != VKRT_SUCCESS) goto init_failed;
    logStepTime("Command pool and descriptor layout ready", stepStartTime);

    stepStartTime = getMicroseconds();
    if (createRayTracingPipeline(vkrt) != VKRT_SUCCESS) goto init_failed;
#if VKRT_SELECTION_ENABLED
    if (createSelectionRayTracingPipeline(vkrt) != VKRT_SUCCESS) goto init_failed;
    if (createComputePipeline(vkrt) != VKRT_SUCCESS) goto init_failed;
#endif
    logStepTime("Ray tracing pipelines and compute pipeline ready", stepStartTime);

    stepStartTime = getMicroseconds();
    if (createGPUImages(vkrt) != VKRT_SUCCESS) goto init_failed;
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
#if VKRT_SELECTION_ENABLED
    if (createSelectionShaderBindingTable(vkrt) != VKRT_SUCCESS) goto init_failed;
#endif
    logStepTime("Shader binding tables ready", stepStartTime);

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
        shutdownRenderPNGExporter(vkrt);
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
    if (vkrt->runtime.glfwInitialized) {
        releaseGLFW();
    }
    logStepTime("GLFW shutdown complete", stepStartTime);

    VKRT_AppHooks hooks = vkrt->appHooks;
    memset(&vkrt->core, 0, sizeof(vkrt->core));
    memset(&vkrt->runtime, 0, sizeof(vkrt->runtime));
    memset(&vkrt->sceneSettings, 0, sizeof(vkrt->sceneSettings));
    memset(&vkrt->renderStatus, 0, sizeof(vkrt->renderStatus));
    memset(&vkrt->renderView, 0, sizeof(vkrt->renderView));
    memset(&vkrt->timing, 0, sizeof(vkrt->timing));
    memset(&vkrt->autoSPP, 0, sizeof(vkrt->autoSPP));
    vkrt->appHooks = hooks;

    LOG_INFO("VKRT deinitialization complete in %.3f ms", (double)(getMicroseconds() - deinitStartTime) / 1e3);
}

int VKRT_shouldDeinit(VKRT* vkrt) {
    if (vkrt && vkrt->runtime.headless) return 0;
    return (vkrt && vkrt->runtime.window) ? glfwWindowShouldClose(vkrt->runtime.window) : 1;
}

void VKRT_poll(VKRT* vkrt) {
    if (!vkrt) return;
    if (vkrt->runtime.headless) return;
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
