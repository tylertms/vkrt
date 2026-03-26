#pragma once

#include <stddef.h>

#include <vulkan/vulkan.h>

#include "vkrt_types.h"
#include "platform.h"

typedef struct AccelerationStructure {
    VkAccelerationStructureKHR structure;
    VkDeviceMemory memory;
    VkBuffer buffer;
    VkDeviceAddress deviceAddress;
} AccelerationStructure;

typedef struct Mesh {
    MeshInfo info;
    mat4 worldTransform;
    char name[VKRT_NAME_LEN];
    AccelerationStructure bottomLevelAccelerationStructure;
    Vertex* vertices;
    uint32_t* indices;
    uint64_t geometryFingerprint;
    uint32_t geometrySource;
    uint8_t hasMaterialAssignment;
    uint8_t ownsGeometry;
    int8_t renderBackfacesOverride;
    uint8_t geometryUploadPending;
    uint8_t blasBuildPending;
} Mesh;

typedef struct SceneMaterial {
    Material material;
    char name[VKRT_NAME_LEN];
} SceneMaterial;

typedef struct SceneTexture {
    VkImage image;
    VkImageView view;
    VkDeviceMemory memory;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t colorSpace;
    uint32_t useCount;
    char name[VKRT_NAME_LEN];
} SceneTexture;

typedef struct GeometryLayout {
    uint32_t vertexCapacity;
    uint32_t indexCapacity;
} GeometryLayout;

typedef struct Buffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
    VkDeviceAddress deviceAddress;
    uint32_t count;
} Buffer;

typedef struct QueueFamily {
    int32_t graphics;
    int32_t present;
} QueueFamily;

struct RenderImageExportJob;

typedef struct RenderImageExporter {
    VKRT_Mutex stateLock;
    VKRT_Mutex workerLock;
    VKRT_Cond workerCondition;
    VKRT_Thread workerThread;
    struct RenderImageExportJob* head;
    struct RenderImageExportJob* tail;
    uint32_t pendingJobCount;
    void* completedViewportPixels;
    size_t completedViewportByteCount;
    uint32_t completedViewportWidth;
    uint32_t completedViewportHeight;
    uint64_t completedViewportRenderSequence;
    int completedViewportReady;
    int completedViewportSucceeded;
    int stop;
    int primitivesInitialized;
    int threadRunning;
    int stateLockInitialized;
} RenderImageExporter;
