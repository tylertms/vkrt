#pragma once

#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "constants.h"
#include "formats.h"
#include "types.h"

enum {
    VKRT_DEVICE_NAME_LEN = 256,
    VKRT_NAME_LEN = 256,
};

typedef enum VKRT_Result {
    VKRT_SUCCESS = 0,
    VKRT_ERROR_INVALID_ARGUMENT = -1,
    VKRT_ERROR_OPERATION_FAILED = -2,
    VKRT_ERROR_OUT_OF_MEMORY = -3,
    VKRT_ERROR_DEVICE_LOST = -4,
    VKRT_ERROR_INITIALIZATION_FAILED = -5,
    VKRT_ERROR_SWAPCHAIN_OUT_OF_DATE = -6,
    VKRT_ERROR_PIPELINE_CREATION_FAILED = -7,
    VKRT_ERROR_SHADER_COMPILATION_FAILED = -8,
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

typedef uint32_t VKRT_ToneMappingMode;
typedef uint32_t VKRT_MaterialTextureSlot;
typedef uint32_t VKRT_TextureColorSpace;

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
    uint8_t startMaximized;
    uint8_t startFullscreen;
    uint8_t headless;
    uint8_t disableSER;
    int32_t preferredDeviceIndex;
    const char* preferredDeviceName;
} VKRT_CreateInfo;

typedef struct VKRT_MeshUpload {
    const Vertex* vertices;
    size_t vertexCount;
    const uint32_t* indices;
    size_t indexCount;
} VKRT_MeshUpload;

typedef struct VKRT_TextureUpload {
    const char* name;
    const void* pixels;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t colorSpace;
} VKRT_TextureUpload;

static inline Material VKRT_materialDefault(void) {
    return (Material){
        .baseColor = {0.8f, 0.8f, 0.8f},
        .roughness = 0.5f,
        .diffuseRoughness = 0.0f,
        .emissionColor = {1.0f, 1.0f, 1.0f},
        .emissionLuminance = 0.0f,
        .metallic = 0.0f,
        .specular = 0.5f,
        .specularTint = 0.0f,
        .anisotropic = 0.0f,
        .sheenTintWeight = {1.0f, 1.0f, 1.0f, 0.0f},
        .clearcoat = 0.0f,
        .clearcoatGloss = 1.0f,
        .ior = 1.5f,
        .eta = {0.0f, 0.0f, 0.0f},
        .k = {0.0f, 0.0f, 0.0f},
        .transmission = 0.0f,
        .subsurface = 0.0f,
        .sheenRoughness = 0.5f,
        .absorptionCoefficient = 0.0f,
        .attenuationColor = {1.0f, 1.0f, 1.0f},
        .normalTextureScale = 1.0f,
        .baseColorTextureIndex = VKRT_INVALID_INDEX,
        .metallicRoughnessTextureIndex = VKRT_INVALID_INDEX,
        .normalTextureIndex = VKRT_INVALID_INDEX,
        .emissiveTextureIndex = VKRT_INVALID_INDEX,
        .baseColorTextureWrap = 0,
        .metallicRoughnessTextureWrap = 0,
        .normalTextureWrap = 0,
        .emissiveTextureWrap = 0,
        .opacity = 1.0f,
        .alphaCutoff = 0.5f,
        .alphaMode = VKRT_MATERIAL_ALPHA_MODE_OPAQUE,
        .textureTexcoordSets = 0u,
        .baseColorTextureTransform = {1.0f, 1.0f, 0.0f, 0.0f},
        .metallicRoughnessTextureTransform = {1.0f, 1.0f, 0.0f, 0.0f},
        .normalTextureTransform = {1.0f, 1.0f, 0.0f, 0.0f},
        .emissiveTextureTransform = {1.0f, 1.0f, 0.0f, 0.0f},
        .textureRotations = {0.0f, 0.0f, 0.0f, 0.0f},
    };
}

struct VKRT;
typedef struct VKRT VKRT;

typedef struct VKRT_SceneSettingsSnapshot {
    Camera camera;
    uint32_t samplesPerPixel;
    uint32_t rrMaxDepth;
    uint32_t rrMinDepth;
    VKRT_ToneMappingMode toneMappingMode;
    float exposure;
    uint8_t autoExposureEnabled;
    uint8_t autoSPPEnabled;
    uint32_t autoSPPTargetFPS;
    vec3 environmentColor;
    float environmentStrength;
    uint32_t environmentTextureIndex;
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
    float frametimes[VKRT_FRAMETIME_HISTORY_SIZE];
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
    uint32_t presentMode;
    float displayRefreshHz;
} VKRT_RuntimeSnapshot;

typedef struct VKRT_SystemInfo {
    char deviceName[VKRT_DEVICE_NAME_LEN];
    uint32_t vendorID;
    uint32_t driverVersion;
} VKRT_SystemInfo;

typedef struct VKRT_MeshSnapshot {
    MeshInfo info;
    Material material;
    uint32_t materialIndex;
    uint32_t geometrySource;
    uint8_t hasMaterialAssignment;
    uint8_t ownsGeometry;
    char name[VKRT_NAME_LEN];
} VKRT_MeshSnapshot;

typedef struct VKRT_MaterialSnapshot {
    Material material;
    uint32_t useCount;
    char name[VKRT_NAME_LEN];
} VKRT_MaterialSnapshot;

typedef struct VKRT_TextureSnapshot {
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t colorSpace;
    uint32_t useCount;
    char name[VKRT_NAME_LEN];
} VKRT_TextureSnapshot;

#define VKRT_ARRAY_COUNT(x) ((sizeof(x) / sizeof(0 [x])) / ((size_t)(!(sizeof(x) % sizeof(0 [x])))))
