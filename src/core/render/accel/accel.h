#pragma once
#include "vkrt_internal.h"

VKRT_Result createShaderBindingTable(VKRT* vkrt);
VKRT_Result createBottomLevelAccelerationStructure(VKRT* vkrt, Mesh* mesh);
VKRT_Result buildAllBLAS(VKRT* vkrt);
VKRT_Result createTopLevelAccelerationStructure(VKRT* vkrt);
