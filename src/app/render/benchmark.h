#pragma once

#include "cli/cli.h"
#include "vkrt.h"

void benchmarkPrepareLaunchOptions(CLILaunchOptions* options);
int benchmarkRun(VKRT* vkrt, const CLIBenchmarkOptions* options);
