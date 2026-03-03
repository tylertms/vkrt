#pragma once
#include "vkrt.h"

void createShaderBindingTable(VKRT* vkrt);
void createBottomLevelAccelerationStructure(VKRT* vkrt, Mesh* mesh);
void buildAllBLAS(VKRT* vkrt);
void createTopLevelAccelerationStructure(VKRT* vkrt);
