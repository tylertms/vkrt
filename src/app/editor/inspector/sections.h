#pragma once

#include "session.h"
#include "vkrt.h"

void inspectorPrepareRenderState(VKRT* vkrt, Session* session);
void inspectorDrawSceneBrowserSection(VKRT* vkrt, Session* session);
void inspectorDrawSceneOverviewSection(VKRT* vkrt);
void inspectorDrawSelectionTab(VKRT* vkrt, Session* session);
void inspectorDrawCameraTab(VKRT* vkrt);
void inspectorDrawRenderTab(VKRT* vkrt, Session* session);
