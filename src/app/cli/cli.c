#include "cli.h"

#include <stdio.h>

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>
#include <spng.h>

#ifndef VKRT_VERSION
#define VKRT_VERSION "dev"
#endif

void cliPrintVersion(void) {
    printf("vkrt %s\n", VKRT_VERSION);
    printf("  GLFW    %d.%d.%d\n", GLFW_VERSION_MAJOR, GLFW_VERSION_MINOR, GLFW_VERSION_REVISION);
    printf("  Vulkan  %d.%d.%d\n",
        VK_API_VERSION_MAJOR(VK_HEADER_VERSION_COMPLETE),
        VK_API_VERSION_MINOR(VK_HEADER_VERSION_COMPLETE),
        VK_API_VERSION_PATCH(VK_HEADER_VERSION_COMPLETE));
    printf("  spng    %d.%d.%d\n", SPNG_VERSION_MAJOR, SPNG_VERSION_MINOR, SPNG_VERSION_PATCH);
}

void cliPrintHelp(void) {
    cliPrintVersion();
    printf("\nCross-platform hardware pathtracer in C using Vulkan. \n");
    printf("\nUsage: vkrt [options]\n");
    printf("\nOptions:\n");
    printf("  --help       Show this help message and exit\n");
    printf("  --version    Show version and exit\n");
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
