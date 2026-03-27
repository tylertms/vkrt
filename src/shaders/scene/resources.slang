#ifndef VKRT_CORE_RESOURCES_SLANG
#define VKRT_CORE_RESOURCES_SLANG

#include "../../shared/types.h"
#include "constants.slang"

[[vk::binding(0, 0)]] RaytracingAccelerationStructure topLevelAS;
[[vk::binding(1, 0)]] RaytracingAccelerationStructure selectionTopLevelAS;

[[vk::binding(2, 0)]][vk::image_format("rgba32f")] RWTexture2D<float4> accumulationReadImage;
[[vk::binding(3, 0)]][vk::image_format("rgba32f")] RWTexture2D<float4> accumulationWriteImage;
[[vk::binding(4, 0)]][vk::image_format("rgba16")] RWTexture2D<float4> outputImage;
[[vk::binding(5, 0)]][vk::image_format("r32ui")] RWTexture2D<uint> selectionMaskImage;
[[vk::binding(6, 0)]][vk::image_format("rgba16f")] RWTexture2D<float4> albedoReadImage;
[[vk::binding(7, 0)]][vk::image_format("rgba16f")] RWTexture2D<float4> albedoWriteImage;
[[vk::binding(8, 0)]][vk::image_format("rgba16f")] RWTexture2D<float4> normalReadImage;
[[vk::binding(9, 0)]][vk::image_format("rgba16f")] RWTexture2D<float4> normalWriteImage;

[[vk::binding(10, 0)]] StructuredBuffer<Vertex> vertices;
[[vk::binding(11, 0)]] StructuredBuffer<uint> indices;
[[vk::binding(12, 0)]] ConstantBuffer<SceneData> scene;
[[vk::binding(13, 0)]] RWStructuredBuffer<Selection> selection;
[[vk::binding(14, 0)]] StructuredBuffer<MeshInfo> meshInfos;
[[vk::binding(15, 0)]] StructuredBuffer<Material> materials;

[[vk::binding(16, 0)]] StructuredBuffer<EmissiveMesh> emissiveMeshes;
[[vk::binding(17, 0)]] StructuredBuffer<EmissiveTriangle> emissiveTriangles;
[[vk::binding(18, 0)]] StructuredBuffer<float> meshAliasQ;
[[vk::binding(19, 0)]] StructuredBuffer<uint> meshAliasIdx;
[[vk::binding(20, 0)]] StructuredBuffer<float> triAliasQ;
[[vk::binding(21, 0)]] StructuredBuffer<uint> triAliasIdx;
[[vk::binding(22, 0)]] SamplerState textureSamplers[VKRT_TEXTURE_SAMPLER_VARIANT_COUNT];
[[vk::binding(23, 0)]] Texture2D<float4> sceneTextures[VKRT_MAX_BINDLESS_TEXTURES];

#endif
