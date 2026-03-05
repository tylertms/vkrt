#include "accel.h"

#include "buffer.h"
#include "command/pool.h"
#include "device.h"
#include "scene.h"
#include "debug.h"

#include <stdlib.h>
#include <string.h>

VKRT_Result createTopLevelAccelerationStructure(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    VKRT_Result result = VKRT_SUCCESS;
    uint64_t startTime = getMicroseconds();

    if (vkrt->core.topLevelAccelerationStructure.structure != VK_NULL_HANDLE) {
        vkrt->core.procs.vkDestroyAccelerationStructureKHR(
            vkrt->core.device,
            vkrt->core.topLevelAccelerationStructure.structure,
            NULL);
        vkDestroyBuffer(vkrt->core.device, vkrt->core.topLevelAccelerationStructure.buffer, NULL);
        vkFreeMemory(vkrt->core.device, vkrt->core.topLevelAccelerationStructure.memory, NULL);

        vkrt->core.topLevelAccelerationStructure.structure = VK_NULL_HANDLE;
        vkrt->core.topLevelAccelerationStructure.buffer = VK_NULL_HANDLE;
        vkrt->core.topLevelAccelerationStructure.memory = VK_NULL_HANDLE;
        vkrt->core.topLevelAccelerationStructure.deviceAddress = 0;
    }

    if (vkrt->core.meshData.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vkrt->core.device, vkrt->core.meshData.buffer, NULL);
        vkrt->core.meshData.buffer = VK_NULL_HANDLE;
    }

    if (vkrt->core.meshData.memory != VK_NULL_HANDLE) {
        vkFreeMemory(vkrt->core.device, vkrt->core.meshData.memory, NULL);
        vkrt->core.meshData.memory = VK_NULL_HANDLE;
    }
    vkrt->core.meshData.deviceAddress = 0;

    uint32_t instanceCount = vkrt->core.meshData.count;
    if (instanceCount == 0) {
        LOG_TRACE("TLAS created. Instances: 0, in %.3f ms", (double)(getMicroseconds() - startTime) / 1e3);
        return VKRT_SUCCESS;
    }

    uint32_t graphicsFamily = vkrt->core.indices.graphics;
    VkAccelerationStructureInstanceKHR* instances = NULL;
    MeshInfo* meshInfos = NULL;
    VkBuffer instanceBuffer = VK_NULL_HANDLE;
    VkDeviceMemory instanceMemory = VK_NULL_HANDLE;
    VkBuffer stagingMeshInfo = VK_NULL_HANDLE;
    VkDeviceMemory stagingMeshInfoMem = VK_NULL_HANDLE;
    VkBuffer scratchBuffer = VK_NULL_HANDLE;
    VkDeviceMemory scratchDeviceMemory = VK_NULL_HANDLE;

    instances = (VkAccelerationStructureInstanceKHR*)malloc(sizeof(VkAccelerationStructureInstanceKHR) * instanceCount);
    meshInfos = (MeshInfo*)malloc(sizeof(MeshInfo) * instanceCount);
    if (!instances || !meshInfos) {
        result = VKRT_ERROR_OPERATION_FAILED;
        goto cleanup;
    }

    for (uint32_t i = 0; i < instanceCount; i++) {
        MeshInfo meshInfo = vkrt->core.meshes[i].info;

        VkAccelerationStructureInstanceKHR instance = {0};
        instance.transform = getMeshTransform(&meshInfo);
        instance.instanceCustomIndex = i;
        instance.mask = 0xFF;
        instance.instanceShaderBindingTableRecordOffset = 0;
        if (meshInfo.renderBackfaces) {
            instance.flags |= VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        }
        instance.accelerationStructureReference = vkrt->core.meshes[i].bottomLevelAccelerationStructure.deviceAddress;

        instances[i] = instance;
        meshInfos[i] = meshInfo;
    }

    VkBufferCreateInfo instanceBufferCreateInfo = {0};
    instanceBufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    instanceBufferCreateInfo.size = sizeof(VkAccelerationStructureInstanceKHR) * instanceCount;
    instanceBufferCreateInfo.usage =
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    instanceBufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    instanceBufferCreateInfo.queueFamilyIndexCount = 1;
    instanceBufferCreateInfo.pQueueFamilyIndices = &graphicsFamily;

    if (vkCreateBuffer(vkrt->core.device, &instanceBufferCreateInfo, NULL, &instanceBuffer) != VK_SUCCESS) {
        result = VKRT_ERROR_OPERATION_FAILED;
        goto cleanup;
    }

    VkMemoryRequirements instanceMemoryRequirements = {0};
    vkGetBufferMemoryRequirements(vkrt->core.device, instanceBuffer, &instanceMemoryRequirements);

    VkMemoryAllocateFlagsInfo memoryAllocateFlagsInfo = {0};
    memoryAllocateFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    memoryAllocateFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    VkMemoryAllocateInfo instanceMemoryAllocateInfo = {0};
    instanceMemoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    instanceMemoryAllocateInfo.pNext = &memoryAllocateFlagsInfo;
    instanceMemoryAllocateInfo.allocationSize = instanceMemoryRequirements.size;
    if (findMemoryType(
            vkrt,
            instanceMemoryRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &instanceMemoryAllocateInfo.memoryTypeIndex) != VKRT_SUCCESS) {
        result = VKRT_ERROR_OPERATION_FAILED;
        goto cleanup;
    }

    if (vkAllocateMemory(vkrt->core.device, &instanceMemoryAllocateInfo, NULL, &instanceMemory) != VK_SUCCESS) {
        result = VKRT_ERROR_OPERATION_FAILED;
        goto cleanup;
    }

    if (vkBindBufferMemory(vkrt->core.device, instanceBuffer, instanceMemory, 0) != VK_SUCCESS) {
        result = VKRT_ERROR_OPERATION_FAILED;
        goto cleanup;
    }

    void* mapped = NULL;
    if (vkMapMemory(vkrt->core.device, instanceMemory, 0, instanceMemoryRequirements.size, 0, &mapped) != VK_SUCCESS ||
        !mapped) {
        result = VKRT_ERROR_OPERATION_FAILED;
        goto cleanup;
    }
    memcpy(mapped, instances, sizeof(VkAccelerationStructureInstanceKHR) * instanceCount);
    vkUnmapMemory(vkrt->core.device, instanceMemory);

    VkDeviceSize meshInfoSize = sizeof(MeshInfo) * instanceCount;
    if ((result = createBuffer(
        vkrt,
        meshInfoSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &stagingMeshInfo,
        &stagingMeshInfoMem)) != VKRT_SUCCESS) goto cleanup;

    mapped = NULL;
    if (vkMapMemory(vkrt->core.device, stagingMeshInfoMem, 0, meshInfoSize, 0, &mapped) != VK_SUCCESS || !mapped) {
        result = VKRT_ERROR_OPERATION_FAILED;
        goto cleanup;
    }
    memcpy(mapped, meshInfos, (size_t)meshInfoSize);
    vkUnmapMemory(vkrt->core.device, stagingMeshInfoMem);

    if ((result = createBuffer(
        vkrt,
        meshInfoSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        &vkrt->core.meshData.buffer,
        &vkrt->core.meshData.memory)) != VKRT_SUCCESS) goto cleanup;

    if ((result = copyBuffer(vkrt, stagingMeshInfo, vkrt->core.meshData.buffer, meshInfoSize)) != VKRT_SUCCESS) goto cleanup;

    vkDestroyBuffer(vkrt->core.device, stagingMeshInfo, NULL);
    vkFreeMemory(vkrt->core.device, stagingMeshInfoMem, NULL);
    stagingMeshInfo = VK_NULL_HANDLE;
    stagingMeshInfoMem = VK_NULL_HANDLE;

    VkBufferDeviceAddressInfo instanceBufferDeviceAddressInfo = {0};
    instanceBufferDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    instanceBufferDeviceAddressInfo.buffer = instanceBuffer;
    VkDeviceAddress instanceDeviceAddress =
        vkrt->core.procs.vkGetBufferDeviceAddressKHR(vkrt->core.device, &instanceBufferDeviceAddressInfo);

    VkAccelerationStructureGeometryInstancesDataKHR geometryInstancesData = {0};
    geometryInstancesData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometryInstancesData.arrayOfPointers = VK_FALSE;
    geometryInstancesData.data.deviceAddress = instanceDeviceAddress;

    VkAccelerationStructureGeometryKHR accelerationGeometry = {0};
    accelerationGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    accelerationGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    accelerationGeometry.geometry.instances = geometryInstancesData;
    accelerationGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo = {0};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &accelerationGeometry;

    VkAccelerationStructureBuildSizesInfoKHR buildSizesInfo = {0};
    buildSizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vkrt->core.procs.vkGetAccelerationStructureBuildSizesKHR(
        vkrt->core.device,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &buildInfo,
        &instanceCount,
        &buildSizesInfo);

    VkBufferCreateInfo tlasBufferCreateInfo = {0};
    tlasBufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    tlasBufferCreateInfo.size = buildSizesInfo.accelerationStructureSize;
    tlasBufferCreateInfo.usage =
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    tlasBufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    tlasBufferCreateInfo.queueFamilyIndexCount = 1;
    tlasBufferCreateInfo.pQueueFamilyIndices = &graphicsFamily;

    if (vkCreateBuffer(
            vkrt->core.device,
            &tlasBufferCreateInfo,
            NULL,
            &vkrt->core.topLevelAccelerationStructure.buffer) != VK_SUCCESS) {
        result = VKRT_ERROR_OPERATION_FAILED;
        goto cleanup;
    }

    VkMemoryRequirements tlasBufferMemoryRequirements = {0};
    vkGetBufferMemoryRequirements(
        vkrt->core.device,
        vkrt->core.topLevelAccelerationStructure.buffer,
        &tlasBufferMemoryRequirements);

    VkMemoryAllocateInfo tlasMemoryAllocateInfo = {0};
    tlasMemoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    tlasMemoryAllocateInfo.pNext = &memoryAllocateFlagsInfo;
    tlasMemoryAllocateInfo.allocationSize = tlasBufferMemoryRequirements.size;
    if (findMemoryType(
            vkrt,
            tlasBufferMemoryRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &tlasMemoryAllocateInfo.memoryTypeIndex) != VKRT_SUCCESS) {
        result = VKRT_ERROR_OPERATION_FAILED;
        goto cleanup;
    }

    if (vkAllocateMemory(
            vkrt->core.device,
            &tlasMemoryAllocateInfo,
            NULL,
            &vkrt->core.topLevelAccelerationStructure.memory) != VK_SUCCESS) {
        result = VKRT_ERROR_OPERATION_FAILED;
        goto cleanup;
    }

    if (vkBindBufferMemory(vkrt->core.device,
            vkrt->core.topLevelAccelerationStructure.buffer,
            vkrt->core.topLevelAccelerationStructure.memory,
            0) != VK_SUCCESS) {
        result = VKRT_ERROR_OPERATION_FAILED;
        goto cleanup;
    }

    VkAccelerationStructureCreateInfoKHR accelerationStructureCreateInfo = {0};
    accelerationStructureCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    accelerationStructureCreateInfo.buffer = vkrt->core.topLevelAccelerationStructure.buffer;
    accelerationStructureCreateInfo.offset = 0;
    accelerationStructureCreateInfo.size = buildSizesInfo.accelerationStructureSize;
    accelerationStructureCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

    if (vkrt->core.procs.vkCreateAccelerationStructureKHR(
            vkrt->core.device,
            &accelerationStructureCreateInfo,
            NULL,
            &vkrt->core.topLevelAccelerationStructure.structure) != VK_SUCCESS) {
        result = VKRT_ERROR_OPERATION_FAILED;
        goto cleanup;
    }

    VkBufferCreateInfo scratchBufferCreateInfo = {0};
    scratchBufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    scratchBufferCreateInfo.size = buildSizesInfo.buildScratchSize;
    scratchBufferCreateInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    scratchBufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    scratchBufferCreateInfo.queueFamilyIndexCount = 1;
    scratchBufferCreateInfo.pQueueFamilyIndices = &graphicsFamily;

    if (vkCreateBuffer(vkrt->core.device, &scratchBufferCreateInfo, NULL, &scratchBuffer) != VK_SUCCESS) {
        result = VKRT_ERROR_OPERATION_FAILED;
        goto cleanup;
    }

    VkMemoryRequirements scratchMemoryRequirements = {0};
    vkGetBufferMemoryRequirements(vkrt->core.device, scratchBuffer, &scratchMemoryRequirements);

    VkMemoryAllocateInfo scratchMemoryAllocateInfo = {0};
    scratchMemoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    scratchMemoryAllocateInfo.pNext = &memoryAllocateFlagsInfo;
    scratchMemoryAllocateInfo.allocationSize = scratchMemoryRequirements.size;
    if (findMemoryType(
            vkrt,
            scratchMemoryRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &scratchMemoryAllocateInfo.memoryTypeIndex) != VKRT_SUCCESS) {
        result = VKRT_ERROR_OPERATION_FAILED;
        goto cleanup;
    }

    if (vkAllocateMemory(vkrt->core.device, &scratchMemoryAllocateInfo, NULL, &scratchDeviceMemory) != VK_SUCCESS) {
        result = VKRT_ERROR_OPERATION_FAILED;
        goto cleanup;
    }

    if (vkBindBufferMemory(vkrt->core.device, scratchBuffer, scratchDeviceMemory, 0) != VK_SUCCESS) {
        result = VKRT_ERROR_OPERATION_FAILED;
        goto cleanup;
    }

    VkBufferDeviceAddressInfo scratchBufferDeviceAddressInfo = {0};
    scratchBufferDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    scratchBufferDeviceAddressInfo.buffer = scratchBuffer;

    VkDeviceAddress scratchBufferDeviceAddress =
        vkrt->core.procs.vkGetBufferDeviceAddressKHR(vkrt->core.device, &scratchBufferDeviceAddressInfo);
    buildInfo.dstAccelerationStructure = vkrt->core.topLevelAccelerationStructure.structure;
    buildInfo.scratchData.deviceAddress = scratchBufferDeviceAddress;

    VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo = {0};
    buildRangeInfo.primitiveCount = instanceCount;
    const VkAccelerationStructureBuildRangeInfoKHR* pBuildRangeInfo = &buildRangeInfo;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if ((result = beginSingleTimeCommands(vkrt, &commandBuffer)) != VKRT_SUCCESS) goto cleanup;

    vkrt->core.procs.vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &buildInfo, &pBuildRangeInfo);

    VkMemoryBarrier memoryBarrier = {0};
    memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memoryBarrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    memoryBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
        0,
        1,
        &memoryBarrier,
        0,
        NULL,
        0,
        NULL);

    if ((result = endSingleTimeCommands(vkrt, commandBuffer)) != VKRT_SUCCESS) goto cleanup;

    VkAccelerationStructureDeviceAddressInfoKHR deviceAddressInfo = {0};
    deviceAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    deviceAddressInfo.accelerationStructure = vkrt->core.topLevelAccelerationStructure.structure;
    vkrt->core.topLevelAccelerationStructure.deviceAddress =
        vkrt->core.procs.vkGetAccelerationStructureDeviceAddressKHR(vkrt->core.device, &deviceAddressInfo);

cleanup:
    if (scratchBuffer != VK_NULL_HANDLE) vkDestroyBuffer(vkrt->core.device, scratchBuffer, NULL);
    if (scratchDeviceMemory != VK_NULL_HANDLE) vkFreeMemory(vkrt->core.device, scratchDeviceMemory, NULL);
    if (instanceBuffer != VK_NULL_HANDLE) vkDestroyBuffer(vkrt->core.device, instanceBuffer, NULL);
    if (instanceMemory != VK_NULL_HANDLE) vkFreeMemory(vkrt->core.device, instanceMemory, NULL);
    if (stagingMeshInfo != VK_NULL_HANDLE) vkDestroyBuffer(vkrt->core.device, stagingMeshInfo, NULL);
    if (stagingMeshInfoMem != VK_NULL_HANDLE) vkFreeMemory(vkrt->core.device, stagingMeshInfoMem, NULL);
    free(instances);
    free(meshInfos);

    if (result != VKRT_SUCCESS) {
        if (vkrt->core.topLevelAccelerationStructure.structure != VK_NULL_HANDLE) {
            vkrt->core.procs.vkDestroyAccelerationStructureKHR(
                vkrt->core.device,
                vkrt->core.topLevelAccelerationStructure.structure,
                NULL);
            vkrt->core.topLevelAccelerationStructure.structure = VK_NULL_HANDLE;
        }
        if (vkrt->core.topLevelAccelerationStructure.buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(vkrt->core.device, vkrt->core.topLevelAccelerationStructure.buffer, NULL);
            vkrt->core.topLevelAccelerationStructure.buffer = VK_NULL_HANDLE;
        }
        if (vkrt->core.topLevelAccelerationStructure.memory != VK_NULL_HANDLE) {
            vkFreeMemory(vkrt->core.device, vkrt->core.topLevelAccelerationStructure.memory, NULL);
            vkrt->core.topLevelAccelerationStructure.memory = VK_NULL_HANDLE;
        }
        vkrt->core.topLevelAccelerationStructure.deviceAddress = 0;

        if (vkrt->core.meshData.buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(vkrt->core.device, vkrt->core.meshData.buffer, NULL);
            vkrt->core.meshData.buffer = VK_NULL_HANDLE;
        }
        if (vkrt->core.meshData.memory != VK_NULL_HANDLE) {
            vkFreeMemory(vkrt->core.device, vkrt->core.meshData.memory, NULL);
            vkrt->core.meshData.memory = VK_NULL_HANDLE;
        }
        vkrt->core.meshData.deviceAddress = 0;
        return result;
    }

    LOG_TRACE("TLAS created. Instances: %u, in %.3f ms", instanceCount, (double)(getMicroseconds() - startTime) / 1e3);
    return VKRT_SUCCESS;
}
