#pragma once
#include "vkrt.h"

void createRayTracingPipeline(VKRT* vkrt);
void createSyncObjects(VKRT* vkrt);
VkShaderModule createShaderModule(VKRT* vkrt, const char* spirv, size_t length);
const char* readFile(const char* filename, size_t* fileSize);