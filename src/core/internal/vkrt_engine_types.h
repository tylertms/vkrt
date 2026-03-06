#pragma once

#include "vkrt_types.h"

typedef struct AccelerationStructure {
    VkAccelerationStructureKHR structure;
    VkDeviceMemory memory;
    VkBuffer buffer;
    VkDeviceAddress deviceAddress;
} AccelerationStructure;

typedef struct Mesh {
    MeshInfo info;
    Material material;
    AccelerationStructure bottomLevelAccelerationStructure;
    Vertex* vertices;
    uint32_t* indices;
    uint32_t geometrySource;
    uint8_t ownsGeometry;
    int8_t renderBackfacesOverride;
    uint8_t geometryUploadPending;
    uint8_t blasBuildPending;
} Mesh;

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
