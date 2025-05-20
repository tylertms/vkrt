#include "pipeline.h"
#include "object.h"

#include <stdio.h>
#include <stdlib.h>

void createRayTracingPipeline(VKRT* vkrt) {
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {0};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &vkrt->descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 0;

    if (vkCreatePipelineLayout(vkrt->device, &pipelineLayoutInfo, NULL, &vkrt->pipelineLayout) != VK_SUCCESS) {
        perror("ERROR: Failed to create pipeline layout");
        exit(EXIT_FAILURE);
    }

    size_t rayGenLen, closestHitLen, missLen;

    const char* rayGenCode = readFile("./rgen.spv", &rayGenLen);
    const char* closestHitCode = readFile("./rchit.spv", &closestHitLen);
    const char* missCode = readFile("./rmiss.spv", &missLen);

    VkShaderModule rayGenModule = createShaderModule(vkrt, rayGenCode, rayGenLen);
    VkShaderModule closestHitModule = createShaderModule(vkrt, closestHitCode, closestHitLen);
    VkShaderModule missModule = createShaderModule(vkrt, missCode, missLen);

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

    VkPipelineShaderStageCreateInfo closestHitStageInfo = {0};
    closestHitStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    closestHitStageInfo.stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    closestHitStageInfo.module = closestHitModule;
    closestHitStageInfo.pName = "main";

    VkRayTracingShaderGroupCreateInfoKHR closestHitShaderGroup = {0};
    closestHitShaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    closestHitShaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
    closestHitShaderGroup.generalShader = VK_SHADER_UNUSED_KHR;
    closestHitShaderGroup.closestHitShader = 2;
    closestHitShaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
    closestHitShaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        rayGenStageInfo,
        missStageInfo,
        closestHitStageInfo};

    VkRayTracingShaderGroupCreateInfoKHR shaderGroups[] = {
        rayGenShaderGroup,
        missShaderGroup,
        closestHitShaderGroup};

    VkRayTracingPipelineCreateInfoKHR pipelineCreateInfo = {0};
    pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
    pipelineCreateInfo.stageCount = COUNT_OF(shaderStages);
    pipelineCreateInfo.pStages = shaderStages;
    pipelineCreateInfo.groupCount = COUNT_OF(shaderGroups);
    pipelineCreateInfo.pGroups = shaderGroups;
    pipelineCreateInfo.maxPipelineRayRecursionDepth = 1;
    pipelineCreateInfo.layout = vkrt->pipelineLayout;

    PFN_vkCreateRayTracingPipelinesKHR pvkCreateRayTracingPipelinesKHR = (PFN_vkCreateRayTracingPipelinesKHR)vkGetDeviceProcAddr(vkrt->device, "vkCreateRayTracingPipelinesKHR");

    if (pvkCreateRayTracingPipelinesKHR(vkrt->device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipelineCreateInfo, NULL, &vkrt->rayTracingPipeline) != VK_SUCCESS) {
        perror("ERROR: Failed to create ray tracing pipeline");
        exit(EXIT_FAILURE);
    }

    vkDestroyShaderModule(vkrt->device, rayGenModule, NULL);
    vkDestroyShaderModule(vkrt->device, closestHitModule, NULL);
    vkDestroyShaderModule(vkrt->device, missModule, NULL);
}

void createSyncObjects(VKRT* vkrt) {
    VkSemaphoreCreateInfo semaphoreCreateInfo = {0};
    semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo = {0};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkResult imageAvailableSemaphoreResult = vkCreateSemaphore(vkrt->device, &semaphoreCreateInfo, NULL, &vkrt->imageAvailableSemaphores[i]);
        VkResult renderFinishedSemaphoreResult = vkCreateSemaphore(vkrt->device, &semaphoreCreateInfo, NULL, &vkrt->renderFinishedSemaphores[i]);
        VkResult inFlightFenceResult = vkCreateFence(vkrt->device, &fenceInfo, NULL, &vkrt->inFlightFences[i]);

        if (imageAvailableSemaphoreResult != VK_SUCCESS || renderFinishedSemaphoreResult != VK_SUCCESS || inFlightFenceResult != VK_SUCCESS) {
            perror("ERROR: Failed to create sync objects");
            exit(EXIT_FAILURE);
        }
    }
}

VkShaderModule createShaderModule(VKRT* vkrt, const char* spirv, size_t length) {
    VkShaderModuleCreateInfo shaderModuleCreateInfo = {0};
    shaderModuleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderModuleCreateInfo.codeSize = length;
    shaderModuleCreateInfo.pCode = (const uint32_t*)spirv;

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(vkrt->device, &shaderModuleCreateInfo, NULL, &shaderModule) != VK_SUCCESS) {
        perror("ERROR: Failed to create shader module");
        exit(EXIT_FAILURE);
    }

    return shaderModule;
}

void createRenderPass(VKRT* vkrt) {
    VkAttachmentDescription colorAttachment = {0};
    colorAttachment.format = vkrt->swapChainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef = {0};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    VkSubpassDescription subpass = {0};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkSubpassDependency dependency = {0};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
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

    if (vkCreateRenderPass(vkrt->device, &renderPassCreateInfo, NULL, &vkrt->renderPass) != VK_SUCCESS) {
        perror("ERROR: Failed to create UI render pass");
        exit(EXIT_FAILURE);
    }
}