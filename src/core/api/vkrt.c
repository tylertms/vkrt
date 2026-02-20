#include "buffer.h"
#include "command.h"
#include "descriptor.h"
#include "device.h"
#include "instance.h"
#include "pipeline.h"
#include "scene.h"
#include "structure.h"
#include "surface.h"
#include "swapchain.h"
#include "validation.h"
#include "debug.h"
#include "vkrt.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>


static void logStepTime(const char* stepName, uint64_t startTime) {
    printf("[INFO]: %s in %.3f ms\n", stepName, (double)(getMicroseconds() - startTime) / 1e3);
}

static void destroyMeshAccelerationStructure(VKRT* vkrt, Mesh* mesh) {
    if (!vkrt || !vkrt->core.device || !mesh) return;

    PFN_vkDestroyAccelerationStructureKHR destroyAS = (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(vkrt->core.device, "vkDestroyAccelerationStructureKHR");
    if (!destroyAS) return;

    if (mesh->bottomLevelAccelerationStructure.structure != VK_NULL_HANDLE) {
        destroyAS(vkrt->core.device, mesh->bottomLevelAccelerationStructure.structure, NULL);
        mesh->bottomLevelAccelerationStructure.structure = VK_NULL_HANDLE;
    }

    if (mesh->bottomLevelAccelerationStructure.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vkrt->core.device, mesh->bottomLevelAccelerationStructure.buffer, NULL);
        mesh->bottomLevelAccelerationStructure.buffer = VK_NULL_HANDLE;
    }

    if (mesh->bottomLevelAccelerationStructure.memory != VK_NULL_HANDLE) {
        vkFreeMemory(vkrt->core.device, mesh->bottomLevelAccelerationStructure.memory, NULL);
        mesh->bottomLevelAccelerationStructure.memory = VK_NULL_HANDLE;
    }

    mesh->bottomLevelAccelerationStructure.deviceAddress = 0;
}

static void destroyMeshBLAS(VKRT* vkrt) {
    if (!vkrt) return;

    for (uint32_t i = 0; i < vkrt->core.meshData.count; i++) {
        if (vkrt->core.meshes[i].ownsGeometry) {
            destroyMeshAccelerationStructure(vkrt, &vkrt->core.meshes[i]);
        }
    }
}


static void rebuildMaterialBuffer(VKRT* vkrt) {
    if (!vkrt) return;

    vkDeviceWaitIdle(vkrt->core.device);

    if (vkrt->core.materialData.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vkrt->core.device, vkrt->core.materialData.buffer, NULL);
        vkrt->core.materialData.buffer = VK_NULL_HANDLE;
    }
    if (vkrt->core.materialData.memory != VK_NULL_HANDLE) {
        vkFreeMemory(vkrt->core.device, vkrt->core.materialData.memory, NULL);
        vkrt->core.materialData.memory = VK_NULL_HANDLE;
    }

    uint32_t materialCount = vkrt->core.meshData.count;
    vkrt->core.materialData.count = materialCount;
    vkrt->core.materialData.deviceAddress = 0;
    if (materialCount == 0) {
        vkrt->core.materialDataDirty = VK_FALSE;
        return;
    }

    MaterialData* materials = (MaterialData*)malloc((size_t)materialCount * sizeof(MaterialData));
    if (!materials) {
        perror("[ERROR]: Failed to allocate material buffer");
        exit(EXIT_FAILURE);
    }

    for (uint32_t i = 0; i < materialCount; i++) {
        vkrt->core.meshes[i].info.materialIndex = i;
        materials[i] = vkrt->core.meshes[i].material;
    }

    vkrt->core.materialData.deviceAddress = createBufferFromHostData(vkrt, materials,
        (VkDeviceSize)materialCount * sizeof(MaterialData),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        &vkrt->core.materialData.buffer,
        &vkrt->core.materialData.memory);

    free(materials);
    vkrt->core.materialDataDirty = VK_FALSE;
}

static void rebuildMeshBuffersAndStructures(VKRT* vkrt) {
    if (!vkrt) return;

    uint64_t startTime = getMicroseconds();
    uint64_t waitIdleStartTime = startTime;
    vkDeviceWaitIdle(vkrt->core.device);
    uint64_t waitIdleTime = getMicroseconds() - waitIdleStartTime;

    uint64_t destroyBlasStartTime = getMicroseconds();
    destroyMeshBLAS(vkrt);
    uint64_t destroyBlasTime = getMicroseconds() - destroyBlasStartTime;

    uint64_t destroyBuffersStartTime = getMicroseconds();

    if (vkrt->core.vertexData.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vkrt->core.device, vkrt->core.vertexData.buffer, NULL);
        vkrt->core.vertexData.buffer = VK_NULL_HANDLE;
    }
    if (vkrt->core.vertexData.memory != VK_NULL_HANDLE) {
        vkFreeMemory(vkrt->core.device, vkrt->core.vertexData.memory, NULL);
        vkrt->core.vertexData.memory = VK_NULL_HANDLE;
    }

    if (vkrt->core.indexData.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vkrt->core.device, vkrt->core.indexData.buffer, NULL);
        vkrt->core.indexData.buffer = VK_NULL_HANDLE;
    }
    if (vkrt->core.indexData.memory != VK_NULL_HANDLE) {
        vkFreeMemory(vkrt->core.device, vkrt->core.indexData.memory, NULL);
        vkrt->core.indexData.memory = VK_NULL_HANDLE;
    }

    vkrt->core.vertexData.count = 0;
    vkrt->core.indexData.count = 0;
    vkrt->core.vertexData.deviceAddress = 0;
    vkrt->core.indexData.deviceAddress = 0;
    uint64_t destroyBuffersTime = getMicroseconds() - destroyBuffersStartTime;

    uint32_t meshCount = vkrt->core.meshData.count;
    uint32_t totalVertexCount = 0;
    uint32_t totalIndexCount = 0;

    for (uint32_t i = 0; i < meshCount; i++) {
        if (!vkrt->core.meshes[i].ownsGeometry) continue;
        totalVertexCount += vkrt->core.meshes[i].info.vertexCount;
        totalIndexCount += vkrt->core.meshes[i].info.indexCount;
    }

    uint64_t packMeshDataTime = 0;
    uint64_t uploadMeshDataTime = 0;
    uint64_t buildBlasTime = 0;
    uint64_t syncInstancesTime = 0;

    if (meshCount > 0) {
        uint64_t packMeshDataStartTime = getMicroseconds();
        Vertex* packedVertices = (Vertex*)malloc((size_t)totalVertexCount * sizeof(Vertex));
        uint32_t* packedIndices = (uint32_t*)malloc((size_t)totalIndexCount * sizeof(uint32_t));
        if (!packedVertices || !packedIndices) {
            free(packedVertices);
            free(packedIndices);
            perror("[ERROR]: Failed to allocate packed mesh buffers");
            exit(EXIT_FAILURE);
        }

        uint32_t vertexBase = 0;
        uint32_t indexBase = 0;

        for (uint32_t i = 0; i < meshCount; i++) {
            Mesh* mesh = &vkrt->core.meshes[i];
            if (!mesh->ownsGeometry) continue;

            mesh->geometrySource = i;
            mesh->info.vertexBase = vertexBase;
            mesh->info.indexBase = indexBase;

            memcpy(packedVertices + vertexBase, mesh->vertices, (size_t)mesh->info.vertexCount * sizeof(Vertex));
            memcpy(packedIndices + indexBase, mesh->indices, (size_t)mesh->info.indexCount * sizeof(uint32_t));

            vertexBase += mesh->info.vertexCount;
            indexBase += mesh->info.indexCount;
        }
        packMeshDataTime = getMicroseconds() - packMeshDataStartTime;

        uint64_t uploadMeshDataStartTime = getMicroseconds();
        vkrt->core.vertexData.deviceAddress = createBufferFromHostData(vkrt, packedVertices,
            (VkDeviceSize)totalVertexCount * sizeof(Vertex),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            &vkrt->core.vertexData.buffer, &vkrt->core.vertexData.memory);

        vkrt->core.indexData.deviceAddress = createBufferFromHostData(vkrt, packedIndices,
            (VkDeviceSize)totalIndexCount * sizeof(uint32_t),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            &vkrt->core.indexData.buffer, &vkrt->core.indexData.memory);
        uploadMeshDataTime = getMicroseconds() - uploadMeshDataStartTime;

        free(packedVertices);
        free(packedIndices);

        vkrt->core.vertexData.count = totalVertexCount;
        vkrt->core.indexData.count = totalIndexCount;

        uint64_t buildBlasStartTime = getMicroseconds();
        for (uint32_t i = 0; i < meshCount; i++) {
            Mesh* mesh = &vkrt->core.meshes[i];
            if (!mesh->ownsGeometry) continue;
            createBottomLevelAccelerationStructure(vkrt, mesh);
        }
        buildBlasTime = getMicroseconds() - buildBlasStartTime;

        uint64_t syncInstancesStartTime = getMicroseconds();
        for (uint32_t i = 0; i < meshCount; i++) {
            Mesh* mesh = &vkrt->core.meshes[i];
            if (mesh->ownsGeometry) continue;

            Mesh* source = &vkrt->core.meshes[mesh->geometrySource];
            mesh->info.vertexBase = source->info.vertexBase;
            mesh->info.indexBase = source->info.indexBase;
            mesh->bottomLevelAccelerationStructure.deviceAddress = source->bottomLevelAccelerationStructure.deviceAddress;
        }
        syncInstancesTime = getMicroseconds() - syncInstancesStartTime;
    }

    rebuildMaterialBuffer(vkrt);

    uint64_t rebuildTlasStartTime = getMicroseconds();
    createTopLevelAccelerationStructure(vkrt);
    uint64_t rebuildTlasTime = getMicroseconds() - rebuildTlasStartTime;

    uint64_t descriptorUpdateStartTime = getMicroseconds();
    updateDescriptorSet(vkrt);
    uint64_t descriptorUpdateTime = getMicroseconds() - descriptorUpdateStartTime;

    uint64_t resetSceneStartTime = getMicroseconds();
    vkrt->core.topLevelAccelerationStructure.needsRebuild = 0;
    resetSceneData(vkrt);
    uint64_t resetSceneTime = getMicroseconds() - resetSceneStartTime;

    uint32_t uniqueGeometryCount = 0;
    for (uint32_t i = 0; i < meshCount; i++) {
        if (vkrt->core.meshes[i].ownsGeometry) uniqueGeometryCount++;
    }

    printf("[INFO]: Scene geometry rebuilt. Meshes: %u, Unique Geometry: %u, Vertices: %u, Indices: %u, in %.3f ms\n",
        meshCount,
        uniqueGeometryCount,
        totalVertexCount,
        totalIndexCount,
        (double)(getMicroseconds() - startTime) / 1e3);
    printf("[INFO]: Scene geometry rebuild breakdown. Device Wait: %.3f ms, BLAS Cleanup: %.3f ms, Buffer Cleanup: %.3f ms, Data Packing: %.3f ms, Buffer Upload: %.3f ms, BLAS Build: %.3f ms, Instance Sync: %.3f ms, TLAS Build: %.3f ms, Descriptor Update: %.3f ms, Scene Reset: %.3f ms\n",
        (double)waitIdleTime / 1e3,
        (double)destroyBlasTime / 1e3,
        (double)destroyBuffersTime / 1e3,
        (double)packMeshDataTime / 1e3,
        (double)uploadMeshDataTime / 1e3,
        (double)buildBlasTime / 1e3,
        (double)syncInstancesTime / 1e3,
        (double)rebuildTlasTime / 1e3,
        (double)descriptorUpdateTime / 1e3,
        (double)resetSceneTime / 1e3);
}

void VKRT_defaultCreateInfo(VKRT_CreateInfo* createInfo) {
    if (!createInfo) return;

    *createInfo = (VKRT_CreateInfo){
        .width = WIDTH,
        .height = HEIGHT,
        .title = "VKRT",
        .vsync = 1,
        .shaders = {
            .rgenPath = "./rgen.spv",
            .rmissPath = "./rmiss.spv",
            .rchitPath = "./rchit.spv",
        },
    };
}

int VKRT_initWithCreateInfo(VKRT* vkrt, const VKRT_CreateInfo* createInfo) {
    if (!vkrt || !createInfo) return -1;

    uint64_t initStartTime = getMicroseconds();
    uint64_t stepStartTime = initStartTime;

    if (!glfwInit()) {
        perror("[ERROR]: Failed to initialize GLFW");
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

    if (!vkrt->core.shaders.rgenPath) vkrt->core.shaders.rgenPath = "./rgen.spv";
    if (!vkrt->core.shaders.rmissPath) vkrt->core.shaders.rmissPath = "./rmiss.spv";
    if (!vkrt->core.shaders.rchitPath) vkrt->core.shaders.rchitPath = "./rchit.spv";

    stepStartTime = getMicroseconds();
    vkrt->runtime.window = glfwCreateWindow((int)width, (int)height, title, 0, 0);
    if (!vkrt->runtime.window) {
        perror("[ERROR]: Failed to create GLFW window");
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

    printf("[INFO]: VKRT initialization complete in %.3f ms\n", (double)(getMicroseconds() - initStartTime) / 1e3);
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

    vkDeviceWaitIdle(vkrt->core.device);
    logStepTime("Device idle wait complete", stepStartTime);

    stepStartTime = getMicroseconds();
    if (vkrt->appHooks.deinit) {
        vkrt->appHooks.deinit(vkrt, vkrt->appHooks.userData);
    }
    logStepTime("Application shutdown complete", stepStartTime);

    stepStartTime = getMicroseconds();
    cleanupSwapChain(vkrt);
    logStepTime("Swapchain cleanup complete", stepStartTime);

    stepStartTime = getMicroseconds();
    vkDestroyRenderPass(vkrt->core.device, vkrt->runtime.renderPass, NULL);

    vkDestroyBuffer(vkrt->core.device, vkrt->core.shaderBindingTableBuffer, NULL);
    vkFreeMemory(vkrt->core.device, vkrt->core.shaderBindingTableMemory, NULL);
    logStepTime("Render pass and shader binding table cleanup complete", stepStartTime);

    stepStartTime = getMicroseconds();
    PFN_vkDestroyAccelerationStructureKHR pvkDestroyAccelerationStructureKHR = (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(vkrt->core.device, "vkDestroyAccelerationStructureKHR");
    if (!pvkDestroyAccelerationStructureKHR) {
        perror("[ERROR]: Failed to load vkDestroyAccelerationStructureKHR during shutdown");
        exit(EXIT_FAILURE);
    }

    pvkDestroyAccelerationStructureKHR(vkrt->core.device, vkrt->core.topLevelAccelerationStructure.structure, NULL);
    vkDestroyBuffer(vkrt->core.device, vkrt->core.topLevelAccelerationStructure.buffer, NULL);
    vkFreeMemory(vkrt->core.device, vkrt->core.topLevelAccelerationStructure.memory, NULL);

    for (uint32_t i = 0; i < vkrt->core.meshData.count; i++) {
        if (!vkrt->core.meshes[i].ownsGeometry) continue;

        pvkDestroyAccelerationStructureKHR(vkrt->core.device, vkrt->core.meshes[i].bottomLevelAccelerationStructure.structure, NULL);
        vkDestroyBuffer(vkrt->core.device, vkrt->core.meshes[i].bottomLevelAccelerationStructure.buffer, NULL);
        vkFreeMemory(vkrt->core.device, vkrt->core.meshes[i].bottomLevelAccelerationStructure.memory, NULL);
        free(vkrt->core.meshes[i].vertices);
        free(vkrt->core.meshes[i].indices);
    }
    free(vkrt->core.meshes);
    vkrt->core.meshes = NULL;
    logStepTime("Acceleration structures and mesh sources cleaned", stepStartTime);

    stepStartTime = getMicroseconds();
    vkDestroyBuffer(vkrt->core.device, vkrt->core.vertexData.buffer, NULL);
    vkFreeMemory(vkrt->core.device, vkrt->core.vertexData.memory, NULL);
    vkDestroyBuffer(vkrt->core.device, vkrt->core.indexData.buffer, NULL);
    vkFreeMemory(vkrt->core.device, vkrt->core.indexData.memory, NULL);
    vkDestroyBuffer(vkrt->core.device, vkrt->core.meshData.buffer, NULL);
    vkFreeMemory(vkrt->core.device, vkrt->core.meshData.memory, NULL);
    vkDestroyBuffer(vkrt->core.device, vkrt->core.materialData.buffer, NULL);
    vkFreeMemory(vkrt->core.device, vkrt->core.materialData.memory, NULL);

    vkDestroyBuffer(vkrt->core.device, vkrt->core.sceneDataBuffer, NULL);
    vkFreeMemory(vkrt->core.device, vkrt->core.sceneDataMemory, NULL);
    logStepTime("Scene and mesh buffer cleanup complete", stepStartTime);

    stepStartTime = getMicroseconds();
    vkDestroyDescriptorPool(vkrt->core.device, vkrt->core.descriptorPool, NULL);
    vkDestroyDescriptorSetLayout(vkrt->core.device, vkrt->core.descriptorSetLayout, NULL);

    vkDestroyPipeline(vkrt->core.device, vkrt->core.rayTracingPipeline, NULL);
    vkDestroyPipelineLayout(vkrt->core.device, vkrt->core.pipelineLayout, NULL);
    logStepTime("Descriptor and pipeline cleanup complete", stepStartTime);

    stepStartTime = getMicroseconds();
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(vkrt->core.device, vkrt->runtime.imageAvailableSemaphores[i], NULL);
        vkDestroyFence(vkrt->core.device, vkrt->runtime.inFlightFences[i], NULL);
    }

    for (size_t i = 0; i < vkrt->runtime.swapChainImageCount; i++) {
        vkDestroySemaphore(vkrt->core.device, vkrt->runtime.renderFinishedSemaphores[i], NULL);
    }
    free(vkrt->runtime.renderFinishedSemaphores);
    logStepTime("Synchronization object cleanup complete", stepStartTime);

    stepStartTime = getMicroseconds();
    vkFreeCommandBuffers(vkrt->core.device, vkrt->runtime.commandPool, COUNT_OF(vkrt->runtime.commandBuffers), vkrt->runtime.commandBuffers);
    vkDestroyCommandPool(vkrt->core.device, vkrt->runtime.commandPool, NULL);

    vkDestroyQueryPool(vkrt->core.device, vkrt->runtime.timestampPool, NULL);
    logStepTime("Command and query resource cleanup complete", stepStartTime);

    stepStartTime = getMicroseconds();
    vkDestroyDevice(vkrt->core.device, NULL);

    if (enableValidationLayers) {
        DestroyDebugUtilsMessengerEXT(vkrt->core.instance, vkrt->core.debugMessenger, NULL);
    }

    vkDestroySurfaceKHR(vkrt->core.instance, vkrt->runtime.surface, NULL);
    vkDestroyInstance(vkrt->core.instance, NULL);
    logStepTime("Vulkan device and instance shutdown complete", stepStartTime);

    stepStartTime = getMicroseconds();
    glfwDestroyWindow(vkrt->runtime.window);
    glfwTerminate();
    logStepTime("GLFW shutdown complete", stepStartTime);

    printf("[INFO]: VKRT deinitialization complete in %.3f ms\n", (double)(getMicroseconds() - deinitStartTime) / 1e3);
}

int VKRT_shouldDeinit(VKRT* vkrt) {
    return vkrt ? glfwWindowShouldClose(vkrt->runtime.window) : 1;
}

void VKRT_poll(VKRT* vkrt) {
    if (!vkrt) return;
    glfwPollEvents();
}

void VKRT_beginFrame(VKRT* vkrt) {
    if (!vkrt) return;

    vkrt->runtime.frameAcquired = VK_FALSE;
    vkrt->runtime.frameSubmitted = VK_FALSE;
    vkrt->runtime.framePresented = VK_FALSE;

    vkWaitForFences(vkrt->core.device, 1, &vkrt->runtime.inFlightFences[vkrt->runtime.currentFrame], VK_TRUE, UINT64_MAX);

    if (vkrt->core.topLevelAccelerationStructure.needsRebuild) {
        vkDeviceWaitIdle(vkrt->core.device);
        createTopLevelAccelerationStructure(vkrt);
        updateDescriptorSet(vkrt);
        vkrt->core.topLevelAccelerationStructure.needsRebuild = 0;
        resetSceneData(vkrt);
    }

    VkResult result = vkAcquireNextImageKHR(
        vkrt->core.device,
        vkrt->runtime.swapChain,
        UINT64_MAX,
        vkrt->runtime.imageAvailableSemaphores[vkrt->runtime.currentFrame],
        VK_NULL_HANDLE,
        &vkrt->runtime.frameImageIndex
    );

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapChain(vkrt);
        return;
    }

    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        perror("[ERROR]: Failed to acquire next swapchain image");
        exit(EXIT_FAILURE);
    }

    vkrt->runtime.frameAcquired = VK_TRUE;
}

void VKRT_updateScene(VKRT* vkrt) {
    if (!vkrt || !vkrt->runtime.frameAcquired) return;

    if (vkrt->core.materialDataDirty) {
        rebuildMaterialBuffer(vkrt);
        updateDescriptorSet(vkrt);
    }
}

void VKRT_trace(VKRT* vkrt) {
    if (!vkrt || !vkrt->runtime.frameAcquired) return;
    vkResetFences(vkrt->core.device, 1, &vkrt->runtime.inFlightFences[vkrt->runtime.currentFrame]);

    vkResetCommandBuffer(vkrt->runtime.commandBuffers[vkrt->runtime.currentFrame], 0);
    recordCommandBuffer(vkrt, vkrt->runtime.frameImageIndex);

    VkSemaphore waitSemaphores[] = {vkrt->runtime.imageAvailableSemaphores[vkrt->runtime.currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_TRANSFER_BIT};
    VkSemaphore signalSemaphores[] = {vkrt->runtime.renderFinishedSemaphores[vkrt->runtime.frameImageIndex]};

    VkSubmitInfo submitInfo = {0};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &vkrt->runtime.commandBuffers[vkrt->runtime.currentFrame];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(vkrt->core.graphicsQueue, 1, &submitInfo, vkrt->runtime.inFlightFences[vkrt->runtime.currentFrame]) != VK_SUCCESS) {
        perror("[ERROR]: Failed to submit draw queue");
        exit(EXIT_FAILURE);
    }

    vkrt->runtime.frameSubmitted = VK_TRUE;
}

void VKRT_present(VKRT* vkrt) {
    if (!vkrt || !vkrt->runtime.frameSubmitted) return;
    VkSemaphore signalSemaphores[] = {vkrt->runtime.renderFinishedSemaphores[vkrt->runtime.frameImageIndex]};

    VkPresentInfoKHR presentInfo = {0};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &vkrt->runtime.swapChain;
    presentInfo.pImageIndices = &vkrt->runtime.frameImageIndex;

    VkResult result = vkQueuePresentKHR(vkrt->core.presentQueue, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || vkrt->runtime.framebufferResized) {
        vkrt->runtime.framebufferResized = VK_FALSE;
        recreateSwapChain(vkrt);
        return;
    }

    if (result != VK_SUCCESS) {
        perror("[ERROR]: Failed to present draw queue");
        exit(EXIT_FAILURE);
    }

    vkrt->runtime.framePresented = VK_TRUE;
}

void VKRT_endFrame(VKRT* vkrt) {
    if (!vkrt) return;

    if (vkrt->runtime.framePresented) {
        uint32_t renderedSPP = vkrt->core.sceneData->samplesPerPixel;
        recordFrameTime(vkrt);
        updateAutoSPP(vkrt);
        if (vkrt->core.descriptorSetReady && !vkrt->core.accumulationNeedsReset) {
            vkrt->state.accumulationFrame++;
            vkrt->state.totalSamples += renderedSPP;
            vkrt->core.sceneData->frameNumber++;
        }
    }

    if (vkrt->runtime.frameSubmitted) {
        vkrt->runtime.currentFrame = (vkrt->runtime.currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }
}

void VKRT_draw(VKRT* vkrt) {
    if (!vkrt) return;
    VKRT_beginFrame(vkrt);
    VKRT_updateScene(vkrt);
    VKRT_trace(vkrt);
    VKRT_present(vkrt);
    VKRT_endFrame(vkrt);
}

void VKRT_uploadMeshData(VKRT* vkrt, const Vertex* vertices, size_t vertexCount, const uint32_t* indices, size_t indexCount) {
    if (!vkrt || !vertices || !indices || vertexCount == 0 || indexCount == 0) return;

    uint64_t startTime = getMicroseconds();
    vkDeviceWaitIdle(vkrt->core.device);

    if (vertexCount > UINT32_MAX || indexCount > UINT32_MAX) {
        perror("[ERROR]: Mesh too large");
        return;
    }

    uint32_t newCount = vkrt->core.meshData.count + 1;
    Mesh* resized = (Mesh*)realloc(vkrt->core.meshes, (size_t)newCount * sizeof(Mesh));
    if (!resized) {
        perror("[ERROR]: Failed to grow mesh list");
        return;
    }

    vkrt->core.meshes = resized;
    uint32_t newIndex = vkrt->core.meshData.count;
    Mesh* mesh = &vkrt->core.meshes[newIndex];
    memset(mesh, 0, sizeof(*mesh));

    uint32_t duplicateIndex = UINT32_MAX;
    for (uint32_t i = 0; i < vkrt->core.meshData.count; i++) {
        Mesh* existing = &vkrt->core.meshes[i];
        if (existing->info.vertexCount != (uint32_t)vertexCount || existing->info.indexCount != (uint32_t)indexCount) {
            continue;
        }
        if (memcmp(existing->vertices, vertices, vertexCount * sizeof(Vertex)) != 0) {
            continue;
        }
        if (memcmp(existing->indices, indices, indexCount * sizeof(uint32_t)) != 0) {
            continue;
        }
        duplicateIndex = existing->geometrySource;
        break;
    }

    if (duplicateIndex != UINT32_MAX) {
        Mesh* source = &vkrt->core.meshes[duplicateIndex];
        mesh->vertices = source->vertices;
        mesh->indices = source->indices;
        mesh->geometrySource = duplicateIndex;
        mesh->ownsGeometry = 0;
    } else {
        mesh->vertices = (Vertex*)malloc(vertexCount * sizeof(Vertex));
        mesh->indices = (uint32_t*)malloc(indexCount * sizeof(uint32_t));
        if (!mesh->vertices || !mesh->indices) {
            free(mesh->vertices);
            free(mesh->indices);
            mesh->vertices = NULL;
            mesh->indices = NULL;
            perror("[ERROR]: Failed to allocate mesh host data");
            return;
        }

        memcpy(mesh->vertices, vertices, vertexCount * sizeof(Vertex));
        memcpy(mesh->indices, indices, indexCount * sizeof(uint32_t));

        mesh->geometrySource = newIndex;
        mesh->ownsGeometry = 1;
    }

    mesh->info.vertexCount = (uint32_t)vertexCount;
    mesh->info.indexCount = (uint32_t)indexCount;
    mesh->info.materialIndex = newIndex;

    mesh->material = (MaterialData){
        .baseColor = {1.0f, 1.0f, 1.0f},
        .roughness = 0.5f,
        .emissionColor = {1.0f, 1.0f, 1.0f},
        .emissionStrength = 0.0f,
    };

    vec3 scale = {1.f, 1.f, 1.f};
    memcpy(&mesh->info.scale, &scale, sizeof(vec3));
    memset(&mesh->info.rotation, 0, sizeof(vec3));
    memset(&mesh->info.position, 0, sizeof(vec3));

    vkrt->core.meshData.count = newCount;
    rebuildMeshBuffersAndStructures(vkrt);
    printf("[INFO]: Mesh upload complete. Total Meshes: %u, Vertices: %zu, Indices: %zu, Reused Geometry: %s, in %.3f ms\n",
        vkrt->core.meshData.count,
        vertexCount,
        indexCount,
        duplicateIndex == UINT32_MAX ? "No" : "Yes",
        (double)(getMicroseconds() - startTime) / 1e3);
}

int VKRT_removeMesh(VKRT* vkrt, uint32_t meshIndex) {
    if (!vkrt || meshIndex >= vkrt->core.meshData.count) return -1;

    uint64_t startTime = getMicroseconds();
    vkDeviceWaitIdle(vkrt->core.device);
    Mesh* removed = &vkrt->core.meshes[meshIndex];

    if (removed->ownsGeometry) {
        int32_t promotedIndex = -1;
        for (uint32_t i = 0; i < vkrt->core.meshData.count; i++) {
            if (i == meshIndex) continue;

            Mesh* candidate = &vkrt->core.meshes[i];
            if (candidate->ownsGeometry || candidate->geometrySource != meshIndex) continue;

            promotedIndex = (int32_t)i;
            candidate->ownsGeometry = 1;
            candidate->geometrySource = i;
            break;
        }

        if (promotedIndex >= 0) {
            for (uint32_t i = 0; i < vkrt->core.meshData.count; i++) {
                Mesh* mesh = &vkrt->core.meshes[i];
                if (!mesh->ownsGeometry && mesh->geometrySource == meshIndex) {
                    mesh->geometrySource = (uint32_t)promotedIndex;
                }
            }
        } else {
            free(removed->vertices);
            free(removed->indices);
        }

        destroyMeshAccelerationStructure(vkrt, removed);
    }

    uint32_t last = vkrt->core.meshData.count - 1;
    if (meshIndex != last) {
        memmove(&vkrt->core.meshes[meshIndex], &vkrt->core.meshes[meshIndex + 1], (size_t)(last - meshIndex) * sizeof(Mesh));
    }

    for (uint32_t i = 0; i < last; i++) {
        Mesh* mesh = &vkrt->core.meshes[i];
        if (mesh->ownsGeometry) {
            mesh->geometrySource = i;
            continue;
        }

        if (mesh->geometrySource > meshIndex) {
            mesh->geometrySource--;
        }
    }

    vkrt->core.meshData.count = last;
    if (last == 0) {
        free(vkrt->core.meshes);
        vkrt->core.meshes = NULL;
    } else {
        Mesh* shrunk = (Mesh*)realloc(vkrt->core.meshes, (size_t)last * sizeof(Mesh));
        if (shrunk) vkrt->core.meshes = shrunk;
    }

    rebuildMeshBuffersAndStructures(vkrt);
    printf("[INFO]: Mesh removal complete. Removed Index: %u, Remaining Meshes: %u, in %.3f ms\n",
        meshIndex,
        vkrt->core.meshData.count,
        (double)(getMicroseconds() - startTime) / 1e3);
    return 0;
}

void VKRT_updateTLAS(VKRT* vkrt) {
    if (!vkrt) return;
    vkDeviceWaitIdle(vkrt->core.device);
    createTopLevelAccelerationStructure(vkrt);
    updateDescriptorSet(vkrt);
    resetSceneData(vkrt);
}

void VKRT_applyCameraInput(VKRT* vkrt, const VKRT_CameraInput* input) {
    applyCameraInput(vkrt, input);
}

void VKRT_invalidateAccumulation(VKRT* vkrt) {
    if (!vkrt) return;
    resetSceneData(vkrt);
}

void VKRT_setSamplesPerPixel(VKRT* vkrt, uint32_t samplesPerPixel) {
    if (!vkrt) return;
    if (samplesPerPixel == 0) samplesPerPixel = 1;

    if (vkrt->state.samplesPerPixel == samplesPerPixel) return;
    vkrt->state.samplesPerPixel = samplesPerPixel;
    if (vkrt->core.sceneData) {
        vkrt->core.sceneData->samplesPerPixel = samplesPerPixel;
    }
    if (vkrt->core.sceneData) {
        resetSceneData(vkrt);
    }
}


void VKRT_setAutoSPPEnabled(VKRT* vkrt, uint8_t enabled) {
    if (!vkrt) return;
    vkrt->state.autoSPPEnabled = enabled ? 1 : 0;
    vkrt->runtime.autoSPPFastFrames = 0;
    vkrt->runtime.autoSPPSlowFrames = 0;
    vkrt->runtime.autoSPPCooldownFrames = 0;
}


void VKRT_setAutoSPPTargetFPS(VKRT* vkrt, uint32_t targetFPS) {
    if (!vkrt) return;
    if (targetFPS < 30) targetFPS = 30;
    if (targetFPS > 360) targetFPS = 360;
    vkrt->state.autoSPPTargetFps = targetFPS;
    vkrt->state.autoSPPTargetFrameMs = 1000.0f / (float)targetFPS;
}

void VKRT_setToneMappingMode(VKRT* vkrt, VKRT_ToneMappingMode toneMappingMode) {
    if (!vkrt) return;

    vkrt->state.toneMappingMode = toneMappingMode;
    if (vkrt->core.sceneData) {
        vkrt->core.sceneData->toneMappingMode = toneMappingMode;
    }
}

uint32_t VKRT_getMeshCount(const VKRT* vkrt) {
    return vkrt ? vkrt->core.meshData.count : 0;
}

int VKRT_setMeshTransform(VKRT* vkrt, uint32_t meshIndex, vec3 position, vec3 rotation, vec3 scale) {
    if (!vkrt || meshIndex >= vkrt->core.meshData.count) return -1;

    MeshInfo* info = &vkrt->core.meshes[meshIndex].info;
    if (position) glm_vec3_copy(position, info->position);
    if (rotation) glm_vec3_copy(rotation, info->rotation);
    if (scale) glm_vec3_copy(scale, info->scale);

    vkrt->core.topLevelAccelerationStructure.needsRebuild = 1;
    resetSceneData(vkrt);
    return 0;
}


int VKRT_setMeshMaterial(VKRT* vkrt, uint32_t meshIndex, const MaterialData* material) {
    if (!vkrt || !material || meshIndex >= vkrt->core.meshData.count) return -1;

    vkrt->core.meshes[meshIndex].material = *material;
    vkrt->core.materialDataDirty = VK_TRUE;
    resetSceneData(vkrt);
    return 0;
}

void VKRT_setRenderViewport(VKRT* vkrt, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    if (!vkrt || !vkrt->core.sceneData) return;

    uint32_t fullWidth = vkrt->runtime.swapChainExtent.width;
    uint32_t fullHeight = vkrt->runtime.swapChainExtent.height;

    if (width == 0 || height == 0 || fullWidth == 0 || fullHeight == 0) {
        x = 0;
        y = 0;
        width = fullWidth;
        height = fullHeight;
    }

    if (width <= 1 || height <= 1) {
        x = 0;
        y = 0;
        width = fullWidth;
        height = fullHeight;
    }

    if (x >= fullWidth) x = fullWidth - 1;
    if (y >= fullHeight) y = fullHeight - 1;
    if (x + width > fullWidth) width = fullWidth - x;
    if (y + height > fullHeight) height = fullHeight - y;

    uint32_t* rect = vkrt->core.sceneData->viewportRect;
    if (rect[0] == x && rect[1] == y && rect[2] == width && rect[3] == height &&
        vkrt->state.camera.width == width && vkrt->state.camera.height == height) {
        return;
    }

    rect[0] = x;
    rect[1] = y;
    rect[2] = width;
    rect[3] = height;

    vkrt->state.camera.width = width;
    vkrt->state.camera.height = height;
    updateMatricesFromCamera(vkrt);
}

void VKRT_cameraSetPose(VKRT* vkrt, vec3 position, vec3 target, vec3 up, float vfov) {
    if (!vkrt) return;

    if (position) glm_vec3_copy(position, vkrt->state.camera.pos);
    if (target) glm_vec3_copy(target, vkrt->state.camera.target);
    if (up) glm_vec3_copy(up, vkrt->state.camera.up);
    if (vfov > 0.0f) vkrt->state.camera.vfov = vfov;

    updateMatricesFromCamera(vkrt);
}

void VKRT_cameraGetPose(const VKRT* vkrt, vec3 position, vec3 target, vec3 up, float* vfov) {
    if (!vkrt) return;

    if (position) memcpy(position, vkrt->state.camera.pos, sizeof(vec3));
    if (target) memcpy(target, vkrt->state.camera.target, sizeof(vec3));
    if (up) memcpy(up, vkrt->state.camera.up, sizeof(vec3));
    if (vfov) *vfov = vkrt->state.camera.vfov;
}

void VKRT_framebufferResizedCallback(GLFWwindow* window, int width, int height) {
    (void)width;
    (void)height;

    VKRT* vkrt = (VKRT*)glfwGetWindowUserPointer(window);
    vkrt->runtime.framebufferResized = VK_TRUE;
}
