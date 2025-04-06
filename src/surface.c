#include "surface.h"
#include <stdio.h>
#include <stdlib.h>

void createSurface(VKRT* vkrt) {
    if (glfwCreateWindowSurface(vkrt->instance, vkrt->window, NULL, &vkrt->surface) != VK_SUCCESS) {
        printf("ERROR: Failed to create window surface!\n");
        exit(EXIT_FAILURE);
    }
}