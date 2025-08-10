#pragma once
#include "main.h"

void createShaderBindingTable(VKRT* vkrt);
void createBottomLevelAccelerationStructure(VKRT* vkrt, Mesh* mesh);
void createTopLevelAccelerationStructure(VKRT* vkrt);