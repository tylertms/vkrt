#pragma once
#include "vkrt.h"

void loadObject(VKRT* vkrt, const char* filename);
VkTransformMatrixKHR meshTransformTLAS(MeshInfo* meshInfo);
void createUniformBuffer(VKRT* vkrt);
const char* readFile(const char* filename, size_t* fileSize);