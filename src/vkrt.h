#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "dcimgui.h"

#define WIDTH 800
#define HEIGHT 600

#define MAX_FRAMES_IN_FLIGHT 2

typedef struct VKRT {
    GLFWwindow* window;
    ImGuiContext* imguiContext;
    VkInstance instance;
    VkDebugUtilsMessengerEXT debugMessenger;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkQueue graphicsQueue;
    VkQueue presentQueue;
    VkSurfaceKHR surface;
    VkSwapchainKHR swapChain;
    VkImage* swapChainImages;
    VkImageView* swapChainImageViews;
    size_t swapChainImageCount;
    VkFormat swapChainImageFormat;
    VkExtent2D swapChainExtent;
    VkRenderPass renderPass;
    VkFramebuffer* framebuffers;
    VkDescriptorSetLayout descriptorSetLayout;
    VkDescriptorPool descriptorPool;
    VkDescriptorSet descriptorSet;
    VkPipelineLayout pipelineLayout;
    VkPipeline rayTracingPipeline;
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffers[MAX_FRAMES_IN_FLIGHT];
    VkSemaphore imageAvailableSemaphores[MAX_FRAMES_IN_FLIGHT];
    VkSemaphore renderFinishedSemaphores[MAX_FRAMES_IN_FLIGHT];
    VkFence inFlightFences[MAX_FRAMES_IN_FLIGHT];
    uint32_t currentFrame;
    VkBool32 framebufferResized;
    VkBuffer shaderBindingTableBuffer;
    VkDeviceMemory shaderBindingTableMemory;
    VkStridedDeviceAddressRegionKHR shaderBindingTables[4];
    VkBuffer uniformBuffer;
    VkDeviceMemory uniformBufferMemory;
    void* uniformBufferMapped;
    VkImage storageImage;
    VkImageView storageImageView;
    VkDeviceMemory storageImageMemory;
    VkAccelerationStructureKHR topLevelAccelerationStructure;
    VkDeviceMemory topLevelAccelerationStructureMemory;
    VkBuffer topLevelAccelerationStructureBuffer;
    VkDeviceAddress topLevelAccelerationStructureDeviceAddress;
    VkAccelerationStructureKHR bottomLevelAccelerationStructure;
    VkDeviceMemory bottomLevelAccelerationStructureMemory;
    VkBuffer bottomLevelAccelerationStructureBuffer;
    VkDeviceAddress bottomLevelAccelerationStructureDeviceAddress;
    VkBuffer vertexBuffer;
    VkDeviceMemory vertexBufferMemory;
    VkDeviceAddress vertexBufferDeviceAddress;
    uint32_t vertexCount;
    VkBuffer indexBuffer;
    VkDeviceMemory indexBufferMemory;
    VkDeviceAddress indexBufferDeviceAddress;
    uint32_t indexCount;
    uint32_t frameCount;
    uint32_t tempFrameCount;
    uint64_t previousTime;
    uint64_t currentTime;
    uint64_t lastFrameTimeReported;
} VKRT;

typedef struct SceneUniform {
    float view[4][4];
} SceneUniform;

#define COUNT_OF(x) ((sizeof(x) / sizeof(0 [x])) / ((size_t)(!(sizeof(x) % sizeof(0 [x])))))