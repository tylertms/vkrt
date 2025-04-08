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
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {0};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &vkrt->descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 0;

    if (vkCreatePipelineLayout(vkrt->device, &pipelineLayoutInfo, NULL, &vkrt->pipelineLayout) != VK_SUCCESS) {
        perror("ERROR: Failed to create pipeline layout");
        exit(EXIT_FAILURE);
    }

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
        closestHitStageInfo
    };

    VkRayTracingShaderGroupCreateInfoKHR shaderGroups[] = {
        rayGenShaderGroup,
        missShaderGroup,
        closestHitShaderGroup
    };

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
    VkSemaphoreCreateInfo semaphoreInfo = {0};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo = {0};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkResult imageAvailableSemaphoreResult = vkCreateSemaphore(vkrt->device, &semaphoreInfo, NULL, &vkrt->imageAvailableSemaphores[i]);
        VkResult renderFinishedSemaphoreResult = vkCreateSemaphore(vkrt->device, &semaphoreInfo, NULL, &vkrt->renderFinishedSemaphores[i]);
        VkResult inFlightFenceResult = vkCreateFence(vkrt->device, &fenceInfo, NULL, &vkrt->inFlightFences[i]);

        if (imageAvailableSemaphoreResult != VK_SUCCESS || renderFinishedSemaphoreResult != VK_SUCCESS || inFlightFenceResult != VK_SUCCESS) {
            perror("ERROR: Failed to create sync objects");
            exit(EXIT_FAILURE);
        }
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