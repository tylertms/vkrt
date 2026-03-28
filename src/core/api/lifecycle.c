#include "GLFW/glfw3.h"
#include "accel/accel.h"
#include "command/pool.h"
#include "config.h"
#include "debug.h"
#include "descriptor.h"
#include "device.h"
#include "export.h"
#include "images.h"
#include "instance.h"
#include "pipeline.h"
#include "procs.h"
#include "rebuild.h"
#include "scene.h"
#include "state.h"
#include "surface.h"
#include "swapchain.h"
#include "sync.h"
#include "textures.h"
#include "validation.h"
#include "vkrt_internal.h"
#include "vkrt_types.h"
#include "vulkan/vulkan_core.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <dwmapi.h>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#endif

static inline void logStepTime(const char* stepName, uint64_t startTime) {
    LOG_TRACE("%s in %.3f ms", stepName, (double)(getMicroseconds() - startTime) / 1e3);
}

static inline uint32_t defaultWindowExtentFromDisplay(uint32_t displayExtent) {
    return displayExtent > 1u ? (uint32_t)(((uint64_t)displayExtent * 4u) / 5u) : 1u;
}

static uint32_t gGlfwInitRefCount = 0u;

static VKRT_Result acquireGLFW(void) {
    if (gGlfwInitRefCount == 0u && !glfwInit()) {
        LOG_ERROR("Failed to initialize GLFW");
        return VKRT_ERROR_INITIALIZATION_FAILED;
    }

    gGlfwInitRefCount++;
    return VKRT_SUCCESS;
}

static void releaseGLFW(void) {
    if (gGlfwInitRefCount == 0u) return;

    gGlfwInitRefCount--;
    if (gGlfwInitRefCount == 0u) {
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

static void releaseSceneMaterials(VKRT* vkrt) {
    if (!vkrt) return;
    free(vkrt->core.materials);
    vkrt->core.materials = NULL;
    vkrt->core.materialCount = 0;
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

static void cleanupRayTracingResources(VKRT* vkrt) {
    if (!vkrt || vkrt->core.device == VK_NULL_HANDLE) return;

    destroyBufferAndMemory(vkrt, &vkrt->core.shaderBindingTableBuffer, &vkrt->core.shaderBindingTableMemory);
    destroyBufferAndMemory(
        vkrt,
        &vkrt->core.selectionShaderBindingTableBuffer,
        &vkrt->core.selectionShaderBindingTableMemory
    );
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
    destroyBufferAndMemory(
        vkrt,
        &vkrt->core.sceneEmissiveTriangleData.buffer,
        &vkrt->core.sceneEmissiveTriangleData.memory
    );
    destroyBufferAndMemory(vkrt, &vkrt->core.sceneMeshAliasQ.buffer, &vkrt->core.sceneMeshAliasQ.memory);
    destroyBufferAndMemory(vkrt, &vkrt->core.sceneMeshAliasIdx.buffer, &vkrt->core.sceneMeshAliasIdx.memory);
    destroyBufferAndMemory(vkrt, &vkrt->core.sceneTriAliasQ.buffer, &vkrt->core.sceneTriAliasQ.memory);
    destroyBufferAndMemory(vkrt, &vkrt->core.sceneTriAliasIdx.buffer, &vkrt->core.sceneTriAliasIdx.memory);
    destroyBufferAndMemory(vkrt, &vkrt->core.sceneRGB2SpecSRGBData.buffer, &vkrt->core.sceneRGB2SpecSRGBData.memory);
    vkrt->core.rgb2specSRGBInfo = (RGB2SpecTableInfo){0};

    for (uint32_t i = 0; i < vkrt->core.meshCount; i++) {
        if (!vkrt->core.meshes[i].ownsGeometry) continue;
        vkrtDestroyAccelerationStructureResources(vkrt, &vkrt->core.meshes[i].bottomLevelAccelerationStructure);
    }

    releaseMeshHostGeometry(vkrt);
    releaseSceneMaterials(vkrt);
    vkrtReleaseSceneTextures(vkrt);

    destroyBufferAndMemory(vkrt, &vkrt->core.vertexData.buffer, &vkrt->core.vertexData.memory);
    destroyBufferAndMemory(vkrt, &vkrt->core.indexData.buffer, &vkrt->core.indexData.memory);
    destroyAutoExposureReadbacks(vkrt);

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
        const uint32_t commandBufferCount = VKRT_MAX_FRAMES_IN_FLIGHT;
        vkFreeCommandBuffers(
            vkrt->core.device,
            vkrt->runtime.commandPool,
            commandBufferCount,
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
    releaseSceneMaterials(vkrt);
    vkrtReleaseSceneTextures(vkrt);
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

static void initializeRuntimeDefaults(VKRT* vkrt, const VKRT_CreateInfo* createInfo) {
    if (!vkrt || !createInfo) return;

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
    vkrt->core.textureResourceRevision = 0;
    vkrt->core.lightResourceRevision = 0;
    vkrt->core.sceneRevision = 1;
    vkrt->core.materialRevision = 1;
    vkrt->core.textureRevision = 1;
    vkrt->core.lightRevision = 1;
    vkrt->core.emissiveMeshCount = 0;
    vkrt->core.emissiveTriangleCount = 0;
    vkrt->renderStatus.renderPhase = VKRT_RENDER_PHASE_INACTIVE;
    vkrt->renderStatus.renderDenoiseEnabled = 1u;
    vkrt->renderStatus.renderTargetSamples = 0;
    vkrt->renderControl.finalImageDenoiseEnabled = 1u;
}

static void configureDisplayMetricsAndExtent(
    VKRT* vkrt,
    const VKRT_CreateInfo* createInfo,
    uint32_t* outWidth,
    uint32_t* outHeight
) {
    if (!vkrt || !createInfo || !outWidth || !outHeight) return;

    uint32_t displayWidth = VKRT_DEFAULT_WIDTH;
    uint32_t displayHeight = VKRT_DEFAULT_HEIGHT;
    vkrtQueryDisplayMetrics(NULL, &displayWidth, &displayHeight, NULL);
    vkrt->runtime.displayWidth = displayWidth;
    vkrt->runtime.displayHeight = displayHeight;

    uint32_t width = createInfo->startFullscreen ? displayWidth : createInfo->width;
    uint32_t height = createInfo->startFullscreen ? displayHeight : createInfo->height;
    if (width == 0) width = createInfo->startMaximized ? displayWidth : defaultWindowExtentFromDisplay(displayWidth);
    if (height == 0) {
        height = createInfo->startMaximized ? displayHeight : defaultWindowExtentFromDisplay(displayHeight);
    }
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

    *outWidth = width;
    *outHeight = height;
}

static VKRT_Result createRuntimeWindow(
    VKRT* vkrt,
    const VKRT_CreateInfo* createInfo,
    const char* title,
    uint32_t width,
    uint32_t height
) {
    if (!vkrt || !createInfo || !title) return VKRT_ERROR_INVALID_ARGUMENT;
    if (vkrt->runtime.headless) return VKRT_SUCCESS;

    GLFWmonitor* fullscreenMonitor = createInfo->startFullscreen ? glfwGetPrimaryMonitor() : NULL;
    int maximized = (!createInfo->startFullscreen && createInfo->startMaximized) ? GLFW_TRUE : GLFW_FALSE;

    glfwWindowHint(GLFW_MAXIMIZED, maximized);
    vkrt->runtime.window = glfwCreateWindow((int)width, (int)height, title, fullscreenMonitor, 0);
    if (!vkrt->runtime.window) {
        LOG_ERROR("Failed to create GLFW window");
        return VKRT_ERROR_INITIALIZATION_FAILED;
    }

#ifdef _WIN32
    BOOL value = TRUE;
    HRESULT hr = DwmSetWindowAttribute(
        glfwGetWin32Window(vkrt->runtime.window),
        DWMWA_USE_IMMERSIVE_DARK_MODE,
        &value,
        sizeof(value)
    );

    if (!SUCCEEDED(hr)) {
        LOG_INFO("Did not use immersive title bar");
    }
#endif

    glfwSetWindowUserPointer(vkrt->runtime.window, vkrt);
    glfwSetFramebufferSizeCallback(vkrt->runtime.window, VKRT_framebufferResizedCallback);
    return VKRT_SUCCESS;
}

static VKRT_Result createInstanceAndDevice(VKRT* vkrt, const VKRT_CreateInfo* createInfo) {
    if (!vkrt || !createInfo) return VKRT_ERROR_INVALID_ARGUMENT;

    if (createInstance(vkrt, !vkrt->runtime.headless) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;
    if (setupDebugMessenger(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;
    if (!vkrt->runtime.headless && createSurface(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;
    if (pickPhysicalDevice(vkrt, createInfo) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;
    if (createLogicalDevice(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;
    if (loadDeviceProcs(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;
    if (createQueryPool(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;

    return VKRT_SUCCESS;
}

static VKRT_Result createRenderBackendResources(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    if (!vkrt->runtime.headless) {
        glfwPollEvents();
        if (createSwapChain(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;
        if (createImageViews(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;
    }

    if (createCommandPool(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;
    if (vkrtEnsureTextureBindings(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;
    if (createDescriptorSetLayout(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;
    if (createRayTracingPipeline(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;
    if (createSelectionRayTracingPipeline(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;
    if (createComputePipeline(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;
    if (createGPUImages(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;
    if (createSceneUniform(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;
    if (createRGB2SpecResources(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;
    if (createDescriptorPool(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;
    if (createDescriptorSet(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;
    if (createShaderBindingTable(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;
    if (createSelectionShaderBindingTable(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;
    if (createCommandBuffers(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;
    if (createSyncObjects(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;

    return VKRT_SUCCESS;
}

static void runInitializationSteps(VKRT* vkrt, const VKRT_CreateInfo* createInfo, uint64_t initStartTime) {
    if (!vkrt || !createInfo) return;

    uint64_t stepStartTime = initStartTime;
    const char* title = createInfo->title ? createInfo->title : "VKRT";
    uint32_t width = 0u;
    uint32_t height = 0u;

    initializeRuntimeDefaults(vkrt, createInfo);
    configureDisplayMetricsAndExtent(vkrt, createInfo, &width, &height);

    stepStartTime = getMicroseconds();
    if (createRuntimeWindow(vkrt, createInfo, title, width, height) != VKRT_SUCCESS) goto init_failed;
    if (!vkrt->runtime.headless) {
        logStepTime("Window setup complete", stepStartTime);
    }

    stepStartTime = getMicroseconds();
    if (createInstanceAndDevice(vkrt, createInfo) != VKRT_SUCCESS) goto init_failed;
    logStepTime("Vulkan instance, surface, and device ready", stepStartTime);

    stepStartTime = getMicroseconds();
    if (createRenderBackendResources(vkrt) != VKRT_SUCCESS) goto init_failed;
    logStepTime("Render backend resources ready", stepStartTime);

    stepStartTime = getMicroseconds();
    if (vkrt->appHooks.init) {
        vkrt->appHooks.init(vkrt, vkrt->appHooks.userData);
    }
    vkrt->runtime.appInitialized = 1;
    logStepTime("Application initialization complete", stepStartTime);

    LOG_INFO("VKRT initialization complete in %.3f ms", (double)(getMicroseconds() - initStartTime) / 1e3);
    return;

init_failed:
    VKRT_deinit(vkrt);
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
        glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
        glfwWindowHint(GLFW_SCALE_FRAMEBUFFER, GLFW_TRUE);
    }
    logStepTime("GLFW setup complete", stepStartTime);
    runInitializationSteps(vkrt, createInfo, initStartTime);
    if (!vkrt->core.device) {
        return VKRT_ERROR_OPERATION_FAILED;
    }
    return VKRT_SUCCESS;
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
        shutdownRenderImageExporter(vkrt);
        logStepTime("Render image export worker shutdown complete", stepStartTime);

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
        cleanupRayTracingResources(vkrt);
        logStepTime("Ray tracing resource cleanup complete", stepStartTime);

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
    memset(&vkrt->renderControl, 0, sizeof(vkrt->renderControl));
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
