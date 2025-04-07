#include "pipeline.h"
#include <stdlib.h>
#include <stdio.h>

const char* readFile(const char* filename, size_t* fileSize) {
    FILE* file = fopen(filename, "rb");
    if (file == NULL) {
        perror("ERROR: Failed to open file");
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    *fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);

    char* buffer = (char*)malloc(*fileSize);
    if (buffer == NULL) {
        perror("ERROR: Failed to allocate memory!");
        fclose(file);
        return NULL;
    }

    fread(buffer, 1, *fileSize, file);
    fclose(file);

    return buffer;
}

void createRayTracingPipeline(VKRT* vkrt) {
    size_t rayGenLen;
    const char* rayGenCode = readFile("./shaders/rgen.spv", &rayGenLen);
    size_t closestHitLen;
    const char* closestHitCode = readFile("./shaders/rchit.spv", &closestHitLen);
    size_t missLen;
    const char* missCode = readFile("./shaders/rmiss.spv", &missLen);

    VkShaderModule rayGenModule = createShaderModule(vkrt, rayGenCode, rayGenLen);
    VkShaderModule closestHitModule = createShaderModule(vkrt, closestHitCode, closestHitLen);
    VkShaderModule missModule = createShaderModule(vkrt, missCode, missLen);

    VkPipelineShaderStageCreateInfo rayGenStageInfo = {0};
    rayGenStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    rayGenStageInfo.stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    rayGenStageInfo.module = rayGenModule;
    rayGenStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo closestHitStageInfo = {0};
    closestHitStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    closestHitStageInfo.stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    closestHitStageInfo.module = closestHitModule;
    closestHitStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo missStageInfo = {0};
    missStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    missStageInfo.stage = VK_SHADER_STAGE_MISS_BIT_KHR;
    missStageInfo.module = missModule;
    missStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        rayGenStageInfo,
        closestHitStageInfo,
        missStageInfo
    };

    VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState = {0};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = (uint32_t)COUNT_OF(dynamicStates);
    dynamicState.pDynamicStates = dynamicStates;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {0};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 0;
    pipelineLayoutInfo.pushConstantRangeCount = 0;

    if (vkCreatePipelineLayout(vkrt->device, &pipelineLayoutInfo, NULL, &vkrt->pipelineLayout) != VK_SUCCESS) {
        perror("ERROR: Failed to create pipeline layout");
        exit(EXIT_FAILURE);
    }

    VkRayTracingPipelineCreateInfoKHR pipelineCreateInfo = {0};
    pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
    pipelineCreateInfo.maxPipelineRayRecursionDepth = 1;
    pipelineCreateInfo.stageCount = 3;
    pipelineCreateInfo.pStages = shaderStages;
    pipelineCreateInfo.pDynamicState = &dynamicState;
    pipelineCreateInfo.layout = vkrt->pipelineLayout;
    pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineCreateInfo.basePipelineIndex = -1;

    PFN_vkCreateRayTracingPipelinesKHR pvkCreateRayTracingPipelinesKHR =
    (PFN_vkCreateRayTracingPipelinesKHR)vkGetDeviceProcAddr(
        vkrt->device, "vkCreateRayTracingPipelinesKHR");

    if (pvkCreateRayTracingPipelinesKHR(vkrt->device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipelineCreateInfo, NULL, &vkrt->rayTracingPipeline) != VK_SUCCESS) {
        perror("ERROR: Failed to create ray tracing pipeline");
        exit(EXIT_FAILURE);
    }

    vkDestroyShaderModule(vkrt->device, rayGenModule, NULL);
    vkDestroyShaderModule(vkrt->device, closestHitModule, NULL);
    vkDestroyShaderModule(vkrt->device, missModule, NULL);
}

void createRenderPass(VKRT* vkrt) {
    VkAttachmentDescription colorAttachment = {0};
    colorAttachment.format = vkrt->swapChainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef = {0};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {0};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency = {0};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstSubpass = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo = {0};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(vkrt->device, &renderPassInfo, NULL, &vkrt->renderPass) != VK_SUCCESS) {
        printf("ERROR: Failed to create render pass!\n");
        exit(EXIT_FAILURE);
    }
}

void createSyncObjects(VKRT* vkrt) {
    VkSemaphoreCreateInfo semaphoreInfo = {0};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo = {0};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    VkResult imageAvailableSemaphoreResult = vkCreateSemaphore(vkrt->device, &semaphoreInfo, NULL, &vkrt->imageAvailableSemaphore);
    VkResult renderFinishedSemaphoreResult = vkCreateSemaphore(vkrt->device, &semaphoreInfo, NULL, &vkrt->renderFinishedSemaphore);
    VkResult inFlightFenceResult = vkCreateFence(vkrt->device, &fenceInfo, NULL, &vkrt->inFlightFence);

    if (imageAvailableSemaphoreResult != VK_SUCCESS || renderFinishedSemaphoreResult != VK_SUCCESS || inFlightFenceResult != VK_SUCCESS) {
        perror("ERROR: Failed to create sync objects");
        exit(EXIT_FAILURE);
    }
}

VkShaderModule createShaderModule(VKRT* vkrt, const char* spirv, size_t length) {
    VkShaderModuleCreateInfo createInfo = {0};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = length;
    createInfo.pCode = (const uint32_t*)spirv;

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(vkrt->device, &createInfo, NULL, &shaderModule) != VK_SUCCESS) {
        perror("ERROR: Failed to create shader module");
        exit(EXIT_FAILURE);
    }

    return shaderModule;
}