#pragma once

#include "vkrt_internal.h"

VKRT_Result createRayTracingPipeline(VKRT* vkrt);
VKRT_Result createSyncObjects(VKRT* vkrt);
VKRT_Result createShaderModule(VKRT* vkrt, const char* spirv, size_t length, VkShaderModule* outShaderModule);
VKRT_Result createRenderPass(VKRT* vkrt);
