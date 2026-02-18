#include "surface.h"

#include <stdio.h>
#include <stdlib.h>

void createSurface(VKRT* vkrt) {
    if (glfwCreateWindowSurface(vkrt->core.instance, vkrt->runtime.window, NULL, &vkrt->runtime.surface) != VK_SUCCESS) {
        perror("ERROR: Failed to create window surface");
        exit(EXIT_FAILURE);
    }
}
