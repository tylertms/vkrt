#include "pipeline.h"
#include "shaders.h"
#include "sync.h"
#include "debug.h"

#include <stdio.h>
#include <stdlib.h>

static VKRT_Result createRayTracingPipelineLayout(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    if (vkrt->core.pipelineLayout != VK_NULL_HANDLE) return VKRT_SUCCESS;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {0};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &vkrt->core.descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 0;

    if (vkCreatePipelineLayout(vkrt->core.device, &pipelineLayoutInfo, NULL, &vkrt->core.pipelineLayout) != VK_SUCCESS) {
        LOG_ERROR("Failed to create pipeline layout");
        return VKRT_ERROR_PIPELINE_CREATION_FAILED;
    }
    return VKRT_SUCCESS;
}

VKRT_Result createRayTracingPipeline(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    uint64_t startTime = getMicroseconds();
    VkBool32 useSerShaders = vkrtSerEnabled(vkrt);
    const unsigned char* rayGenData = useSerShaders ? shaderRgenSerData : shaderRgenData;
    const unsigned char* closestHitData = useSerShaders ? shaderRchitSerData : shaderRchitData;
    const unsigned char* missData = useSerShaders ? shaderRmissSerData : shaderRmissData;
    const unsigned char* shadowMissData = useSerShaders ? shaderShadowMissSerData : shaderShadowMissData;
    size_t rayGenSize = useSerShaders ? shaderRgenSerSize : shaderRgenSize;
    size_t closestHitSize = useSerShaders ? shaderRchitSerSize : shaderRchitSize;
    size_t missSize = useSerShaders ? shaderRmissSerSize : shaderRmissSize;
    size_t shadowMissSize = useSerShaders ? shaderShadowMissSerSize : shaderShadowMissSize;

    if (createRayTracingPipelineLayout(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;

    VkShaderModule rayGenModule = VK_NULL_HANDLE;
    VkShaderModule closestHitModule = VK_NULL_HANDLE;
    VkShaderModule missModule = VK_NULL_HANDLE;
    VkShaderModule shadowMissModule = VK_NULL_HANDLE;
    if (createShaderModule(vkrt, (const char*)rayGenData, rayGenSize, &rayGenModule) != VKRT_SUCCESS ||
        createShaderModule(vkrt, (const char*)closestHitData, closestHitSize, &closestHitModule) != VKRT_SUCCESS ||
        createShaderModule(vkrt, (const char*)missData, missSize, &missModule) != VKRT_SUCCESS ||
        createShaderModule(vkrt, (const char*)shadowMissData, shadowMissSize, &shadowMissModule) != VKRT_SUCCESS) {
        if (rayGenModule != VK_NULL_HANDLE) vkDestroyShaderModule(vkrt->core.device, rayGenModule, NULL);
        if (closestHitModule != VK_NULL_HANDLE) vkDestroyShaderModule(vkrt->core.device, closestHitModule, NULL);
        if (missModule != VK_NULL_HANDLE) vkDestroyShaderModule(vkrt->core.device, missModule, NULL);
        if (shadowMissModule != VK_NULL_HANDLE) vkDestroyShaderModule(vkrt->core.device, shadowMissModule, NULL);
        return VKRT_ERROR_SHADER_COMPILATION_FAILED;
    }

    LOG_INFO(
        "Using embedded ray tracing shaders (%s variant, rgen: %zu bytes, rchit: %zu bytes, rmiss: %zu bytes, rmissShadow: %zu bytes)",
        useSerShaders ? "SER" : "default",
        rayGenSize,
        closestHitSize,
        missSize,
        shadowMissSize
    );

    VkPipelineShaderStageCreateInfo rayGenStageInfo = {0};
    rayGenStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    rayGenStageInfo.stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    rayGenStageInfo.module = rayGenModule;
    rayGenStageInfo.pName = "main";

    VkRayTracingShaderGroupCreateInfoKHR rayGenShaderGroup = {0};
    rayGenShaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    rayGenShaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    rayGenShaderGroup.generalShader = 0;
    rayGenShaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
    rayGenShaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
    rayGenShaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;

    VkPipelineShaderStageCreateInfo missStageInfo = {0};
    missStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    missStageInfo.stage = VK_SHADER_STAGE_MISS_BIT_KHR;
    missStageInfo.module = missModule;
    missStageInfo.pName = "main";

    VkRayTracingShaderGroupCreateInfoKHR missShaderGroup = {0};
    missShaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    missShaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    missShaderGroup.generalShader = 1;
    missShaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
    missShaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
    missShaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;

    VkPipelineShaderStageCreateInfo shadowMissStageInfo = {0};
    shadowMissStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shadowMissStageInfo.stage = VK_SHADER_STAGE_MISS_BIT_KHR;
    shadowMissStageInfo.module = shadowMissModule;
    shadowMissStageInfo.pName = "main";

    VkRayTracingShaderGroupCreateInfoKHR shadowMissShaderGroup = {0};
    shadowMissShaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    shadowMissShaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    shadowMissShaderGroup.generalShader = 2;
    shadowMissShaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
    shadowMissShaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
    shadowMissShaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;

    VkPipelineShaderStageCreateInfo closestHitStageInfo = {0};
    closestHitStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    closestHitStageInfo.stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    closestHitStageInfo.module = closestHitModule;
    closestHitStageInfo.pName = "main";

    VkRayTracingShaderGroupCreateInfoKHR closestHitShaderGroup = {0};
    closestHitShaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    closestHitShaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
    closestHitShaderGroup.generalShader = VK_SHADER_UNUSED_KHR;
    closestHitShaderGroup.closestHitShader = 3;
    closestHitShaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
    closestHitShaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        rayGenStageInfo,
        missStageInfo,
        shadowMissStageInfo,
        closestHitStageInfo
    };

    VkRayTracingShaderGroupCreateInfoKHR shaderGroups[] = {
        rayGenShaderGroup,
        missShaderGroup,
        shadowMissShaderGroup,
        closestHitShaderGroup
    };

    VkRayTracingPipelineCreateInfoKHR pipelineCreateInfo = {0};
    pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
    pipelineCreateInfo.stageCount = VKRT_ARRAY_COUNT(shaderStages);
    pipelineCreateInfo.pStages = shaderStages;
    pipelineCreateInfo.groupCount = VKRT_ARRAY_COUNT(shaderGroups);
    pipelineCreateInfo.pGroups = shaderGroups;
    pipelineCreateInfo.maxPipelineRayRecursionDepth = 1;
    pipelineCreateInfo.layout = vkrt->core.pipelineLayout;

    if (vkrt->core.procs.vkCreateRayTracingPipelinesKHR(vkrt->core.device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipelineCreateInfo, NULL, &vkrt->core.rayTracingPipeline) != VK_SUCCESS) {
        LOG_ERROR("Failed to create ray tracing pipeline");
        vkDestroyShaderModule(vkrt->core.device, rayGenModule, NULL);
        vkDestroyShaderModule(vkrt->core.device, closestHitModule, NULL);
        vkDestroyShaderModule(vkrt->core.device, missModule, NULL);
        vkDestroyShaderModule(vkrt->core.device, shadowMissModule, NULL);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    vkDestroyShaderModule(vkrt->core.device, rayGenModule, NULL);
    vkDestroyShaderModule(vkrt->core.device, closestHitModule, NULL);
    vkDestroyShaderModule(vkrt->core.device, missModule, NULL);
    vkDestroyShaderModule(vkrt->core.device, shadowMissModule, NULL);

    LOG_INFO(
        "Ray tracing pipeline created. Shader Stages: %u, Shader Groups: %u, in %.3f ms",
        (uint32_t)VKRT_ARRAY_COUNT(shaderStages),
        (uint32_t)VKRT_ARRAY_COUNT(shaderGroups),
        (double)(getMicroseconds() - startTime) / 1e3
    );
    return VKRT_SUCCESS;
}

#if VKRT_SELECTION_ENABLED
VKRT_Result createSelectionRayTracingPipeline(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    if (createRayTracingPipelineLayout(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;

    uint64_t startTime = getMicroseconds();
    VKRT_Result result = VKRT_ERROR_OPERATION_FAILED;
    VkShaderModule rayGenModule = VK_NULL_HANDLE;
    VkShaderModule missModule = VK_NULL_HANDLE;
    VkShaderModule closestHitModule = VK_NULL_HANDLE;

    if (createShaderModule(vkrt, (const char*)shaderSelectRgenData, shaderSelectRgenSize, &rayGenModule) != VKRT_SUCCESS ||
        createShaderModule(vkrt, (const char*)shaderSelectRmissData, shaderSelectRmissSize, &missModule) != VKRT_SUCCESS ||
        createShaderModule(vkrt, (const char*)shaderSelectRchitData, shaderSelectRchitSize, &closestHitModule) != VKRT_SUCCESS) {
        goto cleanup;
    }

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR, .module = rayGenModule, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_MISS_BIT_KHR, .module = missModule, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, .module = closestHitModule, .pName = "main"},
    };

    VkRayTracingShaderGroupCreateInfoKHR shaderGroups[3] = {
        {.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR, .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
         .generalShader = 0, .closestHitShader = VK_SHADER_UNUSED_KHR, .anyHitShader = VK_SHADER_UNUSED_KHR, .intersectionShader = VK_SHADER_UNUSED_KHR},
        {.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR, .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
         .generalShader = 1, .closestHitShader = VK_SHADER_UNUSED_KHR, .anyHitShader = VK_SHADER_UNUSED_KHR, .intersectionShader = VK_SHADER_UNUSED_KHR},
        {.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR, .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
         .generalShader = VK_SHADER_UNUSED_KHR, .closestHitShader = 2, .anyHitShader = VK_SHADER_UNUSED_KHR, .intersectionShader = VK_SHADER_UNUSED_KHR},
    };

    VkRayTracingPipelineCreateInfoKHR pipelineCreateInfo = {0};
    pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
    pipelineCreateInfo.stageCount = VKRT_ARRAY_COUNT(shaderStages);
    pipelineCreateInfo.pStages = shaderStages;
    pipelineCreateInfo.groupCount = VKRT_ARRAY_COUNT(shaderGroups);
    pipelineCreateInfo.pGroups = shaderGroups;
    pipelineCreateInfo.maxPipelineRayRecursionDepth = 1;
    pipelineCreateInfo.layout = vkrt->core.pipelineLayout;

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

    result = VKRT_SUCCESS;
    LOG_INFO(
        "Selection ray tracing pipeline created in %.3f ms",
        (double)(getMicroseconds() - startTime) / 1e3
    );

cleanup:
    if (rayGenModule != VK_NULL_HANDLE) vkDestroyShaderModule(vkrt->core.device, rayGenModule, NULL);
    if (missModule != VK_NULL_HANDLE) vkDestroyShaderModule(vkrt->core.device, missModule, NULL);
    if (closestHitModule != VK_NULL_HANDLE) vkDestroyShaderModule(vkrt->core.device, closestHitModule, NULL);
    return result;
}

VKRT_Result createComputePipeline(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    uint64_t startTime = getMicroseconds();

    VkShaderModule compModule = VK_NULL_HANDLE;
    if (createShaderModule(vkrt, (const char*)shaderCompData, shaderCompSize, &compModule) != VKRT_SUCCESS) {
        return VKRT_ERROR_OPERATION_FAILED;
    }

    VkPipelineShaderStageCreateInfo stageInfo = {0};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = compModule;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo createInfo = {0};
    createInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    createInfo.stage = stageInfo;
    createInfo.layout = vkrt->core.pipelineLayout;

    if (vkCreateComputePipelines(vkrt->core.device, VK_NULL_HANDLE, 1, &createInfo, NULL, &vkrt->core.computePipeline) != VK_SUCCESS) {
        LOG_ERROR("Failed to create compute pipeline");
        vkDestroyShaderModule(vkrt->core.device, compModule, NULL);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    vkDestroyShaderModule(vkrt->core.device, compModule, NULL);

    LOG_INFO(
        "Compute pipeline created in %.3f ms",
        (double)(getMicroseconds() - startTime) / 1e3
    );
    return VKRT_SUCCESS;
}
#endif

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

    if (resetRenderFinishedSemaphores(
        vkrt,
        vkrt->runtime.swapChainImageCount,
        vkrt->runtime.swapChainImageCount
    ) != VKRT_SUCCESS) {
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

VKRT_Result createShaderModule(VKRT* vkrt, const char* spirv, size_t length, VkShaderModule* outShaderModule) {
    if (!vkrt || !spirv || !outShaderModule) return VKRT_ERROR_INVALID_ARGUMENT;

    VkShaderModuleCreateInfo shaderModuleCreateInfo = {0};
    shaderModuleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderModuleCreateInfo.codeSize = length;
    shaderModuleCreateInfo.pCode = (const uint32_t*)spirv;

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(vkrt->core.device, &shaderModuleCreateInfo, NULL, &shaderModule) != VK_SUCCESS) {
        LOG_ERROR("Failed to create shader module");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    *outShaderModule = shaderModule;
    return VKRT_SUCCESS;
}

VKRT_Result createRenderPass(VKRT* vkrt, VkRenderPass* outRenderPass) {
    if (!vkrt || !outRenderPass) return VKRT_ERROR_INVALID_ARGUMENT;

    VkAttachmentDescription colorAttachment = {0};
    colorAttachment.format = vkrt->runtime.swapChainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef = {0};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {0};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkSubpassDependency dependency = {0};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    dependency.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassCreateInfo = {0};
    renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassCreateInfo.attachmentCount = 1;
    renderPassCreateInfo.pAttachments = &colorAttachment;
    renderPassCreateInfo.subpassCount = 1;
    renderPassCreateInfo.pSubpasses = &subpass;
    renderPassCreateInfo.dependencyCount = 1;
    renderPassCreateInfo.pDependencies = &dependency;

    VkRenderPass renderPass = VK_NULL_HANDLE;
    if (vkCreateRenderPass(vkrt->core.device, &renderPassCreateInfo, NULL, &renderPass) != VK_SUCCESS) {
        LOG_ERROR("Failed to create UI render pass");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    *outRenderPass = renderPass;
    return VKRT_SUCCESS;
}
