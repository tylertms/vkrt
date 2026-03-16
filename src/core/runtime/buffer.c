#include "buffer.h"
#include "command/pool.h"
#include "device.h"
#include "debug.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static VKRT_Result appendPendingSceneTransfer(
    VKRT* vkrt,
    VkBuffer stagingBuffer,
    VkDeviceMemory stagingMemory,
    VkBuffer dstBuffer,
    VkDeviceSize size
) {
    if (!vkrt || stagingBuffer == VK_NULL_HANDLE || stagingMemory == VK_NULL_HANDLE || dstBuffer == VK_NULL_HANDLE || size == 0) {
        return VKRT_ERROR_INVALID_ARGUMENT;
    }

    FrameSceneUpdate* update = vkrtCurrentFrameSceneUpdate(vkrt);
    uint32_t nextCount = update->sceneTransferCount + 1u;
    PendingBufferCopy* resized = (PendingBufferCopy*)realloc(
        update->sceneTransfers,
        (size_t)nextCount * sizeof(PendingBufferCopy)
    );
    if (!resized) {
        return VKRT_ERROR_OPERATION_FAILED;
    }

    update->sceneTransfers = resized;
    update->sceneTransfers[update->sceneTransferCount] = (PendingBufferCopy){
        .stagingBuffer = stagingBuffer,
        .stagingMemory = stagingMemory,
        .dstBuffer = dstBuffer,
        .size = size,
    };
    update->sceneTransferCount = nextCount;
    return VKRT_SUCCESS;
}

VkDeviceAddress queryBufferDeviceAddress(VKRT* vkrt, VkBuffer buffer) {
    if (!vkrt || buffer == VK_NULL_HANDLE) return 0;

    VkBufferDeviceAddressInfo addrInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = buffer
    };
    return vkrt->core.procs.vkGetBufferDeviceAddressKHR(vkrt->core.device, &addrInfo);
}

VKRT_Result createBuffer(
    VKRT* vkrt,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VkBuffer* buffer,
    VkDeviceMemory* bufferMemory
) {
    if (!vkrt || !buffer || !bufferMemory) return VKRT_ERROR_INVALID_ARGUMENT;

    VkBufferCreateInfo bufferCreateInfo = {0};
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.size = size;
    bufferCreateInfo.usage = usage;
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(vkrt->core.device, &bufferCreateInfo, NULL, buffer) != VK_SUCCESS) {
        LOG_ERROR("Failed to create buffer");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    VkMemoryRequirements memoryRequirements;
    vkGetBufferMemoryRequirements(vkrt->core.device, *buffer, &memoryRequirements);

    VkMemoryAllocateInfo memoryAllocateInfo = {0};
    memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memoryAllocateInfo.allocationSize = memoryRequirements.size;
    uint32_t memoryTypeIndex = 0;
    if (findMemoryType(vkrt, memoryRequirements.memoryTypeBits, properties, &memoryTypeIndex) != VKRT_SUCCESS) {
        vkDestroyBuffer(vkrt->core.device, *buffer, NULL);
        *buffer = VK_NULL_HANDLE;
        return VKRT_ERROR_OPERATION_FAILED;
    }
    memoryAllocateInfo.memoryTypeIndex = memoryTypeIndex;

    VkMemoryAllocateFlagsInfo memoryAllocateFlagsInfo = {0};
    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
        memoryAllocateFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        memoryAllocateFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        memoryAllocateInfo.pNext = &memoryAllocateFlagsInfo;
    }

    if (vkAllocateMemory(vkrt->core.device, &memoryAllocateInfo, NULL, bufferMemory) != VK_SUCCESS) {
        LOG_ERROR("Failed to allocate buffer memory");
        vkDestroyBuffer(vkrt->core.device, *buffer, NULL);
        *buffer = VK_NULL_HANDLE;
        return VKRT_ERROR_OPERATION_FAILED;
    }

    if (vkBindBufferMemory(vkrt->core.device, *buffer, *bufferMemory, 0) != VK_SUCCESS) {
        vkDestroyBuffer(vkrt->core.device, *buffer, NULL);
        vkFreeMemory(vkrt->core.device, *bufferMemory, NULL);
        *buffer = VK_NULL_HANDLE;
        *bufferMemory = VK_NULL_HANDLE;
        return VKRT_ERROR_OPERATION_FAILED;
    }

    return VKRT_SUCCESS;
}

VKRT_Result copyBuffer(VKRT* vkrt, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VKRT_Result result = beginSingleTimeCommands(vkrt, &commandBuffer);
    if (result != VKRT_SUCCESS) return result;

    VkBufferCopy copyRegion = {0};
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    return endSingleTimeCommands(vkrt, commandBuffer);
}

VKRT_Result createHostBufferFromData(
    VKRT* vkrt,
    const void* hostData,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkBuffer* outBuffer,
    VkDeviceMemory* outMemory,
    VkDeviceAddress* outDeviceAddress
) {
    if (!vkrt || !hostData || !outBuffer || !outMemory) return VKRT_ERROR_INVALID_ARGUMENT;

    VKRT_Result result = createBuffer(
        vkrt,
        size,
        usage,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        outBuffer,
        outMemory
    );
    if (result != VKRT_SUCCESS) return result;

    void* mapped = NULL;
    if (vkMapMemory(vkrt->core.device, *outMemory, 0, size, 0, &mapped) != VK_SUCCESS || !mapped) {
        vkDestroyBuffer(vkrt->core.device, *outBuffer, NULL);
        vkFreeMemory(vkrt->core.device, *outMemory, NULL);
        *outBuffer = VK_NULL_HANDLE;
        *outMemory = VK_NULL_HANDLE;
        return VKRT_ERROR_OPERATION_FAILED;
    }

    memcpy(mapped, hostData, (size_t)size);
    vkUnmapMemory(vkrt->core.device, *outMemory);

    if (outDeviceAddress) {
        *outDeviceAddress = queryBufferDeviceAddress(vkrt, *outBuffer);
    }
    return VKRT_SUCCESS;
}

VKRT_Result createDeviceBufferFromData(
    VKRT* vkrt,
    const void* hostData,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkBuffer* outBuffer,
    VkDeviceMemory* outMemory,
    VkDeviceAddress* outDeviceAddress
) {
    if (!vkrt || !hostData || !outBuffer || !outMemory) return VKRT_ERROR_INVALID_ARGUMENT;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    VKRT_Result result = createBuffer(
        vkrt,
        size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &stagingBuffer,
        &stagingMemory
    );
    if (result != VKRT_SUCCESS) return result;

    void* mapped = NULL;
    if (vkMapMemory(vkrt->core.device, stagingMemory, 0, size, 0, &mapped) != VK_SUCCESS || !mapped) {
        vkDestroyBuffer(vkrt->core.device, stagingBuffer, NULL);
        vkFreeMemory(vkrt->core.device, stagingMemory, NULL);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    memcpy(mapped, hostData, (size_t)size);
    vkUnmapMemory(vkrt->core.device, stagingMemory);

    result = createBuffer(
        vkrt,
        size,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        outBuffer,
        outMemory
    );
    if (result != VKRT_SUCCESS) {
        vkDestroyBuffer(vkrt->core.device, stagingBuffer, NULL);
        vkFreeMemory(vkrt->core.device, stagingMemory, NULL);
        return result;
    }

    result = appendPendingSceneTransfer(vkrt, stagingBuffer, stagingMemory, *outBuffer, size);
    if (result != VKRT_SUCCESS) {
        vkDestroyBuffer(vkrt->core.device, stagingBuffer, NULL);
        vkFreeMemory(vkrt->core.device, stagingMemory, NULL);
        vkDestroyBuffer(vkrt->core.device, *outBuffer, NULL);
        vkFreeMemory(vkrt->core.device, *outMemory, NULL);
        *outBuffer = VK_NULL_HANDLE;
        *outMemory = VK_NULL_HANDLE;
        return result;
    }

    if (outDeviceAddress) {
        *outDeviceAddress = queryBufferDeviceAddress(vkrt, *outBuffer);
    }
    return VKRT_SUCCESS;
}

void destroyBufferResources(VKRT* vkrt, Buffer* buffer) {
    if (!vkrt || !buffer) return;
    if (buffer->buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vkrt->core.device, buffer->buffer, NULL);
        buffer->buffer = VK_NULL_HANDLE;
    }
    if (buffer->memory != VK_NULL_HANDLE) {
        vkFreeMemory(vkrt->core.device, buffer->memory, NULL);
        buffer->memory = VK_NULL_HANDLE;
    }
    buffer->deviceAddress = 0;
    buffer->count = 0;
}

void destroyTransfer(VKRT* vkrt, FrameTransfer* transfer) {
    if (!vkrt || !transfer) return;
    if (transfer->buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vkrt->core.device, transfer->buffer, NULL);
        transfer->buffer = VK_NULL_HANDLE;
    }
    if (transfer->memory != VK_NULL_HANDLE) {
        vkFreeMemory(vkrt->core.device, transfer->memory, NULL);
        transfer->memory = VK_NULL_HANDLE;
    }
}
