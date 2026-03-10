#pragma once

#include "vkrt_internal.h"

VKRT_Result createRayTracingPipeline(VKRT* vkrt);
#if VKRT_SELECTION_ENABLED
VKRT_Result createSelectionRayTracingPipeline(VKRT* vkrt);
VKRT_Result createComputePipeline(VKRT* vkrt);
#endif
VKRT_Result createSyncObjects(VKRT* vkrt);
VKRT_Result createShaderModule(VKRT* vkrt, const char* spirv, size_t length, VkShaderModule* outShaderModule);
VKRT_Result createRenderPass(VKRT* vkrt);
