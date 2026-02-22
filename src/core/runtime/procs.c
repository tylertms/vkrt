#include "procs.h"
#include "debug.h"

#include <stdio.h>
#include <stdlib.h>

#define LOAD_DEVICE_PROC(vkrt, proc_name)                                                                         \
    do {                                                                                                          \
        (vkrt)->core.procs.proc_name = (PFN_##proc_name)vkGetDeviceProcAddr((vkrt)->core.device, #proc_name);   \
        if (!(vkrt)->core.procs.proc_name) {                                                                      \
            LOG_ERROR("Failed to load Vulkan procedure %s", #proc_name);                     \
            exit(EXIT_FAILURE);                                                                                   \
        }                                                                                                         \
    } while (0)

void loadDeviceProcs(VKRT* vkrt) {
    if (!vkrt || vkrt->core.device == VK_NULL_HANDLE) {
        LOG_ERROR("Cannot load device procedures before logical device creation");
        exit(EXIT_FAILURE);
    }

    LOAD_DEVICE_PROC(vkrt, vkCreateRayTracingPipelinesKHR);
    LOAD_DEVICE_PROC(vkrt, vkGetRayTracingShaderGroupHandlesKHR);
    LOAD_DEVICE_PROC(vkrt, vkCreateAccelerationStructureKHR);
    LOAD_DEVICE_PROC(vkrt, vkDestroyAccelerationStructureKHR);
    LOAD_DEVICE_PROC(vkrt, vkGetAccelerationStructureBuildSizesKHR);
    LOAD_DEVICE_PROC(vkrt, vkGetAccelerationStructureDeviceAddressKHR);
    LOAD_DEVICE_PROC(vkrt, vkCmdBuildAccelerationStructuresKHR);
    LOAD_DEVICE_PROC(vkrt, vkGetBufferDeviceAddressKHR);
    LOAD_DEVICE_PROC(vkrt, vkCmdTraceRaysKHR);
}
