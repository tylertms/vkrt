#pragma once
#include "vkrt_internal.h"

VKRT_Result createShaderBindingTable(VKRT* vkrt);
VKRT_Result createBottomLevelAccelerationStructureForGeometry(
    VKRT* vkrt,
    const MeshInfo* meshInfo,
    VkDeviceAddress vertexDataAddress,
    VkDeviceAddress indexDataAddress,
    AccelerationStructure* outAccelerationStructure);
VKRT_Result createTopLevelAccelerationStructure(VKRT* vkrt);
VKRT_Result prepareBottomLevelAccelerationStructureBuilds(VKRT* vkrt);
VKRT_Result recordBottomLevelAccelerationStructureBuilds(VKRT* vkrt, VkCommandBuffer commandBuffer);
VKRT_Result recordTopLevelAccelerationStructureBuild(VKRT* vkrt, VkCommandBuffer commandBuffer);
