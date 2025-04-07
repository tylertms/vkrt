#pragma once
#include "vkrt.h"

void createRayTracingPipeline(VKRT* vkrt);
VkShaderModule createShaderModule(VKRT* vkrt, const char* spirv, size_t length);