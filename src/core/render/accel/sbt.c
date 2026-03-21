#include "accel.h"

#include "buffer.h"
#include "device.h"
#include "debug.h"

#include <stdlib.h>
#include <string.h>

static VKRT_Result createShaderBindingTableForPipeline(
    VKRT* vkrt,
    VkPipeline pipeline,
    uint32_t raygenGroupCount,
    uint32_t missGroupCount,
    uint32_t hitGroupCount,
    VkBuffer* outBuffer,
    VkDeviceMemory* outMemory,
    VkStridedDeviceAddressRegionKHR outTables[4]
) {
    if (!vkrt || pipeline == VK_NULL_HANDLE || !outBuffer || !outMemory || !outTables) {
        return VKRT_ERROR_INVALID_ARGUMENT;
    }

    if (raygenGroupCount != 1u || missGroupCount == 0u || hitGroupCount == 0u) {
        return VKRT_ERROR_INVALID_ARGUMENT;
    }

    const uint32_t groupCount = raygenGroupCount + missGroupCount + hitGroupCount;

    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingPipelineProperties = {0};
    rayTracingPipelineProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;

    VkPhysicalDeviceProperties2 physicalDeviceProperties2 = {0};
    physicalDeviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    physicalDeviceProperties2.pNext = &rayTracingPipelineProperties;

    vkGetPhysicalDeviceProperties2(vkrt->core.physicalDevice, &physicalDeviceProperties2);
    VkDeviceSize handleSize = rayTracingPipelineProperties.shaderGroupHandleSize;
    VkDeviceSize stride = rayTracingPipelineProperties.shaderGroupBaseAlignment;
    VkDeviceSize sbtSize = groupCount * stride;

    VkBuffer stageBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stageMemory = VK_NULL_HANDLE;
    if (createBuffer(
        vkrt,
        sbtSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &stageBuffer,
        &stageMemory
    ) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;

    uint8_t* handles = (uint8_t*)malloc(groupCount * handleSize);
    if (!handles) {
        vkDestroyBuffer(vkrt->core.device, stageBuffer, NULL);
        vkFreeMemory(vkrt->core.device, stageMemory, NULL);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    VkResult handlesResult = vkrt->core.procs.vkGetRayTracingShaderGroupHandlesKHR(
        vkrt->core.device,
        pipeline,
        0,
        groupCount,
        groupCount * handleSize,
        handles
    );
    if (handlesResult != VK_SUCCESS) {
        free(handles);
        vkDestroyBuffer(vkrt->core.device, stageBuffer, NULL);
        vkFreeMemory(vkrt->core.device, stageMemory, NULL);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    void* mapped = NULL;
    VkResult mapResult = vkMapMemory(vkrt->core.device, stageMemory, 0, sbtSize, 0, &mapped);
    if (mapResult != VK_SUCCESS || !mapped) {
        free(handles);
        vkDestroyBuffer(vkrt->core.device, stageBuffer, NULL);
        vkFreeMemory(vkrt->core.device, stageMemory, NULL);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    for (uint32_t i = 0; i < groupCount; i++) {
        memcpy((uint8_t*)mapped + i * stride, handles + i * handleSize, handleSize);
    }
    vkUnmapMemory(vkrt->core.device, stageMemory);
    free(handles);

    VkBufferCreateInfo bufferCreateInfo = {0};
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.size = sbtSize;
    bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR |
                             VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(vkrt->core.device, &bufferCreateInfo, NULL, outBuffer) != VK_SUCCESS) {
        vkDestroyBuffer(vkrt->core.device, stageBuffer, NULL);
        vkFreeMemory(vkrt->core.device, stageMemory, NULL);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    VkMemoryRequirements memoryRequirements = {0};
    vkGetBufferMemoryRequirements(vkrt->core.device, *outBuffer, &memoryRequirements);

    VkMemoryAllocateFlagsInfo memoryAllocateFlagsInfo = {0};
    memoryAllocateFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    memoryAllocateFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    VkMemoryAllocateInfo memoryAllocateInfo = {0};
    memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memoryAllocateInfo.pNext = &memoryAllocateFlagsInfo;
    memoryAllocateInfo.allocationSize = memoryRequirements.size;
    if (findMemoryType(
        vkrt,
        memoryRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        &memoryAllocateInfo.memoryTypeIndex
    ) != VKRT_SUCCESS) {
        vkDestroyBuffer(vkrt->core.device, *outBuffer, NULL);
        *outBuffer = VK_NULL_HANDLE;
        vkDestroyBuffer(vkrt->core.device, stageBuffer, NULL);
        vkFreeMemory(vkrt->core.device, stageMemory, NULL);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    if (vkAllocateMemory(vkrt->core.device, &memoryAllocateInfo, NULL, outMemory) != VK_SUCCESS) {
        vkDestroyBuffer(vkrt->core.device, *outBuffer, NULL);
        *outBuffer = VK_NULL_HANDLE;
        vkDestroyBuffer(vkrt->core.device, stageBuffer, NULL);
        vkFreeMemory(vkrt->core.device, stageMemory, NULL);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    if (vkBindBufferMemory(vkrt->core.device, *outBuffer, *outMemory, 0) != VK_SUCCESS) {
        vkDestroyBuffer(vkrt->core.device, *outBuffer, NULL);
        vkFreeMemory(vkrt->core.device, *outMemory, NULL);
        *outBuffer = VK_NULL_HANDLE;
        *outMemory = VK_NULL_HANDLE;
        vkDestroyBuffer(vkrt->core.device, stageBuffer, NULL);
        vkFreeMemory(vkrt->core.device, stageMemory, NULL);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    if (copyBuffer(vkrt, stageBuffer, *outBuffer, sbtSize) != VKRT_SUCCESS) {
        vkDestroyBuffer(vkrt->core.device, *outBuffer, NULL);
        vkFreeMemory(vkrt->core.device, *outMemory, NULL);
        *outBuffer = VK_NULL_HANDLE;
        *outMemory = VK_NULL_HANDLE;
        vkDestroyBuffer(vkrt->core.device, stageBuffer, NULL);
        vkFreeMemory(vkrt->core.device, stageMemory, NULL);
        return VKRT_ERROR_OPERATION_FAILED;
    }
    vkDestroyBuffer(vkrt->core.device, stageBuffer, NULL);
    vkFreeMemory(vkrt->core.device, stageMemory, NULL);

    VkBufferDeviceAddressInfo bufferDeviceAddressInfo = {0};
    bufferDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    bufferDeviceAddressInfo.buffer = *outBuffer;
    VkDeviceAddress base = vkrt->core.procs.vkGetBufferDeviceAddressKHR(vkrt->core.device, &bufferDeviceAddressInfo);

    outTables[0].deviceAddress = base;
    outTables[0].stride = stride;
    outTables[0].size = stride;

    outTables[1].deviceAddress = base + raygenGroupCount * stride;
    outTables[1].stride = stride;
    outTables[1].size = missGroupCount * stride;

    outTables[2].deviceAddress = base + (raygenGroupCount + missGroupCount) * stride;
    outTables[2].stride = stride;
    outTables[2].size = hitGroupCount * stride;

    outTables[3].deviceAddress = 0;
    outTables[3].stride = 0;
    outTables[3].size = 0;

    return VKRT_SUCCESS;
}

VKRT_Result createShaderBindingTable(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    uint64_t startTime = getMicroseconds();
    VKRT_Result result = createShaderBindingTableForPipeline(
        vkrt,
        vkrt->core.rayTracingPipeline,
        1u,
        2u,
        2u,
        &vkrt->core.shaderBindingTableBuffer,
        &vkrt->core.shaderBindingTableMemory,
        vkrt->core.shaderBindingTables
    );
    if (result != VKRT_SUCCESS) return result;

    LOG_TRACE("Shader binding table created in %.3f ms", (double)(getMicroseconds() - startTime) / 1e3);
    return VKRT_SUCCESS;
}

VKRT_Result createSelectionShaderBindingTable(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    uint64_t startTime = getMicroseconds();
    VKRT_Result result = createShaderBindingTableForPipeline(
        vkrt,
        vkrt->core.selectionRayTracingPipeline,
        1u,
        1u,
        1u,
        &vkrt->core.selectionShaderBindingTableBuffer,
        &vkrt->core.selectionShaderBindingTableMemory,
        vkrt->core.selectionShaderBindingTables
    );
    if (result != VKRT_SUCCESS) return result;

    LOG_TRACE("Selection shader binding table created in %.3f ms", (double)(getMicroseconds() - startTime) / 1e3);
    return VKRT_SUCCESS;
}
