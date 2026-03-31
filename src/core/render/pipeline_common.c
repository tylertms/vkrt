#include "debug.h"
#include "pipeline.h"
#include "pipeline_internal.h"
#include "shaders.h"
#include "vkrt_internal.h"
#include "vkrt_types.h"

#include <stddef.h>
#include <stdint.h>
#include <vulkan/vulkan_core.h>

VkPipelineDynamicStateCreateInfo makeRayTracingPipelineDynamicStateCreateInfo(
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

VKRT_Result storeMainRayTracingStackSizes(VKRT* vkrt, VkPipeline pipeline) {
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

VKRT_Result storeSelectionRayTracingStackSize(VKRT* vkrt, VkPipeline pipeline) {
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

VKRT_Result createRayTracingPipelineLayout(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    if (vkrt->core.pipelineLayout != VK_NULL_HANDLE) return VKRT_SUCCESS;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &vkrt->core.descriptorSetLayout,
    };

    if (vkCreatePipelineLayout(vkrt->core.device, &pipelineLayoutInfo, NULL, &vkrt->core.pipelineLayout) !=
        VK_SUCCESS) {
        LOG_ERROR("Failed to create pipeline layout");
        return VKRT_ERROR_PIPELINE_CREATION_FAILED;
    }
    return VKRT_SUCCESS;
}

RayTracingShaderVariant selectRayTracingShaderVariant(VkBool32 useSerShaders) {
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

void destroyRayTracingShaderModules(VKRT* vkrt, RayTracingShaderModules* modules) {
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

VKRT_Result createRayTracingShaderModules(
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

VkPipelineShaderStageCreateInfo makePipelineShaderStageInfo(VkShaderStageFlagBits stage, VkShaderModule module) {
    return (VkPipelineShaderStageCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = stage,
        .module = module,
        .pName = "main",
    };
}

VkRayTracingShaderGroupCreateInfoKHR makeGeneralShaderGroup(uint32_t generalShader) {
    return (VkRayTracingShaderGroupCreateInfoKHR){
        .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
        .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
        .generalShader = generalShader,
        .closestHitShader = VK_SHADER_UNUSED_KHR,
        .anyHitShader = VK_SHADER_UNUSED_KHR,
        .intersectionShader = VK_SHADER_UNUSED_KHR,
    };
}

VkRayTracingShaderGroupCreateInfoKHR makeTriangleHitShaderGroup(uint32_t closestHitShader, uint32_t anyHitShader) {
    return (VkRayTracingShaderGroupCreateInfoKHR){
        .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
        .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
        .generalShader = VK_SHADER_UNUSED_KHR,
        .closestHitShader = closestHitShader,
        .anyHitShader = anyHitShader,
        .intersectionShader = VK_SHADER_UNUSED_KHR,
    };
}

VKRT_Result createShaderModule(VKRT* vkrt, const uint32_t* spirv, size_t length, VkShaderModule* outShaderModule) {
    if (!vkrt || !spirv || !outShaderModule) return VKRT_ERROR_INVALID_ARGUMENT;
    if (length == 0 || (length % sizeof(uint32_t)) != 0) return VKRT_ERROR_INVALID_ARGUMENT;

    VkShaderModuleCreateInfo shaderModuleCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = length,
        .pCode = spirv,
    };
    if (vkCreateShaderModule(vkrt->core.device, &shaderModuleCreateInfo, NULL, outShaderModule) != VK_SUCCESS) {
        LOG_ERROR("Failed to create shader module");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    return VKRT_SUCCESS;
}
