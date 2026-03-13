#ifndef VKRT_CORE_RESOURCES_SLANG
#define VKRT_CORE_RESOURCES_SLANG

#include "constants.slang"
#include "../../shared/types.h"

[[vk::binding(0, 0)]]
RaytracingAccelerationStructure topLevelAS;

[[vk::binding(1, 0)]]
[vk::image_format("rgba32f")]
RWTexture2D<float4> accumulationReadImage;

[[vk::binding(2, 0)]]
[vk::image_format("rgba32f")]
RWTexture2D<float4> accumulationWriteImage;

[[vk::binding(3, 0)]]
[vk::image_format("rgba16")]
RWTexture2D<float4> outputImage;

[[vk::binding(4, 0)]]
[vk::image_format("r32ui")]
RWTexture2D<uint> selectionMaskImage;

[[vk::binding(5, 0)]]
StructuredBuffer<Vertex> vertexBuffer;

[[vk::binding(6, 0)]]
StructuredBuffer<uint> indexBuffer;

[[vk::binding(7, 0)]]
ConstantBuffer<SceneData> scene;

[[vk::binding(8, 0)]]
RWStructuredBuffer<PickBuffer> pickBuffer;

[[vk::binding(9, 0)]]
StructuredBuffer<MeshInfo> meshInfo;

[[vk::binding(10, 0)]]
StructuredBuffer<Material> materialBuffer;

#endif
