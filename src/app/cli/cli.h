#pragma once

#include <stddef.h>
#include <stdint.h>

#include "vkrt.h"

typedef enum CLIMode {
    CLI_MODE_RUN = 0,
    CLI_MODE_HELP,
    CLI_MODE_VERSION,
} CLIMode;

typedef struct CLILaunchOptions {
    CLIMode mode;
    VKRT_CreateInfo createInfo;
    uint8_t loadDefaultScene;
    const char* startupImportPath;
} CLILaunchOptions;

void CLIDefaultLaunchOptions(CLILaunchOptions* options);
int CLIParseArguments(int argc, char* argv[], CLILaunchOptions* outOptions, char* error, size_t errorSize);
int CLIHandleImmediateMode(const CLILaunchOptions* options, int* outExitCode);
void CLIPrintArgumentError(const char* error);
void CLIPrintVersion(void);
void CLIPrintHelp(void);
