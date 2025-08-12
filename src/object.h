#pragma once
#include "vkrt.h"

void loadObject(VKRT* vkrt, const char* filename);
const char* readFile(const char* filename, size_t* fileSize);