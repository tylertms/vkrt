#pragma once

#include "vkrt_internal.h"

int saveCurrentRenderImage(VKRT* vkrt, const char* path);
int saveCurrentRenderImageEx(VKRT* vkrt, const char* path, const VKRT_RenderExportSettings* settings);
int denoiseCurrentRenderToViewport(VKRT* vkrt);
void processPendingViewportDenoise(VKRT* vkrt);
void syncCompletedViewportDenoise(VKRT* vkrt);
void shutdownRenderImageExporter(VKRT* vkrt);
