#ifndef VKRT_CORE_RESOURCES_SLANG
#define VKRT_CORE_RESOURCES_SLANG

#include "constants.slang"
#include "../../shared/types.h"

[[vk::binding(0, 0)]] RaytracingAccelerationStructure topLevelAS;
[[vk::binding(1, 0)]] RaytracingAccelerationStructure selectionTopLevelAS;

[[vk::binding(2, 0)]] [vk::image_format("rgba32f")] RWTexture2D<float4> accumulationReadImage;
[[vk::binding(3, 0)]] [vk::image_format("rgba32f")] RWTexture2D<float4> accumulationWriteImage;
[[vk::binding(4, 0)]] [vk::image_format("rgba16")] RWTexture2D<float4> outputImage;
[[vk::binding(5, 0)]] [vk::image_format("r32ui")] RWTexture2D<uint> selectionMaskImage;

[[vk::binding(6, 0)]] StructuredBuffer<Vertex> vertices;
[[vk::binding(7, 0)]] StructuredBuffer<uint> indices;
[[vk::binding(8, 0)]] ConstantBuffer<SceneData> scene;
[[vk::binding(9, 0)]] RWStructuredBuffer<Selection> selection;
[[vk::binding(10, 0)]] StructuredBuffer<MeshInfo> meshInfos;
[[vk::binding(11, 0)]] StructuredBuffer<Material> materials;

[[vk::binding(12, 0)]] StructuredBuffer<EmissiveMesh> emissiveMeshes;
[[vk::binding(13, 0)]] StructuredBuffer<EmissiveTriangle> emissiveTriangles;
[[vk::binding(14, 0)]] StructuredBuffer<float> meshAliasQ;
[[vk::binding(15, 0)]] StructuredBuffer<uint> meshAliasIdx;
[[vk::binding(16, 0)]] StructuredBuffer<float> triAliasQ;
[[vk::binding(17, 0)]] StructuredBuffer<uint> triAliasIdx;
[[vk::binding(18, 0)]] SamplerState textureSamplers[VKRT_TEXTURE_SAMPLER_VARIANT_COUNT];
[[vk::binding(19, 0)]] Texture2D<float4> sceneTextures[VKRT_MAX_BINDLESS_TEXTURES];

#endif
