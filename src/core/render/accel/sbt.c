#include "accel.h"

#include "buffer.h"
#include "device.h"
#include "debug.h"

#include <stdlib.h>
#include <string.h>

VKRT_Result createShaderBindingTable(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    uint64_t startTime = getMicroseconds();

    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingPipelineProperties = {0};
    rayTracingPipelineProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;

    VkPhysicalDeviceProperties2 physicalDeviceProperties2 = {0};
    physicalDeviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    physicalDeviceProperties2.pNext = &rayTracingPipelineProperties;

    vkGetPhysicalDeviceProperties2(vkrt->core.physicalDevice, &physicalDeviceProperties2);

    const uint32_t groupCount = 3;
    VkDeviceSize handleSize = rayTracingPipelineProperties.shaderGroupHandleSize;
    VkDeviceSize stride = rayTracingPipelineProperties.shaderGroupBaseAlignment;
    VkDeviceSize sbtSize = groupCount * stride;

    VkBuffer stageBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stageMemory = VK_NULL_HANDLE;
    if (createBuffer(vkrt,
        sbtSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &stageBuffer,
        &stageMemory) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;

    uint8_t* handles = (uint8_t*)malloc(groupCount * handleSize);
    if (!handles) {
        vkDestroyBuffer(vkrt->core.device, stageBuffer, NULL);
        vkFreeMemory(vkrt->core.device, stageMemory, NULL);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    VkResult handlesResult = vkrt->core.procs.vkGetRayTracingShaderGroupHandlesKHR(
        vkrt->core.device,
        vkrt->core.rayTracingPipeline,
        0,
        groupCount,
        groupCount * handleSize,
        handles);
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

    if (vkCreateBuffer(vkrt->core.device, &bufferCreateInfo, NULL, &vkrt->core.shaderBindingTableBuffer) != VK_SUCCESS) {
        vkDestroyBuffer(vkrt->core.device, stageBuffer, NULL);
        vkFreeMemory(vkrt->core.device, stageMemory, NULL);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    VkMemoryRequirements memoryRequirements = {0};
    vkGetBufferMemoryRequirements(vkrt->core.device, vkrt->core.shaderBindingTableBuffer, &memoryRequirements);

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
            &memoryAllocateInfo.memoryTypeIndex) != VKRT_SUCCESS) {
        vkDestroyBuffer(vkrt->core.device, vkrt->core.shaderBindingTableBuffer, NULL);
        vkrt->core.shaderBindingTableBuffer = VK_NULL_HANDLE;
        vkDestroyBuffer(vkrt->core.device, stageBuffer, NULL);
        vkFreeMemory(vkrt->core.device, stageMemory, NULL);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    if (vkAllocateMemory(vkrt->core.device, &memoryAllocateInfo, NULL, &vkrt->core.shaderBindingTableMemory) != VK_SUCCESS) {
        vkDestroyBuffer(vkrt->core.device, vkrt->core.shaderBindingTableBuffer, NULL);
        vkrt->core.shaderBindingTableBuffer = VK_NULL_HANDLE;
        vkDestroyBuffer(vkrt->core.device, stageBuffer, NULL);
        vkFreeMemory(vkrt->core.device, stageMemory, NULL);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    if (vkBindBufferMemory(vkrt->core.device, vkrt->core.shaderBindingTableBuffer, vkrt->core.shaderBindingTableMemory, 0) != VK_SUCCESS) {
        vkDestroyBuffer(vkrt->core.device, vkrt->core.shaderBindingTableBuffer, NULL);
        vkFreeMemory(vkrt->core.device, vkrt->core.shaderBindingTableMemory, NULL);
        vkrt->core.shaderBindingTableBuffer = VK_NULL_HANDLE;
        vkrt->core.shaderBindingTableMemory = VK_NULL_HANDLE;
        vkDestroyBuffer(vkrt->core.device, stageBuffer, NULL);
        vkFreeMemory(vkrt->core.device, stageMemory, NULL);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    if (copyBuffer(vkrt, stageBuffer, vkrt->core.shaderBindingTableBuffer, sbtSize) != VKRT_SUCCESS) {
        vkDestroyBuffer(vkrt->core.device, stageBuffer, NULL);
        vkFreeMemory(vkrt->core.device, stageMemory, NULL);
        return VKRT_ERROR_OPERATION_FAILED;
    }
    vkDestroyBuffer(vkrt->core.device, stageBuffer, NULL);
    vkFreeMemory(vkrt->core.device, stageMemory, NULL);

    VkBufferDeviceAddressInfo bufferDeviceAddressInfo = {0};
    bufferDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    bufferDeviceAddressInfo.buffer = vkrt->core.shaderBindingTableBuffer;
    VkDeviceAddress base = vkrt->core.procs.vkGetBufferDeviceAddressKHR(vkrt->core.device, &bufferDeviceAddressInfo);

    for (int i = 0; i < 3; i++) {
        vkrt->core.shaderBindingTables[i].deviceAddress = base + i * stride;
        vkrt->core.shaderBindingTables[i].stride = stride;
        vkrt->core.shaderBindingTables[i].size = stride;
    }

    vkrt->core.shaderBindingTables[3].deviceAddress = 0;
    vkrt->core.shaderBindingTables[3].stride = 0;
    vkrt->core.shaderBindingTables[3].size = 0;

    LOG_TRACE("Shader binding table created in %.3f ms", (double)(getMicroseconds() - startTime) / 1e3);
    return VKRT_SUCCESS;
}
