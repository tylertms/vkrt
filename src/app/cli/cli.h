#pragma once

#include "vkrt.h"

#include <stddef.h>
#include <stdint.h>

typedef enum CLIMode {
    CLI_MODE_RUN = 0,
    CLI_MODE_HELP,
    CLI_MODE_VERSION,
} CLIMode;

typedef struct CLIBenchmarkOptions {
    uint8_t enabled;
    uint8_t headless;
    uint32_t width;
    uint32_t height;
    uint32_t targetSamples;
} CLIBenchmarkOptions;

typedef struct CLILaunchOptions {
    CLIMode mode;
    VKRT_CreateInfo createInfo;
    uint8_t loadDefaultScene;
    const char* startupImportPath;
    CLIBenchmarkOptions benchmark;
} CLILaunchOptions;

void CLIDefaultLaunchOptions(CLILaunchOptions* options);
int CLIParseArguments(int argc, char* argv[], CLILaunchOptions* outOptions, char* error, size_t errorSize);
int CLIHandleImmediateMode(const CLILaunchOptions* options, int* outExitCode);
void CLIPrintArgumentError(const char* error);
void CLIPrintVersion(void);
void CLIPrintHelp(void);
