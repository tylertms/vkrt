#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "command.h"
#include "object.h"
#include "vkrt.h"

int VKRT_init(VKRT *vkrt) {
    if (!vkrt) return -1;
    initWindow(vkrt);
    initVulkan(vkrt);
    return 0;
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
