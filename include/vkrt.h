#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "cglm.h"
#include "dcimgui.h"

#define WIDTH 800
#define HEIGHT 600

#define MAX_FRAMES_IN_FLIGHT 2

typedef struct SceneData {
    mat4 viewInverse;
    mat4 projInverse;
    uint32_t frame;
} SceneData;

typedef struct Camera {
    vec3 pos, target, up;
    uint32_t width, height;
    float nearZ, farZ, vfov;
} Camera;

typedef struct Material {
    vec3 color;
    float roughness;
    vec3 emissiveColor;
    float emissiveIntensity;
} Material;

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
    uint32_t materialIndex;
    uint64_t padding;
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
    VkImage storageImage;
    VkImageView storageImageView;
    VkDeviceMemory storageImageMemory;
    Mesh* meshes;
    Material* materials;
    AccelerationStructure topLevelAccelerationStructure;
    Buffer vertexData;
    Buffer indexData;
    Buffer meshData;
    Buffer materialData;
    uint32_t tempFrameCount;
    uint64_t previousTime;
    uint64_t currentTime;
    uint64_t lastFrameTimeReported;
    uint32_t averageFPS;
    float averageFrametime;
    float frameTimes[128];
    uint8_t frameTimeStartIndex;
    uint8_t vsync;
} VKRT;

typedef struct Vertex {
    float position[4];
    float normal[4];
} Vertex;

int VKRT_init(VKRT* vkrt);
void VKRT_deinit(VKRT* vkrt);
int VKRT_shouldDeinit(VKRT* vkrt);
void VKRT_poll(VKRT* vkrt);
void VKRT_draw(VKRT* vkrt);
void VKRT_addMesh(VKRT* vkrt, const char* path);
void VKRT_addMaterial(VKRT* vkrt, Material* material);
void VKRT_updateTLAS(VKRT* vkrt);

#define COUNT_OF(x) ((sizeof(x) / sizeof(0 [x])) / ((size_t)(!(sizeof(x) % sizeof(0 [x])))))

#ifdef __cplusplus
}
#endif
