#include "procs.h"

#include "debug.h"
#include "vkrt_types.h"
#include "vulkan/vulkan_core.h"

#include <stdio.h>

typedef struct DeviceProcLoad {
    PFN_vkVoidFunction* slot;
    const char* name;
} DeviceProcLoad;

static VKRT_Result loadRequiredDeviceProc(VKRT* vkrt, PFN_vkVoidFunction* slot, const char* name) {
    *slot = vkGetDeviceProcAddr(vkrt->core.device, name);
    if (*slot) return VKRT_SUCCESS;

    LOG_ERROR("Failed to load Vulkan procedure %s", name);
    return VKRT_ERROR_OPERATION_FAILED;
}

VKRT_Result loadDeviceProcs(VKRT* vkrt) {
    if (!vkrt || vkrt->core.device == VK_NULL_HANDLE) {
        LOG_ERROR("Cannot load device procedures before logical device creation");
        return VKRT_ERROR_INVALID_ARGUMENT;
    }

    DeviceProcLoad requiredProcs[] = {
        {(PFN_vkVoidFunction*)&vkrt->core.procs.vkCreateRayTracingPipelinesKHR, "vkCreateRayTracingPipelinesKHR"},
        {(PFN_vkVoidFunction*)&vkrt->core.procs.vkGetRayTracingShaderGroupHandlesKHR,
         "vkGetRayTracingShaderGroupHandlesKHR"},
        {(PFN_vkVoidFunction*)&vkrt->core.procs.vkCreateAccelerationStructureKHR, "vkCreateAccelerationStructureKHR"},
        {(PFN_vkVoidFunction*)&vkrt->core.procs.vkDestroyAccelerationStructureKHR, "vkDestroyAccelerationStructureKHR"},
        {(PFN_vkVoidFunction*)&vkrt->core.procs.vkGetAccelerationStructureBuildSizesKHR,
         "vkGetAccelerationStructureBuildSizesKHR"},
        {(PFN_vkVoidFunction*)&vkrt->core.procs.vkGetAccelerationStructureDeviceAddressKHR,
         "vkGetAccelerationStructureDeviceAddressKHR"},
        {(PFN_vkVoidFunction*)&vkrt->core.procs.vkCmdBuildAccelerationStructuresKHR,
         "vkCmdBuildAccelerationStructuresKHR"},
        {(PFN_vkVoidFunction*)&vkrt->core.procs.vkGetBufferDeviceAddressKHR, "vkGetBufferDeviceAddressKHR"},
        {(PFN_vkVoidFunction*)&vkrt->core.procs.vkCmdTraceRaysKHR, "vkCmdTraceRaysKHR"},
    };

    for (size_t i = 0; i < sizeof(requiredProcs) / sizeof(requiredProcs[0]); i++) {
        VKRT_Result result = loadRequiredDeviceProc(vkrt, requiredProcs[i].slot, requiredProcs[i].name);
        if (result != VKRT_SUCCESS) return result;
    }

    vkrt->core.procs.vkCmdBeginDebugUtilsLabelEXT =
        (PFN_vkCmdBeginDebugUtilsLabelEXT)vkGetDeviceProcAddr(vkrt->core.device, "vkCmdBeginDebugUtilsLabelEXT");
    vkrt->core.procs.vkCmdEndDebugUtilsLabelEXT =
        (PFN_vkCmdEndDebugUtilsLabelEXT)vkGetDeviceProcAddr(vkrt->core.device, "vkCmdEndDebugUtilsLabelEXT");
    return VKRT_SUCCESS;
}
