#pragma once

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
    char name[VKRT_NAME_LEN];
    AccelerationStructure bottomLevelAccelerationStructure;
    Vertex* vertices;
    uint32_t* indices;
    uint32_t geometrySource;
    uint8_t ownsGeometry;
    int8_t renderBackfacesOverride;
    uint8_t geometryUploadPending;
    uint8_t blasBuildPending;
} Mesh;

typedef struct SceneMaterial {
    Material material;
    char name[VKRT_NAME_LEN];
} SceneMaterial;

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

struct PNGEncodeJob;

typedef struct PNGExporter {
    VKRT_Mutex stateLock;
    VKRT_Mutex workerLock;
    VKRT_Cond workerCondition;
    VKRT_Thread workerThread;
    struct PNGEncodeJob* head;
    struct PNGEncodeJob* tail;
    uint32_t pendingJobCount;
    int stop;
    int primitivesInitialized;
    int threadRunning;
    int stateLockInitialized;
} PNGExporter;
