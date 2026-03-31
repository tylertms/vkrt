#pragma once

#include "config.h"
#include "debug.h"
#include "pipeline.h"
#include "shaders.h"
#include "sync.h"
#include "vkrt_types.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct RayTracingShaderVariant {
    const uint32_t* rayGenData[VKRT_MAIN_RAYGEN_GROUP_COUNT];
    const uint32_t* closestHitData;
    const uint32_t* anyHitData;
    const uint32_t* missData;
    const uint32_t* shadowClosestHitData;
    const uint32_t* shadowAnyHitData;
    const uint32_t* shadowMissData;
    size_t rayGenSize[VKRT_MAIN_RAYGEN_GROUP_COUNT];
    size_t closestHitSize;
    size_t anyHitSize;
    size_t missSize;
    size_t shadowClosestHitSize;
    size_t shadowAnyHitSize;
    size_t shadowMissSize;
} RayTracingShaderVariant;

typedef struct RayTracingShaderModules {
    VkShaderModule rayGen[VKRT_MAIN_RAYGEN_GROUP_COUNT];
    VkShaderModule closestHit;
    VkShaderModule anyHit;
    VkShaderModule miss;
    VkShaderModule shadowClosestHit;
    VkShaderModule shadowAnyHit;
    VkShaderModule shadowMiss;
} RayTracingShaderModules;

typedef enum MainRayTracingShaderGroupIndex {
    MAIN_RAY_TRACING_GROUP_RAYGEN_RGB = 0u,
    MAIN_RAY_TRACING_GROUP_RAYGEN_SPECTRAL_SINGLE = 1u,
    MAIN_RAY_TRACING_GROUP_RAYGEN_SPECTRAL_HERO = 2u,
    MAIN_RAY_TRACING_GROUP_MISS_MAIN = 3u,
    MAIN_RAY_TRACING_GROUP_MISS_SHADOW = 4u,
    MAIN_RAY_TRACING_GROUP_HIT_MAIN_OPAQUE = 5u,
    MAIN_RAY_TRACING_GROUP_HIT_MAIN_ALPHA = 6u,
    MAIN_RAY_TRACING_GROUP_HIT_SHADOW_OPAQUE = 7u,
    MAIN_RAY_TRACING_GROUP_HIT_SHADOW_ALPHA = 8u,
    MAIN_RAY_TRACING_GROUP_COUNT = 9u
} MainRayTracingShaderGroupIndex;

typedef enum SelectionRayTracingShaderGroupIndex {
    SELECTION_RAY_TRACING_GROUP_RAYGEN = 0u,
    SELECTION_RAY_TRACING_GROUP_MISS = 1u,
    SELECTION_RAY_TRACING_GROUP_HIT = 2u
} SelectionRayTracingShaderGroupIndex;

VkPipelineDynamicStateCreateInfo makeRayTracingPipelineDynamicStateCreateInfo(
    const VkDynamicState* dynamicStates,
    uint32_t dynamicStateCount
);
VkPipelineShaderStageCreateInfo makePipelineShaderStageInfo(VkShaderStageFlagBits stage, VkShaderModule module);
VkRayTracingShaderGroupCreateInfoKHR makeGeneralShaderGroup(uint32_t generalShader);
VkRayTracingShaderGroupCreateInfoKHR makeTriangleHitShaderGroup(uint32_t closestHitShader, uint32_t anyHitShader);

VKRT_Result createRayTracingPipelineLayout(VKRT* vkrt);
VKRT_Result storeMainRayTracingStackSizes(VKRT* vkrt, VkPipeline pipeline);
VKRT_Result storeSelectionRayTracingStackSize(VKRT* vkrt, VkPipeline pipeline);

RayTracingShaderVariant selectRayTracingShaderVariant(VkBool32 useSerShaders);
void destroyRayTracingShaderModules(VKRT* vkrt, RayTracingShaderModules* modules);
VKRT_Result createRayTracingShaderModules(
    VKRT* vkrt,
    const RayTracingShaderVariant* variant,
    RayTracingShaderModules* outModules
);
