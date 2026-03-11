#pragma once

#include <stddef.h>
#include <stdint.h>

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include "config.h"
#include "constants.h"
#include "types.h"

enum {
    VKRT_DEVICE_NAME_LEN = 256,
    VKRT_NAME_LEN = 256,
};

typedef enum VKRT_Result {
    VKRT_SUCCESS = 0,
    VKRT_ERROR_INVALID_ARGUMENT = -1,
    VKRT_ERROR_OPERATION_FAILED = -2,
} VKRT_Result;

typedef struct VKRT_SceneTimelineKeyframe {
    float time;
    float emissionScale;
    vec3 emissionTint;
} VKRT_SceneTimelineKeyframe;

typedef struct VKRT_SceneTimelineSettings {
    uint8_t enabled;
    uint32_t keyframeCount;
    VKRT_SceneTimelineKeyframe keyframes[VKRT_SCENE_TIMELINE_MAX_KEYFRAMES];
} VKRT_SceneTimelineSettings;

static inline int vkrtCompareSceneTimelineKeyframesByTime(const void* lhs, const void* rhs) {
    const VKRT_SceneTimelineKeyframe* a = (const VKRT_SceneTimelineKeyframe*)lhs;
    const VKRT_SceneTimelineKeyframe* b = (const VKRT_SceneTimelineKeyframe*)rhs;
    if (a->time < b->time) return -1;
    if (a->time > b->time) return 1;
    return 0;
}

typedef enum VKRT_ToneMappingMode {
    VKRT_TONE_MAPPING_NONE = VKRT_TONE_MAPPING_MODE_NONE,
    VKRT_TONE_MAPPING_ACES = VKRT_TONE_MAPPING_MODE_ACES,
} VKRT_ToneMappingMode;

typedef enum VKRT_PresentModePreference {
    VKRT_PRESENT_MODE_ADAPTIVE = 0,
    VKRT_PRESENT_MODE_VSYNC = 1,
    VKRT_PRESENT_MODE_MAILBOX = 2,
    VKRT_PRESENT_MODE_IMMEDIATE = 3,
} VKRT_PresentModePreference;

typedef struct Camera {
    vec3 pos, target, up;
    float nearZ, farZ, vfov;
} Camera;

typedef struct VKRT_CameraInput {
    float orbitDx;
    float orbitDy;
    float panDx;
    float panDy;
    float scroll;
    uint8_t orbiting;
    uint8_t panning;
    uint8_t captureMouse;
} VKRT_CameraInput;

typedef struct VKRT_CreateInfo {
    uint32_t width;
    uint32_t height;
    const char* title;
    VKRT_PresentModePreference presentModePreference;
    uint8_t startMaximized;
    uint8_t startFullscreen;
    int32_t preferredDeviceIndex;
    const char* preferredDeviceName;
} VKRT_CreateInfo;

static inline Material VKRT_materialDefault(void) {
    return (Material){
        .baseColor = {0.8f, 0.8f, 0.8f},
        .roughness = 0.5f,
        .emissionColor = {1.0f, 1.0f, 1.0f},
        .emissionLuminance = 0.0f,
        .metallic = 0.0f,
        .specular = 0.5f,
        .specularTint = 0.0f,
        .anisotropic = 0.0f,
        .sheen = 0.0f,
        .sheenTint = 0.5f,
        .clearcoat = 0.0f,
        .clearcoatGloss = 1.0f,
    };
}

struct VKRT;
typedef struct VKRT VKRT;

typedef struct VKRT_AppHooks {
    void (*init)(struct VKRT* vkrt, void* userData);
    void (*deinit)(struct VKRT* vkrt, void* userData);
    void (*drawOverlay)(struct VKRT* vkrt, VkCommandBuffer commandBuffer, void* userData);
    void* userData;
} VKRT_AppHooks;

typedef struct VKRT_SceneSettingsSnapshot {
    Camera camera;
    uint32_t samplesPerPixel;
    uint32_t rrMaxDepth;
    uint32_t rrMinDepth;
    VKRT_ToneMappingMode toneMappingMode;
    uint8_t autoSPPEnabled;
    uint32_t autoSPPTargetFPS;
    uint32_t renderModeTargetFPS;
    float fogDensity;
    float timeBase;
    float timeStep;
    uint32_t debugMode;
    uint32_t misNeeEnabled;
    uint32_t selectionEnabled;
    uint32_t selectedMeshIndex;
    VKRT_SceneTimelineSettings sceneTimeline;
} VKRT_SceneSettingsSnapshot;

typedef struct VKRT_RenderStatusSnapshot {
    uint32_t framesPerSecond;
    float averageFrametime;
    float frametimes[128];
    float displayTimeMs;
    float renderTimeMs;
    uint32_t accumulationFrame;
    uint64_t totalSamples;
    uint8_t renderModeActive;
    uint8_t renderModeFinished;
    uint32_t renderTargetSamples;
    float displayRenderTimeMs;
    float displayFrameTimeMs;
} VKRT_RenderStatusSnapshot;

typedef struct VKRT_RuntimeSnapshot {
    uint32_t displayWidth;
    uint32_t displayHeight;
    uint32_t swapchainWidth;
    uint32_t swapchainHeight;
    uint32_t renderWidth;
    uint32_t renderHeight;
    uint32_t displayViewportRect[4];
    uint8_t vsync;
    uint8_t savedVsync;
    VKRT_PresentModePreference presentModePreference;
    VKRT_PresentModePreference savedPresentModePreference;
    VkPresentModeKHR presentMode;
    float displayRefreshHz;
} VKRT_RuntimeSnapshot;

typedef struct VKRT_SystemInfo {
    char deviceName[VKRT_DEVICE_NAME_LEN];
    uint32_t vendorID;
    uint32_t driverVersion;
} VKRT_SystemInfo;

typedef struct VKRT_OverlayInfo {
    GLFWwindow* window;
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    uint32_t graphicsQueueFamily;
    VkQueue graphicsQueue;
    VkDescriptorPool descriptorPool;
    VkRenderPass renderPass;
    uint32_t swapchainImageCount;
    uint32_t swapchainMinImageCount;
} VKRT_OverlayInfo;

typedef struct VKRT_MeshSnapshot {
    MeshInfo info;
    Material material;
    uint32_t geometrySource;
    uint8_t ownsGeometry;
    char name[VKRT_NAME_LEN];
} VKRT_MeshSnapshot;

#define VKRT_ARRAY_COUNT(x) ((sizeof(x) / sizeof(0 [x])) / ((size_t)(!(sizeof(x) % sizeof(0 [x])))))
