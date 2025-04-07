#include "pipeline.h"
#include <stdlib.h>
#include <stdio.h>

const char* readFile(const char* filename, size_t* fileSize) {
    FILE* file = fopen(filename, "rb");
    if (file == NULL) {
        perror("ERROR: Failed to open file!");
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
    const char* rayGenCode = readFile("shaders/rayGen.glsl", &rayGenLen);
    size_t closestHitLen;
    const char* closestHitCode = readFile("shaders/closestHit.glsl", &closestHitLen);
    size_t missLen;
    const char* missCode = readFile("shaders/miss.glsl", &missLen);

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

    VkPipelineViewportStateCreateInfo viewportState = {0};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {0};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 0;
    pipelineLayoutInfo.pushConstantRangeCount = 0;

    if (vkCreatePipelineLayout(vkrt->device, &pipelineLayoutInfo, NULL, &vkrt->pipelineLayout) != VK_SUCCESS) {
        printf("ERROR: Failed to create pipeline layout!\n");
        exit(EXIT_FAILURE);
    }

    VkRayTracingPipelineCreateInfoKHR rayTracingPipelineCreateInfo = {0};
    rayTracingPipelineCreateInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
    rayTracingPipelineCreateInfo.stageCount = 3;
    rayTracingPipelineCreateInfo.maxPipelineRayRecursionDepth = 1;

    vkDestroyShaderModule(vkrt->device, rayGenModule, NULL);
    vkDestroyShaderModule(vkrt->device, closestHitModule, NULL);
    vkDestroyShaderModule(vkrt->device, missModule, NULL);
}

VkShaderModule createShaderModule(VKRT* vkrt, const char* spirv, size_t length) {
    VkShaderModuleCreateInfo createInfo = {0};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = length;
    createInfo.pCode = (const uint32_t*)spirv;

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(vkrt->device, &createInfo, NULL, &shaderModule) != VK_SUCCESS) {
        printf("ERROR: Failed to create shader module!\n");
        exit(EXIT_FAILURE);
    }

    return shaderModule;
}