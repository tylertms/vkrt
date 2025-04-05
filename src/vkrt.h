#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define WIDTH 800
#define HEIGHT 600

typedef struct VKRT {
    GLFWwindow* window;
    VkInstance instance;
} VKRT;

#include "cleanup.h"
#include "instance.h"
#include "vulkan.h"
#include "window.h"