#include "debug.h"
#include "pipeline.h"
#include "pipeline_internal.h"
#include "platform.h"
#include "shaders.h"
#include "vkrt_internal.h"
#include "vkrt_types.h"

#include <stddef.h>
#include <stdint.h>
#include <vulkan/vulkan_core.h>

static void logRayTracingPipelineCreateResult(const char* label, uint64_t startTime, VkResult result) {
    LOG_TRACE(
        "%s vkCreateRayTracingPipelinesKHR returned %d in %.3f ms (synchronous)",
        label,
        (int)result,
        (double)(getMicroseconds() - startTime) / 1e3
    );
}

static void logElapsedTraceMs(const char* label, uint64_t startTime) {
    LOG_TRACE("%s in %.3f ms", label, (double)(getMicroseconds() - startTime) / 1e3);
}

static void logMainRayTracingPipelineCreated(
    uint64_t startTime,
    VkBool32 useSerShaders,
    VkRayTracingInvocationReorderModeNV serReorderingHintMode,
    uint32_t shaderStageCount,
    uint32_t shaderGroupCount
) {
    LOG_TRACE(
        "Main RT pipeline created. Variant: %s, SER hint: %s, Shader Stages: %u, Shader Groups: %u, in %.3f ms",
        useSerShaders ? "SER" : "default",
        serReorderingHintMode == VK_RAY_TRACING_INVOCATION_REORDER_MODE_REORDER_EXT ? "reorder" : "none",
        shaderStageCount,
        shaderGroupCount,
        (double)(getMicroseconds() - startTime) / 1e3
    );
}

static void logSelectionRayTracingPipelineCreated(uint64_t startTime) {
    LOG_TRACE("Selection RT pipeline created in %.3f ms", (double)(getMicroseconds() - startTime) / 1e3);
}

static VkResult createRayTracingPipelineTracked(
    VKRT* vkrt,
    const char* label,
    const VkRayTracingPipelineCreateInfoKHR* pipelineCreateInfo,
    VkPipeline* outPipeline
) {
    if (!vkrt || !label || !pipelineCreateInfo || !outPipeline) return VK_ERROR_INITIALIZATION_FAILED;

    uint64_t createStartTime = getMicroseconds();
    VkResult result = vkrt->core.procs.vkCreateRayTracingPipelinesKHR(
        vkrt->core.device,
        VK_NULL_HANDLE,
        VK_NULL_HANDLE,
        1,
        pipelineCreateInfo,
        NULL,
        outPipeline
    );
    logRayTracingPipelineCreateResult(label, createStartTime, result);
    return result;
}

VKRT_Result createRayTracingPipeline(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    uint64_t startTime = getMicroseconds();
    VkBool32 useSerShaders = vkrtSerEnabled(vkrt);
    RayTracingShaderVariant shaderVariant = selectRayTracingShaderVariant(useSerShaders);

    if (createRayTracingPipelineLayout(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;

    RayTracingShaderModules modules = {0};
    uint64_t shaderModuleStartTime = getMicroseconds();
    if (createRayTracingShaderModules(vkrt, &shaderVariant, &modules) != VKRT_SUCCESS) {
        return VKRT_ERROR_SHADER_COMPILATION_FAILED;
    }
    logElapsedTraceMs("Main RT shader modules created", shaderModuleStartTime);

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
    VkRayTracingPipelineCreateInfoKHR pipelineCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
        .stageCount = (uint32_t)VKRT_ARRAY_COUNT(shaderStages),
        .pStages = shaderStages,
        .groupCount = (uint32_t)VKRT_ARRAY_COUNT(shaderGroups),
        .pGroups = shaderGroups,
        .maxPipelineRayRecursionDepth = 1,
        .layout = vkrt->core.pipelineLayout,
        .pDynamicState = &dynamicStateInfo,
    };

    if (createRayTracingPipelineTracked(vkrt, "Main RT", &pipelineCreateInfo, &vkrt->core.rayTracingPipeline) !=
        VK_SUCCESS) {
        LOG_ERROR("Failed to create ray tracing pipeline");
        destroyRayTracingShaderModules(vkrt, &modules);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    uint64_t stackSizeStartTime = getMicroseconds();
    if (storeMainRayTracingStackSizes(vkrt, vkrt->core.rayTracingPipeline) != VKRT_SUCCESS) {
        destroyRayTracingShaderModules(vkrt, &modules);
        vkDestroyPipeline(vkrt->core.device, vkrt->core.rayTracingPipeline, NULL);
        vkrt->core.rayTracingPipeline = VK_NULL_HANDLE;
        return VKRT_ERROR_OPERATION_FAILED;
    }
    logElapsedTraceMs("Main RT stack sizes queried", stackSizeStartTime);

    destroyRayTracingShaderModules(vkrt, &modules);
    logMainRayTracingPipelineCreated(
        startTime,
        useSerShaders,
        vkrt->core.serReorderingHintMode,
        (uint32_t)VKRT_ARRAY_COUNT(shaderStages),
        (uint32_t)VKRT_ARRAY_COUNT(shaderGroups)
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

    uint64_t shaderModuleStartTime = getMicroseconds();
    if (createShaderModule(vkrt, shaderSelectRgenData, shaderSelectRgenSize, &rayGenModule) != VKRT_SUCCESS ||
        createShaderModule(vkrt, shaderSelectRmissData, shaderSelectRmissSize, &missModule) != VKRT_SUCCESS ||
        createShaderModule(vkrt, shaderSelectRchitData, shaderSelectRchitSize, &closestHitModule) != VKRT_SUCCESS ||
        createShaderModule(vkrt, shaderSelectRahitData, shaderSelectRahitSize, &anyHitModule) != VKRT_SUCCESS) {
        goto cleanup;
    }
    logElapsedTraceMs("Selection RT shader modules created", shaderModuleStartTime);

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        makePipelineShaderStageInfo(VK_SHADER_STAGE_RAYGEN_BIT_KHR, rayGenModule),
        makePipelineShaderStageInfo(VK_SHADER_STAGE_MISS_BIT_KHR, missModule),
        makePipelineShaderStageInfo(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, closestHitModule),
        makePipelineShaderStageInfo(VK_SHADER_STAGE_ANY_HIT_BIT_KHR, anyHitModule),
    };
    VkRayTracingShaderGroupCreateInfoKHR shaderGroups[] = {
        makeGeneralShaderGroup(0),
        makeGeneralShaderGroup(1),
        makeTriangleHitShaderGroup(2, 3),
    };
    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_RAY_TRACING_PIPELINE_STACK_SIZE_KHR};
    VkPipelineDynamicStateCreateInfo dynamicStateInfo =
        makeRayTracingPipelineDynamicStateCreateInfo(dynamicStates, (uint32_t)VKRT_ARRAY_COUNT(dynamicStates));
    VkRayTracingPipelineCreateInfoKHR pipelineCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
        .stageCount = (uint32_t)VKRT_ARRAY_COUNT(shaderStages),
        .pStages = shaderStages,
        .groupCount = (uint32_t)VKRT_ARRAY_COUNT(shaderGroups),
        .pGroups = shaderGroups,
        .maxPipelineRayRecursionDepth = 1,
        .layout = vkrt->core.pipelineLayout,
        .pDynamicState = &dynamicStateInfo,
    };

    if (createRayTracingPipelineTracked(
            vkrt,
            "Selection RT",
            &pipelineCreateInfo,
            &vkrt->core.selectionRayTracingPipeline
        ) != VK_SUCCESS) {
        LOG_ERROR("Failed to create selection ray tracing pipeline");
        goto cleanup;
    }

    uint64_t stackSizeStartTime = getMicroseconds();
    if (storeSelectionRayTracingStackSize(vkrt, vkrt->core.selectionRayTracingPipeline) != VKRT_SUCCESS) {
        vkDestroyPipeline(vkrt->core.device, vkrt->core.selectionRayTracingPipeline, NULL);
        vkrt->core.selectionRayTracingPipeline = VK_NULL_HANDLE;
        goto cleanup;
    }
    logElapsedTraceMs("Selection RT stack size queried", stackSizeStartTime);

    result = VKRT_SUCCESS;
    logSelectionRayTracingPipelineCreated(startTime);

cleanup:
    if (rayGenModule != VK_NULL_HANDLE) vkDestroyShaderModule(vkrt->core.device, rayGenModule, NULL);
    if (missModule != VK_NULL_HANDLE) vkDestroyShaderModule(vkrt->core.device, missModule, NULL);
    if (closestHitModule != VK_NULL_HANDLE) vkDestroyShaderModule(vkrt->core.device, closestHitModule, NULL);
    if (anyHitModule != VK_NULL_HANDLE) vkDestroyShaderModule(vkrt->core.device, anyHitModule, NULL);
    return result;
}
