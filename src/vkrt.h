#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define WIDTH 800
#define HEIGHT 600

typedef struct VKRT {
    GLFWwindow* window;
    VkInstance instance;
    VkDebugUtilsMessengerEXT debugMessenger;
} VKRT;

#define ARRLEN(arr) sizeof((void *)arr) / sizeof(arr[0])