#pragma once
#include "vkrt.h"

void createShaderBindingTable(VKRT* vkrt);
void createBottomLevelAccelerationStructure(VKRT* vkrt, uint32_t meshIndex);
void createTopLevelAccelerationStructure(VKRT* vkrt);