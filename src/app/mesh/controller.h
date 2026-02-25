#pragma once

#include "session.h"
#include "vkrt.h"

int meshControllerImportMesh(VKRT* vkrt, Session* session, const char* path, const char* importName, uint32_t* outMeshIndex);
void meshControllerApplySessionActions(VKRT* vkrt, Session* session);
void meshControllerLoadDefaultAssets(VKRT* vkrt, Session* session);
