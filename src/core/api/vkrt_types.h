#pragma once

#include <stddef.h>
#include <stdint.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "config.h"
#include "types.h"

/*
 * Result contract for all VKRT_* functions that return VKRT_Result:
 * - VKRT_SUCCESS: operation completed; output pointers are valid when provided.
 * - VKRT_ERROR_INVALID_ARGUMENT: invalid input; operation has no side effects.
 * - VKRT_ERROR_OPERATION_FAILED: runtime/backend failure; state may be partially updated.
 */
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

typedef enum VKRT_ToneMappingMode {
    VKRT_TONE_MAPPING_NONE = VKRT_TONE_MAPPING_MODE_NONE,
    VKRT_TONE_MAPPING_ACES = VKRT_TONE_MAPPING_MODE_ACES,
} VKRT_ToneMappingMode;

typedef struct Camera {
    vec3 pos, target, up;
    uint32_t width, height;
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

typedef struct VKRT_ShaderConfig {
    const char* rgenPath;
    const char* rmissPath;
    const char* rchitPath;
} VKRT_ShaderConfig;

typedef struct VKRT_CreateInfo {
    uint32_t width;
    uint32_t height;
    const char* title;
    uint8_t vsync;
    VKRT_ShaderConfig shaders;
} VKRT_CreateInfo;

static inline Material VKRT_materialDefault(void) {
    return (Material){
        .baseColor = {0.8f, 0.8f, 0.8f},
        .metallic = 0.0f,
        .roughness = 0.3f,
        .specular = 0.5f,
        .specularTint = 0.0f,
        .anisotropic = 0.0f,
        .sheen = 0.0f,
        .sheenTint = 0.5f,
        .clearcoat = 0.0f,
        .clearcoatGloss = 1.0f,
        .subsurface = 0.0f,
        .transmission = 0.0f,
        .ior = 1.5f,
        .emissionColor = {1.0f, 1.0f, 1.0f},
        .emissionLuminance = 0.0f,
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

typedef struct VKRT_PublicState {
    Camera camera;
    uint32_t framesPerSecond;
    uint64_t lastFrameTimestamp;
    uint8_t frametimeStartIndex;
    float averageFrametime;
    float frametimes[128];
    float displayTimeMs;
    float renderTimeMs;
    uint32_t accumulationFrame;
    uint32_t samplesPerPixel;
    uint64_t totalSamples;
    uint8_t renderModeActive;
    uint8_t renderModeFinished;
    uint32_t renderTargetSamples;
    float renderViewZoom;
    float renderViewPanX;
    float renderViewPanY;
    float displayRenderTimeMs;
    float displayFrameTimeMs;
    uint32_t rrMaxDepth;
    uint32_t rrMinDepth;
    VKRT_ToneMappingMode toneMappingMode;
    uint8_t autoSPPEnabled;
    uint32_t autoSPPTargetFPS;
    float autoSPPTargetFrameMs;
    float autoSPPControlMs;
    uint32_t autoSPPFramesUntilNextAdjust;
    float fogDensity;
    float timeBase;
    float timeStep;
    uint32_t debugMode;
    uint32_t misNeeEnabled;
    uint32_t selectionEnabled;
    uint32_t selectedMeshIndex;
    VKRT_SceneTimelineSettings sceneTimeline;
} VKRT_PublicState;

typedef struct VKRT_RuntimeSnapshot {
    uint32_t swapchainWidth;
    uint32_t swapchainHeight;
    uint32_t renderWidth;
    uint32_t renderHeight;
    uint32_t displayViewportRect[4];
    uint8_t vsync;
    uint8_t savedVsync;
    float displayRefreshHz;
} VKRT_RuntimeSnapshot;

typedef struct VKRT_SystemInfo {
    char deviceName[256];
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
} VKRT_MeshSnapshot;

#define VKRT_ARRAY_COUNT(x) ((sizeof(x) / sizeof(0 [x])) / ((size_t)(!(sizeof(x) % sizeof(0 [x])))))
