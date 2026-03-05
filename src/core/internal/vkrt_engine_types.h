#pragma once

#include "vkrt_types.h"

typedef struct SceneData {
    mat4 viewInverse;
    mat4 projInverse;
    uint32_t frameNumber;
    uint32_t samplesPerPixel;
    uint32_t rrMaxDepth;
    uint32_t rrMinDepth;
    uint32_t viewportRect[4];
    uint32_t toneMappingMode;
    float timeBase;
    float timeStep;
    float fogDensity;
    uint32_t debugMode;
    uint32_t misNeeEnabled;
    uint32_t timelineEnabled;
    uint32_t timelineKeyframeCount;
    uint32_t emissiveMeshCount;
    uint32_t emissiveTriangleCount;
    uint32_t selectionEnabled;
    uint32_t selectedMeshIndex;
    vec4 timelineTimeScale[VKRT_SCENE_TIMELINE_MAX_KEYFRAMES];
    vec4 timelineTint[VKRT_SCENE_TIMELINE_MAX_KEYFRAMES];
} SceneData;

typedef struct AccelerationStructure {
    VkAccelerationStructureKHR structure;
    VkDeviceMemory memory;
    VkBuffer buffer;
    VkDeviceAddress deviceAddress;
    uint8_t needsRebuild;
} AccelerationStructure;

typedef struct Mesh {
    MeshInfo info;
    MaterialData material;
    AccelerationStructure bottomLevelAccelerationStructure;
    Vertex* vertices;
    uint32_t* indices;
    uint32_t geometrySource;
    uint8_t ownsGeometry;
} Mesh;

typedef struct PickBuffer {
    uint32_t pixel; // (x | (y << 16))
    uint32_t requestID;
    uint32_t hitMeshIndex;
    uint32_t resultID;
} PickBuffer;

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
