#include "cli.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>
#include <spng.h>

#ifndef VKRT_VERSION
#define VKRT_VERSION "dev"
#endif

enum {
    kPresentModeAdaptive = (int)VKRT_PRESENT_MODE_ADAPTIVE,
    kPresentModeVSync = (int)VKRT_PRESENT_MODE_VSYNC,
    kPresentModeMailbox = (int)VKRT_PRESENT_MODE_MAILBOX,
    kPresentModeImmediate = (int)VKRT_PRESENT_MODE_IMMEDIATE,
};

static int stringsEqual(const char* lhs, const char* rhs) {
    if (!lhs || !rhs) return 0;
    return strcmp(lhs, rhs) == 0;
}

static int optionMatches(const char* arg, const char* optionName) {
    if (!arg || !optionName) return 0;
    if (stringsEqual(arg, optionName)) return 1;

    size_t optionLength = strlen(optionName);
    return strncmp(arg, optionName, optionLength) == 0 && arg[optionLength] == '=';
}

static const char* requireOptionValue(
    int argc,
    char* argv[],
    int* inOutIndex,
    const char* optionName,
    char* error,
    size_t errorSize
) {
    if (!inOutIndex || !optionName || !error || errorSize == 0) return NULL;

    const char* arg = argv[*inOutIndex];
    size_t optionLength = strlen(optionName);
    if (strncmp(arg, optionName, optionLength) == 0 && arg[optionLength] == '=') {
        const char* inlineValue = arg + optionLength + 1;
        if (inlineValue[0]) return inlineValue;

        snprintf(error, errorSize, "Missing value for %s", optionName);
        return NULL;
    }

    if (*inOutIndex + 1 >= argc) {
        snprintf(error, errorSize, "Missing value for %s", optionName);
        return NULL;
    }

    (*inOutIndex)++;
    return argv[*inOutIndex];
}

static int parseUnsignedValue(
    const char* text,
    uint32_t* outValue,
    const char* optionName,
    char* error,
    size_t errorSize
) {
    if (!text || !outValue || !optionName || !error || errorSize == 0) return 0;

    errno = 0;
    char* end = NULL;
    unsigned long parsed = strtoul(text, &end, 10);
    if (errno != 0 || end == text || (end && end[0] != '\0') || parsed == 0 || parsed > UINT32_MAX) {
        snprintf(error, errorSize, "Invalid value for %s: %s", optionName, text);
        return 0;
    }

    *outValue = (uint32_t)parsed;
    return 1;
}

static int parseDeviceIndexValue(
    const char* text,
    int32_t* outValue,
    char* error,
    size_t errorSize
) {
    if (!text || !outValue || !error || errorSize == 0) return 0;

    errno = 0;
    char* end = NULL;
    long parsed = strtol(text, &end, 10);
    if (errno != 0 || end == text || (end && end[0] != '\0') || parsed < 0 || parsed > INT32_MAX) {
        snprintf(error, errorSize, "Invalid value for --device-index: %s", text);
        return 0;
    }

    *outValue = (int32_t)parsed;
    return 1;
}

static int parsePresentModeValue(
    const char* text,
    VKRT_PresentModePreference* outValue,
    char* error,
    size_t errorSize
) {
    if (!text || !outValue || !error || errorSize == 0) return 0;

    errno = 0;
    char* end = NULL;
    long parsed = strtol(text, &end, 10);
    if (errno != 0 || end == text || (end && end[0] != '\0')) {
        snprintf(error, errorSize, "Invalid value for --vsync-mode: %s", text);
        return 0;
    }

    switch ((int)parsed) {
    case kPresentModeAdaptive:
        *outValue = VKRT_PRESENT_MODE_ADAPTIVE;
        return 1;
    case kPresentModeVSync:
        *outValue = VKRT_PRESENT_MODE_VSYNC;
        return 1;
    case kPresentModeMailbox:
        *outValue = VKRT_PRESENT_MODE_MAILBOX;
        return 1;
    case kPresentModeImmediate:
        *outValue = VKRT_PRESENT_MODE_IMMEDIATE;
        return 1;
    default:
        snprintf(error, errorSize, "Invalid value for --vsync-mode: %s", text);
        return 0;
    }
}

void CLIDefaultLaunchOptions(CLILaunchOptions* options) {
    if (!options) return;

    memset(options, 0, sizeof(*options));
    options->mode = CLI_MODE_RUN;
    options->loadDefaultScene = 1u;
    VKRT_defaultCreateInfo(&options->createInfo);
}

int CLIParseArguments(int argc, char* argv[], CLILaunchOptions* outOptions, char* error, size_t errorSize) {
    if (!outOptions || !error || errorSize == 0) return 0;

    CLIDefaultLaunchOptions(outOptions);
    error[0] = '\0';

    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];
        if (!arg || !arg[0]) continue;

        if (stringsEqual(arg, "--help") || stringsEqual(arg, "-h")) {
            outOptions->mode = CLI_MODE_HELP;
            return 1;
        }
        if (stringsEqual(arg, "--version") || stringsEqual(arg, "-v")) {
            outOptions->mode = CLI_MODE_VERSION;
            return 1;
        }

        if (optionMatches(arg, "--width")) {
            const char* value = requireOptionValue(argc, argv, &i, "--width", error, errorSize);
            if (!value) return 0;
            if (!parseUnsignedValue(value, &outOptions->createInfo.width, "--width", error, errorSize)) {
                return 0;
            }
            continue;
        }
        if (optionMatches(arg, "--height")) {
            const char* value = requireOptionValue(argc, argv, &i, "--height", error, errorSize);
            if (!value) return 0;
            if (!parseUnsignedValue(value, &outOptions->createInfo.height, "--height", error, errorSize)) {
                return 0;
            }
            continue;
        }
        if (optionMatches(arg, "--vsync-mode")) {
            const char* value = requireOptionValue(argc, argv, &i, "--vsync-mode", error, errorSize);
            if (!value) return 0;
            if (!parsePresentModeValue(value, &outOptions->createInfo.presentModePreference, error, errorSize)) {
                return 0;
            }
            continue;
        }
        if (stringsEqual(arg, "--fullscreen")) {
            outOptions->createInfo.startFullscreen = 1u;
            continue;
        }
        if (optionMatches(arg, "--device-index")) {
            const char* value = requireOptionValue(argc, argv, &i, "--device-index", error, errorSize);
            if (!value) return 0;
            if (!parseDeviceIndexValue(value, &outOptions->createInfo.preferredDeviceIndex, error, errorSize)) {
                return 0;
            }
            continue;
        }
        if (optionMatches(arg, "--device-name")) {
            const char* value = requireOptionValue(argc, argv, &i, "--device-name", error, errorSize);
            if (!value || !value[0]) {
                snprintf(error, errorSize, "Invalid value for --device-name");
                return 0;
            }
            outOptions->createInfo.preferredDeviceName = value;
            continue;
        }
        if (stringsEqual(arg, "--empty-scene")) {
            outOptions->loadDefaultScene = 0u;
            continue;
        }
        if (optionMatches(arg, "--import")) {
            const char* value = requireOptionValue(argc, argv, &i, "--import", error, errorSize);
            if (!value || !value[0]) {
                snprintf(error, errorSize, "Invalid value for --import");
                return 0;
            }
            outOptions->startupImportPath = value;
            continue;
        }

        snprintf(error, errorSize, "Unknown option: %s", arg);
        return 0;
    }

    return 1;
}

int CLIHandleImmediateMode(const CLILaunchOptions* options, int* outExitCode) {
    if (outExitCode) *outExitCode = EXIT_SUCCESS;
    if (!options) return 0;

    switch (options->mode) {
        case CLI_MODE_HELP:
            CLIPrintHelp();
            return 1;
        case CLI_MODE_VERSION:
            CLIPrintVersion();
            return 1;
        case CLI_MODE_RUN:
        default:
            return 0;
    }
}

void CLIPrintArgumentError(const char* error) {
    if (!error || !error[0]) return;
    fprintf(stderr, "Error: %s\n\n", error);
    fprintf(stderr, "Use --help to show available options.\n");
}

void CLIPrintVersion(void) {
    printf("vkrt %s\n", VKRT_VERSION);
    printf("  GLFW    %d.%d.%d\n", GLFW_VERSION_MAJOR, GLFW_VERSION_MINOR, GLFW_VERSION_REVISION);
    printf("  Vulkan  %d.%d.%d\n",
        VK_API_VERSION_MAJOR(VK_HEADER_VERSION_COMPLETE),
        VK_API_VERSION_MINOR(VK_HEADER_VERSION_COMPLETE),
        VK_API_VERSION_PATCH(VK_HEADER_VERSION_COMPLETE));
    printf("  spng    %d.%d.%d\n", SPNG_VERSION_MAJOR, SPNG_VERSION_MINOR, SPNG_VERSION_PATCH);
}

void CLIPrintHelp(void) {
    CLIPrintVersion();
    printf("\nCross-platform hardware pathtracer in C using Vulkan. \n");
    printf("\nUsage: vkrt [options]\n");
    printf("\nOptions:\n");
    printf("  --help                    Show this help message and exit\n");
    printf("  --version                 Show version and exit\n");
    printf("  --width <px>              Set initial window width\n");
    printf("  --height <px>             Set initial window height\n");
    printf("  --vsync-mode <n>          Set present mode: 0 adaptive, 1 vsync, 2 mailbox, 3 immediate\n");
    printf("  --fullscreen              Start in fullscreen mode\n");
    printf("  --device-index <index>    Force a Vulkan device by enumerated index\n");
    printf("  --device-name <text>      Force a Vulkan device if its name contains this text\n");
    printf("  --empty-scene             Skip loading the default starter scene\n");
    printf("  --import <path>           Import a mesh on startup\n");
    printf("\nViewport Controls:\n");
    printf("  Middle mouse drag          Orbit camera\n");
    printf("  Shift + middle mouse drag  Pan camera\n");
    printf("  Right mouse drag           Pan camera\n");
    printf("  Scroll wheel               Zoom camera\n");
    printf("  Left click                 Select mesh\n");
    printf("\nRender View Controls:\n");
    printf("  Middle mouse drag          Pan render view\n");
    printf("  Right mouse drag           Pan render view\n");
    printf("  Scroll wheel               Zoom render view\n");
    printf("\nFile Formats:\n");
    printf("  Import: glTF 2.0 (.glb, .gltf)\n");
    printf("  Export: PNG (.png)\n");
    printf("\nRequirements:\n");
    printf("  Vulkan 1.2+ with ray tracing pipeline support\n");
}
