#pragma once

#include "cli/cli.h"
#include "vkrt.h"

int offlineRenderRun(VKRT* vkrt, const CLIOfflineRenderOptions* options);
void offlineRenderPrepareLaunchOptions(CLILaunchOptions* options);
int offlineRenderSaveOutput(VKRT* vkrt, const char* outputPath);
