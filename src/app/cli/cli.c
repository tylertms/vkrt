#include "cli.h"

#include "vkrt.h"
#include "vulkan/vulkan_core.h"

#include <dcimgui.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <version.h>

#ifndef VKRT_VERSION
#define VKRT_VERSION "dev"
#endif

#ifndef VKRT_GLFW_VERSION
#define VKRT_GLFW_VERSION "unknown"
#endif

#ifndef VKRT_SPNG_VERSION
#define VKRT_SPNG_VERSION "unknown"
#endif

#ifndef VKRT_TURBOJPEG_VERSION
#define VKRT_TURBOJPEG_VERSION "unknown"
#endif

static const uint32_t kOfflineRenderWidth = 3840u;
static const uint32_t kOfflineRenderHeight = 2160u;
static const uint32_t kOfflineRenderTargetSamples = 16384u;

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

static uint32_t queryVulkanRuntimeVersion(void) {
    uint32_t version = VK_API_VERSION_1_0;
    PFN_vkEnumerateInstanceVersion enumerateInstanceVersion =
        (PFN_vkEnumerateInstanceVersion)vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion");
    if (enumerateInstanceVersion && enumerateInstanceVersion(&version) != VK_SUCCESS) {
        version = VK_API_VERSION_1_0;
    }
    return version;
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

        (void)snprintf(error, errorSize, "Missing value for %s", optionName);
        return NULL;
    }

    if (*inOutIndex + 1 >= argc) {
        (void)snprintf(error, errorSize, "Missing value for %s", optionName);
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
        (void)snprintf(error, errorSize, "Invalid value for %s: %s", optionName, text);
        return 0;
    }

    *outValue = (uint32_t)parsed;
    return 1;
}

static int parseDeviceIndexValue(const char* text, int32_t* outValue, char* error, size_t errorSize) {
    if (!text || !outValue || !error || errorSize == 0) return 0;

    errno = 0;
    char* end = NULL;
    long parsed = strtol(text, &end, 10);
    if (errno != 0 || end == text || (end && end[0] != '\0') || parsed < 0 || parsed > INT32_MAX) {
        (void)snprintf(error, errorSize, "Invalid value for --device-index: %s", text);
        return 0;
    }

    *outValue = (int32_t)parsed;
    return 1;
}

void CLIDefaultLaunchOptions(CLILaunchOptions* options) {
    if (!options) return;

    memset(options, 0, sizeof(*options));
    options->mode = CLI_MODE_RUN;
    options->loadDefaultScene = 1u;
    options->offlineRender.width = kOfflineRenderWidth;
    options->offlineRender.height = kOfflineRenderHeight;
    options->offlineRender.targetSamples = kOfflineRenderTargetSamples;
    VKRT_defaultCreateInfo(&options->createInfo);
}

static int setCLIError(char* error, size_t errorSize, const char* message, const char* value) {
    if (!error || errorSize == 0u || !message) return 0;
    if (value) {
        (void)snprintf(error, errorSize, message, value);
    } else {
        (void)snprintf(error, errorSize, "%s", message);
    }
    return 0;
}

static int parseWindowArgument(
    const char* arg,
    int argc,
    char* argv[],
    int* index,
    CLILaunchOptions* options,
    char* error,
    size_t errorSize
) {
    if (optionMatches(arg, "--width")) {
        const char* value = requireOptionValue(argc, argv, index, "--width", error, errorSize);
        return value && parseUnsignedValue(value, &options->createInfo.width, "--width", error, errorSize);
    }
    if (optionMatches(arg, "--height")) {
        const char* value = requireOptionValue(argc, argv, index, "--height", error, errorSize);
        return value && parseUnsignedValue(value, &options->createInfo.height, "--height", error, errorSize);
    }
    if (stringsEqual(arg, "--fullscreen")) {
        options->createInfo.startFullscreen = 1u;
        return 1;
    }
    if (stringsEqual(arg, "--no-ser")) {
        options->createInfo.disableSER = 1u;
        return 1;
    }
    if (optionMatches(arg, "--device-index")) {
        const char* value = requireOptionValue(argc, argv, index, "--device-index", error, errorSize);
        return value && parseDeviceIndexValue(value, &options->createInfo.preferredDeviceIndex, error, errorSize);
    }
    if (optionMatches(arg, "--device-name")) {
        const char* value = requireOptionValue(argc, argv, index, "--device-name", error, errorSize);
        if (!value || !value[0]) return setCLIError(error, errorSize, "Invalid value for --device-name", NULL);
        options->createInfo.preferredDeviceName = value;
        return 1;
    }
    return -1;
}

static int parseOfflineRenderArgument(
    const char* arg,
    int argc,
    char* argv[],
    int* index,
    CLILaunchOptions* options,
    char* error,
    size_t errorSize
) {
    if (stringsEqual(arg, "--render") || stringsEqual(arg, "--benchmark")) {
        options->offlineRender.enabled = 1u;
        options->offlineRender.headless = 0u;
        return 1;
    }
    if (stringsEqual(arg, "--render-headless")) {
        options->offlineRender.enabled = 1u;
        options->offlineRender.headless = 1u;
        return 1;
    }
    if (optionMatches(arg, "--render-width")) {
        const char* value = requireOptionValue(argc, argv, index, "--render-width", error, errorSize);
        return value && parseUnsignedValue(value, &options->offlineRender.width, "--render-width", error, errorSize);
    }
    if (optionMatches(arg, "--render-height")) {
        const char* value = requireOptionValue(argc, argv, index, "--render-height", error, errorSize);
        return value && parseUnsignedValue(value, &options->offlineRender.height, "--render-height", error, errorSize);
    }
    if (optionMatches(arg, "--render-samples")) {
        const char* value = requireOptionValue(argc, argv, index, "--render-samples", error, errorSize);
        return value &&
               parseUnsignedValue(value, &options->offlineRender.targetSamples, "--render-samples", error, errorSize);
    }
    return -1;
}

static int parseSceneArgument(
    const char* arg,
    int argc,
    char* argv[],
    int* index,
    CLILaunchOptions* options,
    char* error,
    size_t errorSize
) {
    if (stringsEqual(arg, "--empty-scene")) {
        options->loadDefaultScene = 0u;
        return 1;
    }
    if (optionMatches(arg, "--scene")) {
        const char* value = requireOptionValue(argc, argv, index, "--scene", error, errorSize);
        if (!value || !value[0]) return setCLIError(error, errorSize, "Invalid value for --scene", NULL);
        options->startupScenePath = value;
        options->loadDefaultScene = 0u;
        return 1;
    }
    if (optionMatches(arg, "--import")) {
        const char* value = requireOptionValue(argc, argv, index, "--import", error, errorSize);
        if (!value || !value[0]) return setCLIError(error, errorSize, "Invalid value for --import", NULL);
        options->startupImportPath = value;
        return 1;
    }
    if (optionMatches(arg, "--render-output")) {
        const char* value = requireOptionValue(argc, argv, index, "--render-output", error, errorSize);
        if (!value || !value[0]) return setCLIError(error, errorSize, "Invalid value for --render-output", NULL);
        options->renderOutputPath = value;
        return 1;
    }
    return -1;
}

static int validateCLIArgumentCombination(const CLILaunchOptions* options, char* error, size_t errorSize) {
    if (!options->offlineRender.enabled) return 1;
    if (options->startupImportPath) {
        return setCLIError(error, errorSize, "--render cannot be combined with --import", NULL);
    }
    if (!options->loadDefaultScene && !options->startupScenePath) {
        return setCLIError(error, errorSize, "--render requires either the default scene or --scene", NULL);
    }
    if (options->renderOutputPath && !options->offlineRender.headless) {
        return setCLIError(error, errorSize, "--render-output currently requires --render-headless", NULL);
    }
    return 1;
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

        int parseResult = parseWindowArgument(arg, argc, argv, &i, outOptions, error, errorSize);
        if (parseResult == 0) return 0;
        if (parseResult > 0) continue;

        parseResult = parseOfflineRenderArgument(arg, argc, argv, &i, outOptions, error, errorSize);
        if (parseResult == 0) return 0;
        if (parseResult > 0) continue;

        parseResult = parseSceneArgument(arg, argc, argv, &i, outOptions, error, errorSize);
        if (parseResult == 0) return 0;
        if (parseResult > 0) continue;

        setCLIError(error, errorSize, "Unknown option: %s", arg);
        return 0;
    }

    return validateCLIArgumentCombination(outOptions, error, errorSize);
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
    (void)fputs("Error: ", stderr);
    (void)fputs(error, stderr);
    (void)fputs("\n\nUse --help to show available options.\n", stderr);
}

void CLIPrintVersion(void) {
    uint32_t runtimeVersion = queryVulkanRuntimeVersion();
    printf("===================================\n");
    printf(" vkrt       %s\n", VKRT_VERSION);
    printf("===================================\n");

    printf(
        " Vulkan     %d.%d.%d\n",
        VK_API_VERSION_MAJOR(runtimeVersion),
        VK_API_VERSION_MINOR(runtimeVersion),
        VK_API_VERSION_PATCH(runtimeVersion)
    );
    printf(" ImGui      %s\n", IMGUI_VERSION);
    printf(" glfw       %s\n", VKRT_GLFW_VERSION);
    printf(" cglm       %d.%d.%d\n", CGLM_VERSION_MAJOR, CGLM_VERSION_MINOR, CGLM_VERSION_PATCH);
    printf(" cgltf      1.15\n");
    printf(" libspng    %s\n", VKRT_SPNG_VERSION);
    printf(" turbojpeg  %s\n", VKRT_TURBOJPEG_VERSION);
    printf("===================================\n");
    printf(" https://github.com/tylertms/vkrt\n");
    printf("===================================\n");
}

void CLIPrintHelp(void) {
    printf("\nCross-platform hardware pathtracer in C using Vulkan. \n");
    printf("\nUsage: vkrt [options]\n");
    printf("\nOptions:\n");
    printf("  --help                    Show this help message and exit\n");
    printf("  --version                 Show version and exit\n");
    printf("  --width <px>              Set initial window width\n");
    printf("  --height <px>             Set initial window height\n");
    printf("  --fullscreen              Start in fullscreen mode\n");
    printf("  --no-ser                  Disable shader execution reordering even if supported\n");
    printf("  --device-index <index>    Force a Vulkan device by enumerated index\n");
    printf("  --device-name <text>      Force a Vulkan device if its name contains this text\n");
    printf("  --empty-scene             Skip loading the default starter scene\n");
    printf("  --scene <path>            Load a vkrt scene (.json) on startup\n");
    printf("  --render                  Run an offline render with presentation enabled\n");
    printf(
        "  --render-headless         Run an offline render offscreen with no window or "
        "presentation\n"
    );
    printf("  --render-width <px>       Override offline render width (default: 3840)\n");
    printf("  --render-height <px>      Override offline render height (default: 2160)\n");
    printf("  --render-samples <n>      Override offline render target samples (default: 16384)\n");
    printf("  --import <path>           Import a mesh on startup\n");
    printf("  --render-output <path>    Save the --render-headless image after completion\n");
    printf("  --benchmark               Alias for --render\n");
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
    printf("  Import: glTF mesh (.glb, .gltf; geometry + scalar material factors)\n");
    printf("  Texture import: Image (.png, .jpg, .jpeg, .exr)\n");
    printf("  Environment import: Image (.exr, .png, .jpg, .jpeg)\n");
    printf("  Export: Image (.png, .jpg, .jpeg, .exr)\n");
    printf("\nRequirements:\n");
    printf("  Vulkan 1.3+ with ray tracing pipeline support\n");
}
