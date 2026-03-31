#include "pipeline.h"

#include "config.h"
#include "debug.h"
#include "shaders.h"
#include "sync.h"
#include "vkrt_internal.h"
#include "vkrt_types.h"
#include "vulkan/vulkan_core.h"

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

static VkPipelineDynamicStateCreateInfo makeRayTracingPipelineDynamicStateCreateInfo(
    const VkDynamicState* dynamicStates,
    uint32_t dynamicStateCount
) {
    return (VkPipelineDynamicStateCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = dynamicStateCount,
        .pDynamicStates = dynamicStates,
    };
}

static VkDeviceSize queryShaderGroupStackSize(
    const VKRT* vkrt,
    VkPipeline pipeline,
    uint32_t group,
    VkShaderGroupShaderKHR shader
) {
    if (!vkrt || pipeline == VK_NULL_HANDLE || !vkrt->core.procs.vkGetRayTracingShaderGroupStackSizeKHR) return 0;
    return vkrt->core.procs.vkGetRayTracingShaderGroupStackSizeKHR(vkrt->core.device, pipeline, group, shader);
}

static VkDeviceSize maxDeviceSize(VkDeviceSize a, VkDeviceSize b) {
    return a > b ? a : b;
}

static VKRT_Result storeMainRayTracingStackSizes(VKRT* vkrt, VkPipeline pipeline) {
    if (!vkrt || pipeline == VK_NULL_HANDLE) return VKRT_ERROR_INVALID_ARGUMENT;

    const VkDeviceSize mainMissStack =
        queryShaderGroupStackSize(vkrt, pipeline, MAIN_RAY_TRACING_GROUP_MISS_MAIN, VK_SHADER_GROUP_SHADER_GENERAL_KHR);
    const VkDeviceSize shadowMissStack = queryShaderGroupStackSize(
        vkrt,
        pipeline,
        MAIN_RAY_TRACING_GROUP_MISS_SHADOW,
        VK_SHADER_GROUP_SHADER_GENERAL_KHR
    );
    const VkDeviceSize mainClosestHitStack = maxDeviceSize(
        queryShaderGroupStackSize(
            vkrt,
            pipeline,
            MAIN_RAY_TRACING_GROUP_HIT_MAIN_OPAQUE,
            VK_SHADER_GROUP_SHADER_CLOSEST_HIT_KHR
        ),
        queryShaderGroupStackSize(
            vkrt,
            pipeline,
            MAIN_RAY_TRACING_GROUP_HIT_MAIN_ALPHA,
            VK_SHADER_GROUP_SHADER_CLOSEST_HIT_KHR
        )
    );
    const VkDeviceSize mainAnyHitStack = queryShaderGroupStackSize(
        vkrt,
        pipeline,
        MAIN_RAY_TRACING_GROUP_HIT_MAIN_ALPHA,
        VK_SHADER_GROUP_SHADER_ANY_HIT_KHR
    );
    const VkDeviceSize shadowClosestHitStack = maxDeviceSize(
        queryShaderGroupStackSize(
            vkrt,
            pipeline,
            MAIN_RAY_TRACING_GROUP_HIT_SHADOW_OPAQUE,
            VK_SHADER_GROUP_SHADER_CLOSEST_HIT_KHR
        ),
        queryShaderGroupStackSize(
            vkrt,
            pipeline,
            MAIN_RAY_TRACING_GROUP_HIT_SHADOW_ALPHA,
            VK_SHADER_GROUP_SHADER_CLOSEST_HIT_KHR
        )
    );
    const VkDeviceSize shadowAnyHitStack = queryShaderGroupStackSize(
        vkrt,
        pipeline,
        MAIN_RAY_TRACING_GROUP_HIT_SHADOW_ALPHA,
        VK_SHADER_GROUP_SHADER_ANY_HIT_KHR
    );

    const VkDeviceSize mainTraceStack =
        maxDeviceSize(mainMissStack, maxDeviceSize(mainClosestHitStack, mainAnyHitStack));
    const VkDeviceSize shadowTraceStack =
        maxDeviceSize(shadowMissStack, maxDeviceSize(shadowClosestHitStack, shadowAnyHitStack));
    const VkDeviceSize traceTailStack = maxDeviceSize(mainTraceStack, shadowTraceStack);

    for (uint32_t raygenIndex = 0; raygenIndex < VKRT_MAIN_RAYGEN_GROUP_COUNT; raygenIndex++) {
        VkDeviceSize raygenStack =
            queryShaderGroupStackSize(vkrt, pipeline, raygenIndex, VK_SHADER_GROUP_SHADER_GENERAL_KHR);
        VkDeviceSize totalStack = raygenStack + traceTailStack;
        if (totalStack > UINT32_MAX) {
            LOG_ERROR("Ray tracing pipeline stack size overflow for raygen group %u", raygenIndex);
            return VKRT_ERROR_OPERATION_FAILED;
        }
        vkrt->core.mainRayTracingStackSizes[raygenIndex] = (uint32_t)totalStack;
    }

    return VKRT_SUCCESS;
}

static VKRT_Result storeSelectionRayTracingStackSize(VKRT* vkrt, VkPipeline pipeline) {
    if (!vkrt || pipeline == VK_NULL_HANDLE) return VKRT_ERROR_INVALID_ARGUMENT;

    const VkDeviceSize raygenStack = queryShaderGroupStackSize(
        vkrt,
        pipeline,
        SELECTION_RAY_TRACING_GROUP_RAYGEN,
        VK_SHADER_GROUP_SHADER_GENERAL_KHR
    );
    const VkDeviceSize missStack =
        queryShaderGroupStackSize(vkrt, pipeline, SELECTION_RAY_TRACING_GROUP_MISS, VK_SHADER_GROUP_SHADER_GENERAL_KHR);
    const VkDeviceSize closestHitStack = queryShaderGroupStackSize(
        vkrt,
        pipeline,
        SELECTION_RAY_TRACING_GROUP_HIT,
        VK_SHADER_GROUP_SHADER_CLOSEST_HIT_KHR
    );
    const VkDeviceSize anyHitStack =
        queryShaderGroupStackSize(vkrt, pipeline, SELECTION_RAY_TRACING_GROUP_HIT, VK_SHADER_GROUP_SHADER_ANY_HIT_KHR);
    const VkDeviceSize totalStack = raygenStack + maxDeviceSize(missStack, maxDeviceSize(closestHitStack, anyHitStack));

    if (totalStack > UINT32_MAX) {
        LOG_ERROR("Selection ray tracing pipeline stack size overflow");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    vkrt->core.selectionRayTracingStackSize = (uint32_t)totalStack;
    return VKRT_SUCCESS;
}

static VKRT_Result createRayTracingPipelineLayout(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    if (vkrt->core.pipelineLayout != VK_NULL_HANDLE) return VKRT_SUCCESS;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {0};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &vkrt->core.descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 0;

    if (vkCreatePipelineLayout(vkrt->core.device, &pipelineLayoutInfo, NULL, &vkrt->core.pipelineLayout) !=
        VK_SUCCESS) {
        LOG_ERROR("Failed to create pipeline layout");
        return VKRT_ERROR_PIPELINE_CREATION_FAILED;
    }
    return VKRT_SUCCESS;
}

static RayTracingShaderVariant selectRayTracingShaderVariant(VkBool32 useSerShaders) {
    return (RayTracingShaderVariant){
        .rayGenData =
            {
                useSerShaders ? shaderRgenSerData : shaderRgenData,
                useSerShaders ? shaderRgenSpectralSingleSerData : shaderRgenSpectralSingleData,
                useSerShaders ? shaderRgenSpectralHeroSerData : shaderRgenSpectralHeroData,
            },
        .closestHitData = useSerShaders ? shaderRchitSerData : shaderRchitData,
        .anyHitData = shaderRahitData,
        .missData = useSerShaders ? shaderRmissSerData : shaderRmissData,
        .shadowClosestHitData = useSerShaders ? shaderShadowRchitSerData : shaderShadowRchitData,
        .shadowAnyHitData = shaderShadowRahitData,
        .shadowMissData = useSerShaders ? shaderShadowMissSerData : shaderShadowMissData,
        .rayGenSize =
            {
                useSerShaders ? shaderRgenSerSize : shaderRgenSize,
                useSerShaders ? shaderRgenSpectralSingleSerSize : shaderRgenSpectralSingleSize,
                useSerShaders ? shaderRgenSpectralHeroSerSize : shaderRgenSpectralHeroSize,
            },
        .closestHitSize = useSerShaders ? shaderRchitSerSize : shaderRchitSize,
        .anyHitSize = shaderRahitSize,
        .missSize = useSerShaders ? shaderRmissSerSize : shaderRmissSize,
        .shadowClosestHitSize = useSerShaders ? shaderShadowRchitSerSize : shaderShadowRchitSize,
        .shadowAnyHitSize = shaderShadowRahitSize,
        .shadowMissSize = useSerShaders ? shaderShadowMissSerSize : shaderShadowMissSize,
    };
}

static void destroyRayTracingShaderModules(VKRT* vkrt, RayTracingShaderModules* modules) {
    if (!vkrt || !modules) return;

    for (uint32_t i = 0; i < VKRT_MAIN_RAYGEN_GROUP_COUNT; i++) {
        if (modules->rayGen[i] != VK_NULL_HANDLE) {
            vkDestroyShaderModule(vkrt->core.device, modules->rayGen[i], NULL);
        }
    }
    if (modules->closestHit != VK_NULL_HANDLE) vkDestroyShaderModule(vkrt->core.device, modules->closestHit, NULL);
    if (modules->anyHit != VK_NULL_HANDLE) vkDestroyShaderModule(vkrt->core.device, modules->anyHit, NULL);
    if (modules->miss != VK_NULL_HANDLE) vkDestroyShaderModule(vkrt->core.device, modules->miss, NULL);
    if (modules->shadowClosestHit != VK_NULL_HANDLE) {
        vkDestroyShaderModule(vkrt->core.device, modules->shadowClosestHit, NULL);
    }
    if (modules->shadowAnyHit != VK_NULL_HANDLE) vkDestroyShaderModule(vkrt->core.device, modules->shadowAnyHit, NULL);
    if (modules->shadowMiss != VK_NULL_HANDLE) vkDestroyShaderModule(vkrt->core.device, modules->shadowMiss, NULL);
    *modules = (RayTracingShaderModules){0};
}

static VKRT_Result createRayTracingShaderModules(
    VKRT* vkrt,
    const RayTracingShaderVariant* variant,
    RayTracingShaderModules* outModules
) {
    *outModules = (RayTracingShaderModules){0};
    for (uint32_t i = 0; i < VKRT_MAIN_RAYGEN_GROUP_COUNT; i++) {
        if (createShaderModule(vkrt, variant->rayGenData[i], variant->rayGenSize[i], &outModules->rayGen[i]) !=
            VKRT_SUCCESS) {
            destroyRayTracingShaderModules(vkrt, outModules);
            return VKRT_ERROR_SHADER_COMPILATION_FAILED;
        }
    }

    if (createShaderModule(vkrt, variant->closestHitData, variant->closestHitSize, &outModules->closestHit) !=
            VKRT_SUCCESS ||
        createShaderModule(vkrt, variant->anyHitData, variant->anyHitSize, &outModules->anyHit) != VKRT_SUCCESS ||
        createShaderModule(vkrt, variant->missData, variant->missSize, &outModules->miss) != VKRT_SUCCESS ||
        createShaderModule(
            vkrt,
            variant->shadowClosestHitData,
            variant->shadowClosestHitSize,
            &outModules->shadowClosestHit
        ) != VKRT_SUCCESS ||
        createShaderModule(vkrt, variant->shadowAnyHitData, variant->shadowAnyHitSize, &outModules->shadowAnyHit) !=
            VKRT_SUCCESS ||
        createShaderModule(vkrt, variant->shadowMissData, variant->shadowMissSize, &outModules->shadowMiss) !=
            VKRT_SUCCESS) {
        destroyRayTracingShaderModules(vkrt, outModules);
        return VKRT_ERROR_SHADER_COMPILATION_FAILED;
    }

    return VKRT_SUCCESS;
}

static VkPipelineShaderStageCreateInfo makePipelineShaderStageInfo(VkShaderStageFlagBits stage, VkShaderModule module) {
    return (VkPipelineShaderStageCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = stage,
        .module = module,
        .pName = "main",
    };
}

static VkRayTracingShaderGroupCreateInfoKHR makeGeneralShaderGroup(uint32_t generalShader) {
    return (VkRayTracingShaderGroupCreateInfoKHR){
        .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
        .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
        .generalShader = generalShader,
        .closestHitShader = VK_SHADER_UNUSED_KHR,
        .anyHitShader = VK_SHADER_UNUSED_KHR,
        .intersectionShader = VK_SHADER_UNUSED_KHR,
    };
}

static VkRayTracingShaderGroupCreateInfoKHR makeTriangleHitShaderGroup(
    uint32_t closestHitShader,
    uint32_t anyHitShader
) {
    return (VkRayTracingShaderGroupCreateInfoKHR){
        .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
        .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
        .generalShader = VK_SHADER_UNUSED_KHR,
        .closestHitShader = closestHitShader,
        .anyHitShader = anyHitShader,
        .intersectionShader = VK_SHADER_UNUSED_KHR,
    };
}

VKRT_Result createRayTracingPipeline(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    uint64_t startTime = getMicroseconds();
    VkBool32 useSerShaders = vkrtSerEnabled(vkrt);
    RayTracingShaderVariant shaderVariant = selectRayTracingShaderVariant(useSerShaders);

    if (createRayTracingPipelineLayout(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;

    RayTracingShaderModules modules = {0};
    if (createRayTracingShaderModules(vkrt, &shaderVariant, &modules) != VKRT_SUCCESS) {
        return VKRT_ERROR_SHADER_COMPILATION_FAILED;
    }

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        makePipelineShaderStageInfo(VK_SHADER_STAGE_RAYGEN_BIT_KHR, modules.rayGen[VKRT_MAIN_RAYGEN_GROUP_RGB]),
        makePipelineShaderStageInfo(
            VK_SHADER_STAGE_RAYGEN_BIT_KHR,
            modules.rayGen[VKRT_MAIN_RAYGEN_GROUP_SPECTRAL_SINGLE]
        ),
        makePipelineShaderStageInfo(
            VK_SHADER_STAGE_RAYGEN_BIT_KHR,
            modules.rayGen[VKRT_MAIN_RAYGEN_GROUP_SPECTRAL_HERO]
        ),
        makePipelineShaderStageInfo(VK_SHADER_STAGE_MISS_BIT_KHR, modules.miss),
        makePipelineShaderStageInfo(VK_SHADER_STAGE_MISS_BIT_KHR, modules.shadowMiss),
        makePipelineShaderStageInfo(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, modules.closestHit),
        makePipelineShaderStageInfo(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, modules.shadowClosestHit),
        makePipelineShaderStageInfo(VK_SHADER_STAGE_ANY_HIT_BIT_KHR, modules.anyHit),
        makePipelineShaderStageInfo(VK_SHADER_STAGE_ANY_HIT_BIT_KHR, modules.shadowAnyHit),
    };

    VkRayTracingShaderGroupCreateInfoKHR shaderGroups[] = {
        makeGeneralShaderGroup(0),
        makeGeneralShaderGroup(1),
        makeGeneralShaderGroup(2),
        makeGeneralShaderGroup(3),
        makeGeneralShaderGroup(4),
        makeTriangleHitShaderGroup(5, VK_SHADER_UNUSED_KHR),
        makeTriangleHitShaderGroup(5, 7),
        makeTriangleHitShaderGroup(6, VK_SHADER_UNUSED_KHR),
        makeTriangleHitShaderGroup(6, 8),
    };

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_RAY_TRACING_PIPELINE_STACK_SIZE_KHR};
    VkPipelineDynamicStateCreateInfo dynamicStateInfo =
        makeRayTracingPipelineDynamicStateCreateInfo(dynamicStates, (uint32_t)VKRT_ARRAY_COUNT(dynamicStates));

    VkRayTracingPipelineCreateInfoKHR pipelineCreateInfo = {0};
    pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
    pipelineCreateInfo.stageCount = (uint32_t)(sizeof(shaderStages) / sizeof(shaderStages[0]));
    pipelineCreateInfo.pStages = shaderStages;
    pipelineCreateInfo.groupCount = (uint32_t)(sizeof(shaderGroups) / sizeof(shaderGroups[0]));
    pipelineCreateInfo.pGroups = shaderGroups;
    pipelineCreateInfo.maxPipelineRayRecursionDepth = 1;
    pipelineCreateInfo.layout = vkrt->core.pipelineLayout;
    pipelineCreateInfo.pDynamicState = &dynamicStateInfo;

    if (vkrt->core.procs.vkCreateRayTracingPipelinesKHR(
            vkrt->core.device,
            VK_NULL_HANDLE,
            VK_NULL_HANDLE,
            1,
            &pipelineCreateInfo,
            NULL,
            &vkrt->core.rayTracingPipeline
        ) != VK_SUCCESS) {
        LOG_ERROR("Failed to create ray tracing pipeline");
        destroyRayTracingShaderModules(vkrt, &modules);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    if (storeMainRayTracingStackSizes(vkrt, vkrt->core.rayTracingPipeline) != VKRT_SUCCESS) {
        destroyRayTracingShaderModules(vkrt, &modules);
        vkDestroyPipeline(vkrt->core.device, vkrt->core.rayTracingPipeline, NULL);
        vkrt->core.rayTracingPipeline = VK_NULL_HANDLE;
        return VKRT_ERROR_OPERATION_FAILED;
    }

    destroyRayTracingShaderModules(vkrt, &modules);

    LOG_INFO(
        "Ray tracing pipeline created. Variant: %s, SER hint: %s, Shader Stages: %u, Shader Groups: %u, in "
        "%.3f ms",
        useSerShaders ? "SER" : "default",
        vkrt->core.serReorderingHintMode == VK_RAY_TRACING_INVOCATION_REORDER_MODE_REORDER_EXT ? "reorder" : "none",
        (uint32_t)VKRT_ARRAY_COUNT(shaderStages),
        (uint32_t)VKRT_ARRAY_COUNT(shaderGroups),
        (double)(getMicroseconds() - startTime) / 1e3
    );
    return VKRT_SUCCESS;
}

VKRT_Result createSelectionRayTracingPipeline(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    if (createRayTracingPipelineLayout(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;

    uint64_t startTime = getMicroseconds();
    VKRT_Result result = VKRT_ERROR_OPERATION_FAILED;
    VkShaderModule rayGenModule = VK_NULL_HANDLE;
    VkShaderModule missModule = VK_NULL_HANDLE;
    VkShaderModule closestHitModule = VK_NULL_HANDLE;
    VkShaderModule anyHitModule = VK_NULL_HANDLE;

    if (createShaderModule(vkrt, shaderSelectRgenData, shaderSelectRgenSize, &rayGenModule) != VKRT_SUCCESS ||
        createShaderModule(vkrt, shaderSelectRmissData, shaderSelectRmissSize, &missModule) != VKRT_SUCCESS ||
        createShaderModule(vkrt, shaderSelectRchitData, shaderSelectRchitSize, &closestHitModule) != VKRT_SUCCESS ||
        createShaderModule(vkrt, shaderSelectRahitData, shaderSelectRahitSize, &anyHitModule) != VKRT_SUCCESS) {
        goto cleanup;
    }

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
         .module = rayGenModule,
         .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_MISS_BIT_KHR,
         .module = missModule,
         .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
         .module = closestHitModule,
         .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_ANY_HIT_BIT_KHR,
         .module = anyHitModule,
         .pName = "main"},
    };

    VkRayTracingShaderGroupCreateInfoKHR shaderGroups[3] = {
        {.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
         .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
         .generalShader = 0,
         .closestHitShader = VK_SHADER_UNUSED_KHR,
         .anyHitShader = VK_SHADER_UNUSED_KHR,
         .intersectionShader = VK_SHADER_UNUSED_KHR},
        {.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
         .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
         .generalShader = 1,
         .closestHitShader = VK_SHADER_UNUSED_KHR,
         .anyHitShader = VK_SHADER_UNUSED_KHR,
         .intersectionShader = VK_SHADER_UNUSED_KHR},
        {.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
         .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
         .generalShader = VK_SHADER_UNUSED_KHR,
         .closestHitShader = 2,
         .anyHitShader = 3,
         .intersectionShader = VK_SHADER_UNUSED_KHR},
    };

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_RAY_TRACING_PIPELINE_STACK_SIZE_KHR};
    VkPipelineDynamicStateCreateInfo dynamicStateInfo =
        makeRayTracingPipelineDynamicStateCreateInfo(dynamicStates, (uint32_t)VKRT_ARRAY_COUNT(dynamicStates));

    VkRayTracingPipelineCreateInfoKHR pipelineCreateInfo = {0};
    pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
    pipelineCreateInfo.stageCount = (uint32_t)(sizeof(shaderStages) / sizeof(shaderStages[0]));
    pipelineCreateInfo.pStages = shaderStages;
    pipelineCreateInfo.groupCount = (uint32_t)(sizeof(shaderGroups) / sizeof(shaderGroups[0]));
    pipelineCreateInfo.pGroups = shaderGroups;
    pipelineCreateInfo.maxPipelineRayRecursionDepth = 1;
    pipelineCreateInfo.layout = vkrt->core.pipelineLayout;
    pipelineCreateInfo.pDynamicState = &dynamicStateInfo;

    if (vkrt->core.procs.vkCreateRayTracingPipelinesKHR(
            vkrt->core.device,
            VK_NULL_HANDLE,
            VK_NULL_HANDLE,
            1,
            &pipelineCreateInfo,
            NULL,
            &vkrt->core.selectionRayTracingPipeline
        ) != VK_SUCCESS) {
        LOG_ERROR("Failed to create selection ray tracing pipeline");
        goto cleanup;
    }

    if (storeSelectionRayTracingStackSize(vkrt, vkrt->core.selectionRayTracingPipeline) != VKRT_SUCCESS) {
        vkDestroyPipeline(vkrt->core.device, vkrt->core.selectionRayTracingPipeline, NULL);
        vkrt->core.selectionRayTracingPipeline = VK_NULL_HANDLE;
        goto cleanup;
    }

    result = VKRT_SUCCESS;
    LOG_INFO("Selection ray tracing pipeline created in %.3f ms", (double)(getMicroseconds() - startTime) / 1e3);

cleanup:
    if (rayGenModule != VK_NULL_HANDLE) vkDestroyShaderModule(vkrt->core.device, rayGenModule, NULL);
    if (missModule != VK_NULL_HANDLE) vkDestroyShaderModule(vkrt->core.device, missModule, NULL);
    if (closestHitModule != VK_NULL_HANDLE) vkDestroyShaderModule(vkrt->core.device, closestHitModule, NULL);
    if (anyHitModule != VK_NULL_HANDLE) vkDestroyShaderModule(vkrt->core.device, anyHitModule, NULL);
    return result;
}

VKRT_Result createComputePipeline(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    uint64_t startTime = getMicroseconds();

    VkShaderModule compModule = VK_NULL_HANDLE;
    if (createShaderModule(vkrt, shaderCompData, shaderCompSize, &compModule) != VKRT_SUCCESS) {
        return VKRT_ERROR_OPERATION_FAILED;
    }

    VkPipelineShaderStageCreateInfo stageInfo = makePipelineShaderStageInfo(VK_SHADER_STAGE_COMPUTE_BIT, compModule);

    VkComputePipelineCreateInfo createInfo = {0};
    createInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    createInfo.stage = stageInfo;
    createInfo.layout = vkrt->core.pipelineLayout;

    if (vkCreateComputePipelines(vkrt->core.device, VK_NULL_HANDLE, 1, &createInfo, NULL, &vkrt->core.computePipeline) !=
        VK_SUCCESS) {
        LOG_ERROR("Failed to create compute pipeline");
        vkDestroyShaderModule(vkrt->core.device, compModule, NULL);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    vkDestroyShaderModule(vkrt->core.device, compModule, NULL);

    LOG_INFO("Compute pipeline created in %.3f ms", (double)(getMicroseconds() - startTime) / 1e3);
    return VKRT_SUCCESS;
}

VKRT_Result createSyncObjects(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    VkSemaphoreCreateInfo semaphoreCreateInfo = {0};
    semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo = {0};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < VKRT_MAX_FRAMES_IN_FLIGHT; i++) {
        vkrt->runtime.imageAvailableSemaphores[i] = VK_NULL_HANDLE;
        vkrt->runtime.inFlightFences[i] = VK_NULL_HANDLE;
    }

    size_t createdImageAvailableCount = 0;
    size_t createdFenceCount = 0;

    for (size_t i = 0; i < VKRT_MAX_FRAMES_IN_FLIGHT; i++) {
        VkResult imageAvailableSemaphoreResult =
            vkCreateSemaphore(vkrt->core.device, &semaphoreCreateInfo, NULL, &vkrt->runtime.imageAvailableSemaphores[i]);
        if (imageAvailableSemaphoreResult != VK_SUCCESS) {
            LOG_ERROR("Failed to create image-available semaphore");
            goto create_sync_objects_failed;
        }
        createdImageAvailableCount++;

        VkResult inFlightFenceResult =
            vkCreateFence(vkrt->core.device, &fenceInfo, NULL, &vkrt->runtime.inFlightFences[i]);
        if (inFlightFenceResult != VK_SUCCESS) {
            LOG_ERROR("Failed to create in-flight fence");
            goto create_sync_objects_failed;
        }
        createdFenceCount++;
    }

    if (resetRenderFinishedSemaphores(vkrt, vkrt->runtime.swapChainImageCount, vkrt->runtime.swapChainImageCount) !=
        VKRT_SUCCESS) {
        goto create_sync_objects_failed;
    }

    return VKRT_SUCCESS;

create_sync_objects_failed:
    for (size_t i = 0; i < createdImageAvailableCount; i++) {
        if (vkrt->runtime.imageAvailableSemaphores[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(vkrt->core.device, vkrt->runtime.imageAvailableSemaphores[i], NULL);
            vkrt->runtime.imageAvailableSemaphores[i] = VK_NULL_HANDLE;
        }
    }
    for (size_t i = 0; i < createdFenceCount; i++) {
        if (vkrt->runtime.inFlightFences[i] != VK_NULL_HANDLE) {
            vkDestroyFence(vkrt->core.device, vkrt->runtime.inFlightFences[i], NULL);
            vkrt->runtime.inFlightFences[i] = VK_NULL_HANDLE;
        }
    }
    resetRenderFinishedSemaphores(vkrt, vkrt->runtime.swapChainImageCount, 0);
    return VKRT_ERROR_OPERATION_FAILED;
}

VKRT_Result createShaderModule(VKRT* vkrt, const uint32_t* spirv, size_t length, VkShaderModule* outShaderModule) {
    if (!vkrt || !spirv || !outShaderModule) return VKRT_ERROR_INVALID_ARGUMENT;
    if (length == 0 || (length % sizeof(uint32_t)) != 0) return VKRT_ERROR_INVALID_ARGUMENT;

    VkShaderModuleCreateInfo shaderModuleCreateInfo = {0};
    shaderModuleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderModuleCreateInfo.codeSize = length;
    shaderModuleCreateInfo.pCode = spirv;

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(vkrt->core.device, &shaderModuleCreateInfo, NULL, &shaderModule) != VK_SUCCESS) {
        LOG_ERROR("Failed to create shader module");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    *outShaderModule = shaderModule;
    return VKRT_SUCCESS;
}
