#include <stdlib.h>
#include <string.h>

#include "buffer.h"
#include "command.h"
#include "descriptor.h"
#include "device.h"
#include "instance.h"
#include "object.h"
#include "pipeline.h"
#include "scene.h"
#include "structure.h"
#include "surface.h"
#include "swapchain.h"
#include "validation.h"
#include "vkrt.h"

static void rebuildGeometryBuffers(VKRT* vkrt) {
    if (vkrt->meshData.count == 0) {
        if (vkrt->vertexData.buffer) {
            vkDestroyBuffer(vkrt->device, vkrt->vertexData.buffer, NULL);
            vkFreeMemory(vkrt->device, vkrt->vertexData.memory, NULL);
            vkrt->vertexData.buffer = VK_NULL_HANDLE;
            vkrt->vertexData.memory = VK_NULL_HANDLE;
        }
        if (vkrt->indexData.buffer) {
            vkDestroyBuffer(vkrt->device, vkrt->indexData.buffer, NULL);
            vkFreeMemory(vkrt->device, vkrt->indexData.memory, NULL);
            vkrt->indexData.buffer = VK_NULL_HANDLE;
            vkrt->indexData.memory = VK_NULL_HANDLE;
        }
        vkrt->vertexData.count = 0;
        vkrt->indexData.count = 0;
        return;
    }

    uint32_t vBase = 0, iBase = 0;
    MeshInfo* meshInfos = (MeshInfo*)vkrt->meshData.host;
    for (uint32_t i = 0; i < vkrt->meshData.count; i++) {
        meshInfos[i].vertexBase = vBase;
        meshInfos[i].indexBase = iBase;
        vBase += meshInfos[i].vertexCount;
        iBase += meshInfos[i].indexCount;
    }

    if (vkrt->vertexData.buffer) {
        vkDestroyBuffer(vkrt->device, vkrt->vertexData.buffer, NULL);
        vkFreeMemory(vkrt->device, vkrt->vertexData.memory, NULL);
    }
    if (vkrt->indexData.buffer) {
        vkDestroyBuffer(vkrt->device, vkrt->indexData.buffer, NULL);
        vkFreeMemory(vkrt->device, vkrt->indexData.memory, NULL);
    }

    vkrt->vertexData.count = vBase;
    vkrt->indexData.count = iBase;

    if (vkrt->vertexData.count > 0) {
        vkrt->vertexData.deviceAddress = createBufferFromHostData(vkrt, vkrt->vertexData.host,
                                                                  (VkDeviceSize)vkrt->vertexData.count * vkrt->vertexData.stride,
                                                                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                                  &vkrt->vertexData.buffer, &vkrt->vertexData.memory);
    }
    if (vkrt->indexData.count > 0) {
        vkrt->indexData.deviceAddress = createBufferFromHostData(vkrt, vkrt->indexData.host,
                                                                 (VkDeviceSize)vkrt->indexData.count * vkrt->indexData.stride,
                                                                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                                 &vkrt->indexData.buffer, &vkrt->indexData.memory);
    }
}

int VKRT_init(VKRT* vkrt) {
    if (!vkrt)
        return -1;

    vkrt->vertexData.stride = sizeof(Vertex);
    vkrt->indexData.stride = sizeof(uint32_t);
    vkrt->meshData.stride = sizeof(MeshInfo);
    vkrt->vertexData.host = NULL;
    vkrt->indexData.host = NULL;
    vkrt->meshData.host = NULL;
    vkrt->vertexData.count = 0;
    vkrt->indexData.count = 0;
    vkrt->meshData.count = 0;

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

void VKRT_registerGUI(VKRT* vkrt, void (*init)(void*), void (*deinit)(void*), void (*draw)(void*)) {
    if (!vkrt)
        return;
    vkrt->gui.init = init;
    vkrt->gui.deinit = deinit;
    vkrt->gui.draw = draw;
}

void VKRT_deinit(VKRT* vkrt) {
    if (!vkrt)
        return;

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
    free(vkrt->vertexData.host);
    free(vkrt->indexData.host);
    free(vkrt->meshData.host);
    free(vkrt->meshes);

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
    if (!vkrt)
        return;
    glfwPollEvents();
}

void VKRT_draw(VKRT* vkrt) {
    if (vkrt)
        drawFrame(vkrt);
}

void VKRT_addMesh(VKRT* vkrt, const char* path) {
    if (!vkrt || !path)
        return;
    uint32_t meshIndex = vkrt->meshData.count;
    loadObject(vkrt, path);
    rebuildGeometryBuffers(vkrt);
    createBottomLevelAccelerationStructure(vkrt, meshIndex);
    VKRT_updateTLAS(vkrt);
    updateDescriptorSet(vkrt);
}

void VKRT_removeMesh(VKRT* vkrt, const char* name) {
    if (!vkrt || !name)
        return;
    uint32_t index = UINT32_MAX;
    for (uint32_t i = 0; i < vkrt->meshData.count; i++) {
        if (strcmp(vkrt->meshes[i].name, name) == 0) {
            index = i;
            break;
        }
    }
    if (index == UINT32_MAX)
        return;

    PFN_vkDestroyAccelerationStructureKHR pDestroy = (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(vkrt->device, "vkDestroyAccelerationStructureKHR");
    Mesh* m = &vkrt->meshes[index];
    MeshInfo* info = &((MeshInfo*)vkrt->meshData.host)[index];

    size_t vOffset = (size_t)info->vertexBase * vkrt->vertexData.stride;
    size_t vSize = (size_t)info->vertexCount * vkrt->vertexData.stride;
    size_t vRemain = (size_t)vkrt->vertexData.count * vkrt->vertexData.stride - (vOffset + vSize);
    if (vRemain > 0) {
        memmove((uint8_t*)vkrt->vertexData.host + vOffset,
                (uint8_t*)vkrt->vertexData.host + vOffset + vSize,
                vRemain);
    }
    vkrt->vertexData.count -= info->vertexCount;
    if (vkrt->vertexData.count == 0) {
        free(vkrt->vertexData.host);
        vkrt->vertexData.host = NULL;
    } else {
        vkrt->vertexData.host = realloc(vkrt->vertexData.host, (size_t)vkrt->vertexData.count * vkrt->vertexData.stride);
    }

    size_t iOffset = (size_t)info->indexBase * vkrt->indexData.stride;
    size_t iSize = (size_t)info->indexCount * vkrt->indexData.stride;
    size_t iRemain = (size_t)vkrt->indexData.count * vkrt->indexData.stride - (iOffset + iSize);
    if (iRemain > 0) {
        memmove((uint8_t*)vkrt->indexData.host + iOffset,
                (uint8_t*)vkrt->indexData.host + iOffset + iSize,
                iRemain);
    }
    vkrt->indexData.count -= info->indexCount;
    if (vkrt->indexData.count == 0) {
        free(vkrt->indexData.host);
        vkrt->indexData.host = NULL;
    } else {
        vkrt->indexData.host = realloc(vkrt->indexData.host, (size_t)vkrt->indexData.count * vkrt->indexData.stride);
    }

    size_t meshRemain = vkrt->meshData.count - index - 1;
    if (meshRemain > 0) {
        memmove((MeshInfo*)vkrt->meshData.host + index,
                (MeshInfo*)vkrt->meshData.host + index + 1,
                meshRemain * vkrt->meshData.stride);
    }
    vkrt->meshData.count--;
    if (vkrt->meshData.count == 0) {
        free(vkrt->meshData.host);
        vkrt->meshData.host = NULL;
    } else {
        vkrt->meshData.host = realloc(vkrt->meshData.host, vkrt->meshData.count * vkrt->meshData.stride);
    }

    pDestroy(vkrt->device, m->bottomLevelAccelerationStructure.structure, NULL);
    vkDestroyBuffer(vkrt->device, m->bottomLevelAccelerationStructure.buffer, NULL);
    vkFreeMemory(vkrt->device, m->bottomLevelAccelerationStructure.memory, NULL);

    for (uint32_t i = index + 1; i < vkrt->meshData.count + 1; i++) {
        vkrt->meshes[i - 1] = vkrt->meshes[i];
    }
    if (vkrt->meshData.count == 0) {
        free(vkrt->meshes);
        vkrt->meshes = NULL;
    } else {
        vkrt->meshes = realloc(vkrt->meshes, vkrt->meshData.count * sizeof(Mesh));
    }

    rebuildGeometryBuffers(vkrt);
    VKRT_updateTLAS(vkrt);
    updateDescriptorSet(vkrt);
}

void VKRT_updateTLAS(VKRT* vkrt) {
    if (!vkrt)
        return;
    createTopLevelAccelerationStructure(vkrt);
}

void VKRT_pollCameraMovement(VKRT* vkrt) {
    if (!vkrt)
        return;
    pollCameraMovement(vkrt);
}

void VKRT_setDefaultStyle() {
    setDefaultStyle();
}

void VKRT_getImGuiVulkanInitInfo(VKRT* vkrt, ImGui_ImplVulkan_InitInfo* info) {
    if (!info)
        return;

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