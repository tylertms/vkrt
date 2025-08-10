#pragma once
#include "main.h"

void loadObject(VKRT* vkrt, const char* filename);
void createUniformBuffer(VKRT* vkrt);
const char* readFile(const char* filename, size_t* fileSize);