#include "window.h"

void initWindow(VKRT* vkrt) {
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    vkrt->window = glfwCreateWindow(WIDTH, HEIGHT, "VKRT", 0, 0);
}