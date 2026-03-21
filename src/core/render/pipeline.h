#pragma once

#include "vkrt_internal.h"

VKRT_Result createRayTracingPipeline(VKRT* vkrt);
VKRT_Result createSelectionRayTracingPipeline(VKRT* vkrt);
VKRT_Result createComputePipeline(VKRT* vkrt);
VKRT_Result createSyncObjects(VKRT* vkrt);
VKRT_Result createShaderModule(VKRT* vkrt, const uint32_t* spirv, size_t length, VkShaderModule* outShaderModule);
