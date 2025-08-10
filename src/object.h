#pragma once
#include "vkrt.h"

void loadObject(VKRT* vkrt, const char* filename);
void addMaterial(VKRT* vkrt, Material* material);
void createUniformBuffer(VKRT* vkrt);
const char* readFile(const char* filename, size_t* fileSize);