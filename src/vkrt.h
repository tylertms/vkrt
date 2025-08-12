#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "cglm.h"
#include "dcimgui.h"
#include "dcimgui_impl_glfw.h"
#include "dcimgui_impl_vulkan.h"
#include "dcimgui_internal.h"

#define WIDTH 1600
#define HEIGHT 900

#define MAX_FRAMES_IN_FLIGHT 2

typedef struct SceneData {
    mat4 viewInverse;
    mat4 projInverse;
    uint32_t frameNumber;
} SceneData;

typedef struct Camera {
    vec3 pos, target, up;
    uint32_t width, height;
    float nearZ, farZ, vfov;
} Camera;

typedef struct Vertex {
    vec4 position;
    vec4 normal;
} Vertex;

typedef struct AccelerationStructure {
    VkAccelerationStructureKHR structure;
    VkDeviceMemory memory;
    VkBuffer buffer;
    VkDeviceAddress deviceAddress;
    uint8_t needsRebuild;
} AccelerationStructure;

typedef struct MeshInfo {
    vec3 position;
    uint32_t vertexBase;
    vec3 rotation;
    uint32_t vertexCount;
    vec3 scale;
    uint32_t indexBase;
    uint32_t indexCount;
    uint32_t padding[3];
} MeshInfo;

typedef struct Mesh {
    MeshInfo info;
    AccelerationStructure bottomLevelAccelerationStructure;
} Mesh;

typedef struct Buffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
    VkDeviceAddress deviceAddress;
    uint32_t count;
} Buffer;

typedef struct Interface {
    void (*init)(void* vkrt);
    void (*deinit)(void* vkrt);
    void (*draw)(void* vkrt);
} Interface;

typedef struct QueueFamily {
    int32_t graphics;
    int32_t present;
} QueueFamily;

typedef struct VKRT {
    GLFWwindow* window;
    ImGuiContext* imguiContext;
    VkInstance instance;
    VkDebugUtilsMessengerEXT debugMessenger;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    char deviceName[256];
    VkQueue graphicsQueue;
    VkQueue presentQueue;
    VkQueryPool timestampPool;
    float timestampPeriod;
    QueueFamily indices;
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
    char *rgenPath, *rmissPath, *rchitPath;
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
    VkBuffer sceneDataBuffer;
    VkDeviceMemory sceneDataMemory;
    SceneData* sceneData;
    Camera camera;
    Interface gui;
    VkImage storageImage;
    VkImageView storageImageView;
    VkDeviceMemory storageImageMemory;
    Mesh* meshes;
    AccelerationStructure topLevelAccelerationStructure;
    Buffer vertexData;
    Buffer indexData;
    Buffer meshData;
    uint32_t framesPerSecond;
    uint64_t lastFrameTimestamp;
    uint8_t frametimeStartIndex;
    float averageFrametime;
    float frametimes[128];
    float displayTimeMs;
    float renderTimeMs;
    uint8_t vsync;
} VKRT;

int VKRT_init(VKRT* vkrt);
void VKRT_registerGUI(VKRT* vkrt, void (*init)(void*), void (*deinit)(void*), void (*draw)(void*));
void VKRT_deinit(VKRT* vkrt);
int VKRT_shouldDeinit(VKRT* vkrt);
void VKRT_poll(VKRT* vkrt);
void VKRT_draw(VKRT* vkrt);
void VKRT_addMesh(VKRT* vkrt, const char* path);
void VKRT_updateTLAS(VKRT* vkrt);
void VKRT_pollCameraMovement(VKRT* vkrt);
void VKRT_setDefaultStyle();
void VKRT_getImGuiVulkanInitInfo(VKRT* vkrt, ImGui_ImplVulkan_InitInfo* info);
static void VKRT_framebufferResizedCallback(GLFWwindow* window, int width, int height);

#define COUNT_OF(x) ((sizeof(x) / sizeof(0 [x])) / ((size_t)(!(sizeof(x) % sizeof(0 [x])))))

#ifdef __cplusplus
}
#endif
