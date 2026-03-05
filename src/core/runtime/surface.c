#include "surface.h"
#include "debug.h"

#include <stdio.h>
#include <stdlib.h>

VKRT_Result createSurface(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    if (glfwCreateWindowSurface(vkrt->core.instance, vkrt->runtime.window, NULL, &vkrt->runtime.surface) != VK_SUCCESS) {
        LOG_ERROR("Failed to create window surface");
        return VKRT_ERROR_OPERATION_FAILED;
    }
    return VKRT_SUCCESS;
}
