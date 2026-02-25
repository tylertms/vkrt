#pragma once

#include "session.h"
#include "vkrt.h"

void meshControllerImportMesh(VKRT* vkrt, Session* session, const char* path);
void meshControllerApplySessionActions(VKRT* vkrt, Session* session);
void meshControllerLoadDefaultAssets(VKRT* vkrt, Session* session);
