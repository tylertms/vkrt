#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "command.h"
#include "interface.h"
#include "object.h"
#include "structure.h"
#include "vkrt.h"

int VKRT_init(VKRT *vkrt) {
    if (!vkrt) return -1;
    initWindow(vkrt);
    initVulkan(vkrt);
    return 0;
}

void VKRT_registerGUI(VKRT* vkrt, void (*init)(void*), void (*deinit)(void*), void (*draw)(void*)) {
    if (!vkrt) return;
    vkrt->gui.init = init;
    vkrt->gui.deinit = deinit;
    vkrt->gui.draw = draw;
}

void VKRT_deinit(VKRT *vkrt) {
    if (!vkrt) return;
    vkDeviceWaitIdle(vkrt->device);
    deinit(vkrt);
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

void VKRT_addMaterial(VKRT* vkrt, Material* material) {
    if (!vkrt || !material) return;
    addMaterial(vkrt, material);
}

void VKRT_updateTLAS(VKRT* vkrt) {
    if (!vkrt) return;
    createTopLevelAccelerationStructure(vkrt);
}

void VKRT_pollCameraMovement(VKRT* vkrt) {
    if (!vkrt) return;
    pollCameraMovement(vkrt);
}

void VKRT_setDarkTheme() {
    setDarkTheme();
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