#include "accel.h"
#include "buffer.h"
#include "debug.h"
#include "device.h"
#include "platform.h"
#include "vkrt_internal.h"
#include "vkrt_types.h"
#include "vulkan/vulkan_core.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct ShaderBindingTableBuildOutput {
    VkBuffer* buffer;
    VkDeviceMemory* memory;
    VkStridedDeviceAddressRegionKHR* tables;
} ShaderBindingTableBuildOutput;

static void destroyShaderBindingTableStageResources(VKRT* vkrt, VkBuffer buffer, VkDeviceMemory memory) {
    if (!vkrt) return;
    if (buffer != VK_NULL_HANDLE) vkDestroyBuffer(vkrt->core.device, buffer, NULL);
    if (memory != VK_NULL_HANDLE) vkFreeMemory(vkrt->core.device, memory, NULL);
}

static VKRT_Result createShaderBindingTableStageBuffer(
    VKRT* vkrt,
    VkPipeline pipeline,
    uint32_t groupCount,
    VkDeviceSize handleSize,
    VkDeviceSize stride,
    VkBuffer* outStageBuffer,
    VkDeviceMemory* outStageMemory
) {
    if (createBuffer(
            vkrt,
            groupCount * stride,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            outStageBuffer,
            outStageMemory
        ) != VKRT_SUCCESS) {
        return VKRT_ERROR_OPERATION_FAILED;
    }

    uint8_t* handles = (uint8_t*)malloc(groupCount * handleSize);
    if (!handles) {
        destroyShaderBindingTableStageResources(vkrt, *outStageBuffer, *outStageMemory);
        *outStageBuffer = VK_NULL_HANDLE;
        *outStageMemory = VK_NULL_HANDLE;
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
        destroyShaderBindingTableStageResources(vkrt, *outStageBuffer, *outStageMemory);
        *outStageBuffer = VK_NULL_HANDLE;
        *outStageMemory = VK_NULL_HANDLE;
        return VKRT_ERROR_OPERATION_FAILED;
    }

    void* mapped = NULL;
    VkResult mapResult = vkMapMemory(vkrt->core.device, *outStageMemory, 0, groupCount * stride, 0, &mapped);
    if (mapResult != VK_SUCCESS || !mapped) {
        free(handles);
        destroyShaderBindingTableStageResources(vkrt, *outStageBuffer, *outStageMemory);
        *outStageBuffer = VK_NULL_HANDLE;
        *outStageMemory = VK_NULL_HANDLE;
        return VKRT_ERROR_OPERATION_FAILED;
    }

    for (uint32_t groupIndex = 0; groupIndex < groupCount; groupIndex++) {
        memcpy(((uint8_t*)mapped) + (groupIndex * stride), handles + (groupIndex * handleSize), handleSize);
    }
    vkUnmapMemory(vkrt->core.device, *outStageMemory);
    free(handles);
    return VKRT_SUCCESS;
}

static VKRT_Result createShaderBindingTableDeviceBuffer(
    VKRT* vkrt,
    VkDeviceSize sbtSize,
    ShaderBindingTableBuildOutput* output
) {
    VkBufferCreateInfo bufferCreateInfo = {0};
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.size = sbtSize;
    bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR |
                             VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(vkrt->core.device, &bufferCreateInfo, NULL, output->buffer) != VK_SUCCESS) {
        return VKRT_ERROR_OPERATION_FAILED;
    }

    VkMemoryRequirements memoryRequirements = {0};
    vkGetBufferMemoryRequirements(vkrt->core.device, *output->buffer, &memoryRequirements);

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
        vkDestroyBuffer(vkrt->core.device, *output->buffer, NULL);
        *output->buffer = VK_NULL_HANDLE;
        return VKRT_ERROR_OPERATION_FAILED;
    }

    if (vkAllocateMemory(vkrt->core.device, &memoryAllocateInfo, NULL, output->memory) != VK_SUCCESS) {
        vkDestroyBuffer(vkrt->core.device, *output->buffer, NULL);
        *output->buffer = VK_NULL_HANDLE;
        return VKRT_ERROR_OPERATION_FAILED;
    }
    if (vkBindBufferMemory(vkrt->core.device, *output->buffer, *output->memory, 0) != VK_SUCCESS) {
        vkDestroyBuffer(vkrt->core.device, *output->buffer, NULL);
        vkFreeMemory(vkrt->core.device, *output->memory, NULL);
        *output->buffer = VK_NULL_HANDLE;
        *output->memory = VK_NULL_HANDLE;
        return VKRT_ERROR_OPERATION_FAILED;
    }
    return VKRT_SUCCESS;
}

static void buildShaderBindingTableRegions(
    VKRT* vkrt,
    VkBuffer buffer,
    VkDeviceSize stride,
    uint32_t raygenGroupCount,
    uint32_t missGroupCount,
    uint32_t hitGroupCount,
    VkStridedDeviceAddressRegionKHR outTables[4]
) {
    VkBufferDeviceAddressInfo bufferDeviceAddressInfo = {0};
    bufferDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    bufferDeviceAddressInfo.buffer = buffer;
    VkDeviceAddress base = vkrt->core.procs.vkGetBufferDeviceAddressKHR(vkrt->core.device, &bufferDeviceAddressInfo);

    outTables[0].deviceAddress = base;
    outTables[0].stride = stride;
    outTables[0].size = stride;
    outTables[1].deviceAddress = base + (raygenGroupCount * stride);
    outTables[1].stride = stride;
    outTables[1].size = missGroupCount * stride;
    outTables[2].deviceAddress = base + ((raygenGroupCount + missGroupCount) * stride);
    outTables[2].stride = stride;
    outTables[2].size = hitGroupCount * stride;
    outTables[3].deviceAddress = 0;
    outTables[3].stride = 0;
    outTables[3].size = 0;
}

static void buildMainRaygenRegions(VKRT* vkrt) {
    if (!vkrt) return;

    VkStridedDeviceAddressRegionKHR baseRegion = vkrt->core.shaderBindingTables[0];
    for (uint32_t groupIndex = 0; groupIndex < VKRT_MAIN_RAYGEN_GROUP_COUNT; groupIndex++) {
        vkrt->core.mainRaygenRegions[groupIndex] = baseRegion;
        vkrt->core.mainRaygenRegions[groupIndex].deviceAddress += (VkDeviceAddress)groupIndex * baseRegion.stride;
        vkrt->core.mainRaygenRegions[groupIndex].size = baseRegion.stride;
    }
}

static VKRT_Result createShaderBindingTableForPipeline(
    VKRT* vkrt,
    const char* label,
    VkPipeline pipeline,
    uint32_t raygenGroupCount,
    uint32_t missGroupCount,
    uint32_t hitGroupCount,
    ShaderBindingTableBuildOutput output
) {
    if (!vkrt || pipeline == VK_NULL_HANDLE || !output.buffer || !output.memory || !output.tables) {
        return VKRT_ERROR_INVALID_ARGUMENT;
    }

    if (raygenGroupCount == 0u || missGroupCount == 0u || hitGroupCount == 0u) {
        return VKRT_ERROR_INVALID_ARGUMENT;
    }

    const uint32_t groupCount = raygenGroupCount + missGroupCount + hitGroupCount;
    uint64_t handlesStartTime = getMicroseconds();

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
    if (createShaderBindingTableStageBuffer(
            vkrt,
            pipeline,
            groupCount,
            handleSize,
            stride,
            &stageBuffer,
            &stageMemory
        ) != VKRT_SUCCESS) {
        return VKRT_ERROR_OPERATION_FAILED;
    }
    LOG_TRACE(
        "%s SBT handles fetched and staged in %.3f ms (%u groups, stride %llu, size %llu bytes)",
        label ? label : "RT",
        (double)(getMicroseconds() - handlesStartTime) / 1e3,
        groupCount,
        (unsigned long long)stride,
        (unsigned long long)sbtSize
    );

    uint64_t uploadStartTime = getMicroseconds();
    if (createShaderBindingTableDeviceBuffer(vkrt, sbtSize, &output) != VKRT_SUCCESS) {
        destroyShaderBindingTableStageResources(vkrt, stageBuffer, stageMemory);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    if (copyBuffer(vkrt, stageBuffer, *output.buffer, sbtSize) != VKRT_SUCCESS) {
        vkDestroyBuffer(vkrt->core.device, *output.buffer, NULL);
        vkFreeMemory(vkrt->core.device, *output.memory, NULL);
        *output.buffer = VK_NULL_HANDLE;
        *output.memory = VK_NULL_HANDLE;
        destroyShaderBindingTableStageResources(vkrt, stageBuffer, stageMemory);
        return VKRT_ERROR_OPERATION_FAILED;
    }
    destroyShaderBindingTableStageResources(vkrt, stageBuffer, stageMemory);
    buildShaderBindingTableRegions(
        vkrt,
        *output.buffer,
        stride,
        raygenGroupCount,
        missGroupCount,
        hitGroupCount,
        output.tables
    );
    LOG_TRACE(
        "%s SBT uploaded in %.3f ms",
        label ? label : "RT",
        (double)(getMicroseconds() - uploadStartTime) / 1e3
    );

    return VKRT_SUCCESS;
}

VKRT_Result createShaderBindingTable(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    VKRT_Result result = createShaderBindingTableForPipeline(
        vkrt,
        "Main RT",
        vkrt->core.rayTracingPipeline,
        VKRT_MAIN_RAYGEN_GROUP_COUNT,
        2u,
        4u,
        (ShaderBindingTableBuildOutput){
            .buffer = &vkrt->core.shaderBindingTableBuffer,
            .memory = &vkrt->core.shaderBindingTableMemory,
            .tables = vkrt->core.shaderBindingTables,
        }
    );
    if (result != VKRT_SUCCESS) return result;

    buildMainRaygenRegions(vkrt);

    LOG_TRACE("Shader binding table created");
    return VKRT_SUCCESS;
}

VKRT_Result createSelectionShaderBindingTable(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    VKRT_Result result = createShaderBindingTableForPipeline(
        vkrt,
        "Selection RT",
        vkrt->core.selectionRayTracingPipeline,
        1u,
        1u,
        1u,
        (ShaderBindingTableBuildOutput){
            .buffer = &vkrt->core.selectionShaderBindingTableBuffer,
            .memory = &vkrt->core.selectionShaderBindingTableMemory,
            .tables = vkrt->core.selectionShaderBindingTables,
        }
    );
    if (result != VKRT_SUCCESS) return result;

    LOG_TRACE("Selection shader binding table created");
    return VKRT_SUCCESS;
}
