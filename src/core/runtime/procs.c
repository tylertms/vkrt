#include "procs.h"
#include "debug.h"

#include <stdio.h>

#define LOAD_DEVICE_PROC(vkrt, proc_name)                                                                         \
    do {                                                                                                          \
        (vkrt)->core.procs.proc_name = (PFN_##proc_name)vkGetDeviceProcAddr((vkrt)->core.device, #proc_name);   \
        if (!(vkrt)->core.procs.proc_name) {                                                                      \
            LOG_ERROR("Failed to load Vulkan procedure %s", #proc_name);                     \
            return VKRT_ERROR_OPERATION_FAILED;                                                                   \
        }                                                                                                         \
    } while (0)

VKRT_Result loadDeviceProcs(VKRT* vkrt) {
    if (!vkrt || vkrt->core.device == VK_NULL_HANDLE) {
        LOG_ERROR("Cannot load device procedures before logical device creation");
        return VKRT_ERROR_INVALID_ARGUMENT;
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
    vkrt->core.procs.vkCmdBeginDebugUtilsLabelEXT =
        (PFN_vkCmdBeginDebugUtilsLabelEXT)vkGetDeviceProcAddr(vkrt->core.device, "vkCmdBeginDebugUtilsLabelEXT");
    vkrt->core.procs.vkCmdEndDebugUtilsLabelEXT =
        (PFN_vkCmdEndDebugUtilsLabelEXT)vkGetDeviceProcAddr(vkrt->core.device, "vkCmdEndDebugUtilsLabelEXT");
    return VKRT_SUCCESS;
}
