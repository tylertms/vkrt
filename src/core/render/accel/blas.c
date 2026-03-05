#include "accel.h"

#include "command/pool.h"
#include "device.h"

#include <stdlib.h>

static VKRT_Result prepareBLAS(VKRT* vkrt, Mesh* mesh) {
    if (!vkrt || !mesh) return VKRT_ERROR_INVALID_ARGUMENT;

    mesh->bottomLevelAccelerationStructure.structure = VK_NULL_HANDLE;
    mesh->bottomLevelAccelerationStructure.buffer = VK_NULL_HANDLE;
    mesh->bottomLevelAccelerationStructure.memory = VK_NULL_HANDLE;
    mesh->bottomLevelAccelerationStructure.deviceAddress = 0;

    VkAccelerationStructureGeometryTrianglesDataKHR trianglesData = {0};
    trianglesData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    trianglesData.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    trianglesData.vertexData.deviceAddress = vkrt->core.vertexData.deviceAddress;
    trianglesData.vertexStride = sizeof(Vertex);
    trianglesData.maxVertex = mesh->info.vertexBase + mesh->info.vertexCount - 1;
    trianglesData.indexType = VK_INDEX_TYPE_UINT32;
    trianglesData.indexData.deviceAddress = vkrt->core.indexData.deviceAddress;

    VkAccelerationStructureGeometryKHR geometry = {0};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geometry.geometry.triangles = trianglesData;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo = {0};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    uint32_t primitiveCount = mesh->info.indexCount / 3;
    VkAccelerationStructureBuildSizesInfoKHR sizesInfo = {0};
    sizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vkrt->core.procs.vkGetAccelerationStructureBuildSizesKHR(
        vkrt->core.device,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &buildInfo,
        &primitiveCount,
        &sizesInfo);

    VkMemoryAllocateFlagsInfo allocFlags = {0};
    allocFlags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    allocFlags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    VkBufferCreateInfo blasBufferCreateInfo = {0};
    blasBufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    blasBufferCreateInfo.size = sizesInfo.accelerationStructureSize;
    blasBufferCreateInfo.usage =
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    blasBufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(vkrt->core.device, &blasBufferCreateInfo, NULL, &mesh->bottomLevelAccelerationStructure.buffer) !=
        VK_SUCCESS) {
        return VKRT_ERROR_OPERATION_FAILED;
    }

    VkMemoryRequirements memReqs = {0};
    vkGetBufferMemoryRequirements(vkrt->core.device, mesh->bottomLevelAccelerationStructure.buffer, &memReqs);

    VkMemoryAllocateInfo memAlloc = {0};
    memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memAlloc.pNext = &allocFlags;
    memAlloc.allocationSize = memReqs.size;
    if (findMemoryType(vkrt, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &memAlloc.memoryTypeIndex) !=
        VKRT_SUCCESS) {
        vkDestroyBuffer(vkrt->core.device, mesh->bottomLevelAccelerationStructure.buffer, NULL);
        mesh->bottomLevelAccelerationStructure.buffer = VK_NULL_HANDLE;
        return VKRT_ERROR_OPERATION_FAILED;
    }

    if (vkAllocateMemory(vkrt->core.device, &memAlloc, NULL, &mesh->bottomLevelAccelerationStructure.memory) != VK_SUCCESS) {
        vkDestroyBuffer(vkrt->core.device, mesh->bottomLevelAccelerationStructure.buffer, NULL);
        mesh->bottomLevelAccelerationStructure.buffer = VK_NULL_HANDLE;
        return VKRT_ERROR_OPERATION_FAILED;
    }

    if (vkBindBufferMemory(vkrt->core.device,
            mesh->bottomLevelAccelerationStructure.buffer,
            mesh->bottomLevelAccelerationStructure.memory,
            0) != VK_SUCCESS) {
        vkDestroyBuffer(vkrt->core.device, mesh->bottomLevelAccelerationStructure.buffer, NULL);
        vkFreeMemory(vkrt->core.device, mesh->bottomLevelAccelerationStructure.memory, NULL);
        mesh->bottomLevelAccelerationStructure.buffer = VK_NULL_HANDLE;
        mesh->bottomLevelAccelerationStructure.memory = VK_NULL_HANDLE;
        return VKRT_ERROR_OPERATION_FAILED;
    }

    VkAccelerationStructureCreateInfoKHR asCreateInfo = {0};
    asCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    asCreateInfo.buffer = mesh->bottomLevelAccelerationStructure.buffer;
    asCreateInfo.size = sizesInfo.accelerationStructureSize;
    asCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

    if (vkrt->core.procs.vkCreateAccelerationStructureKHR(
            vkrt->core.device,
            &asCreateInfo,
            NULL,
            &mesh->bottomLevelAccelerationStructure.structure) != VK_SUCCESS) {
        vkDestroyBuffer(vkrt->core.device, mesh->bottomLevelAccelerationStructure.buffer, NULL);
        vkFreeMemory(vkrt->core.device, mesh->bottomLevelAccelerationStructure.memory, NULL);
        mesh->bottomLevelAccelerationStructure.buffer = VK_NULL_HANDLE;
        mesh->bottomLevelAccelerationStructure.memory = VK_NULL_HANDLE;
        return VKRT_ERROR_OPERATION_FAILED;
    }

    VkAccelerationStructureDeviceAddressInfoKHR addrInfo = {0};
    addrInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addrInfo.accelerationStructure = mesh->bottomLevelAccelerationStructure.structure;
    mesh->bottomLevelAccelerationStructure.deviceAddress =
        vkrt->core.procs.vkGetAccelerationStructureDeviceAddressKHR(vkrt->core.device, &addrInfo);

    return VKRT_SUCCESS;
}

VKRT_Result createBottomLevelAccelerationStructure(VKRT* vkrt, Mesh* mesh) {
    if (prepareBLAS(vkrt, mesh) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;
    vkrt->core.topLevelAccelerationStructure.needsRebuild = 1;
    return VKRT_SUCCESS;
}

VKRT_Result buildAllBLAS(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    VKRT_Result result = VKRT_SUCCESS;
    uint32_t meshCount = vkrt->core.meshData.count;
    uint32_t buildCount = 0;

    for (uint32_t i = 0; i < meshCount; i++) {
        if (!vkrt->core.meshes[i].ownsGeometry) continue;
        if ((result = prepareBLAS(vkrt, &vkrt->core.meshes[i])) != VKRT_SUCCESS) return result;
        buildCount++;
    }

    if (buildCount == 0) {
        vkrt->core.topLevelAccelerationStructure.needsRebuild = 1;
        return VKRT_SUCCESS;
    }

    VkAccelerationStructureGeometryKHR* geometries =
        (VkAccelerationStructureGeometryKHR*)calloc(buildCount, sizeof(VkAccelerationStructureGeometryKHR));
    VkAccelerationStructureBuildGeometryInfoKHR* buildInfos =
        (VkAccelerationStructureBuildGeometryInfoKHR*)calloc(buildCount, sizeof(VkAccelerationStructureBuildGeometryInfoKHR));
    VkAccelerationStructureBuildRangeInfoKHR* rangeInfos =
        (VkAccelerationStructureBuildRangeInfoKHR*)calloc(buildCount, sizeof(VkAccelerationStructureBuildRangeInfoKHR));
    const VkAccelerationStructureBuildRangeInfoKHR** rangeInfoPtrs =
        (const VkAccelerationStructureBuildRangeInfoKHR**)calloc(buildCount, sizeof(void*));
    VkBuffer* scratchBuffers = (VkBuffer*)calloc(buildCount, sizeof(VkBuffer));
    VkDeviceMemory* scratchMemories = (VkDeviceMemory*)calloc(buildCount, sizeof(VkDeviceMemory));

    if (!geometries || !buildInfos || !rangeInfos || !rangeInfoPtrs || !scratchBuffers || !scratchMemories) {
        free(geometries);
        free(buildInfos);
        free(rangeInfos);
        free(rangeInfoPtrs);
        free(scratchBuffers);
        free(scratchMemories);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    VkMemoryAllocateFlagsInfo allocFlags = {0};
    allocFlags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    allocFlags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    uint32_t buildIndex = 0;
    for (uint32_t i = 0; i < meshCount; i++) {
        Mesh* mesh = &vkrt->core.meshes[i];
        if (!mesh->ownsGeometry) continue;

        uint32_t primitiveCount = mesh->info.indexCount / 3;

        VkAccelerationStructureGeometryTrianglesDataKHR trianglesData = {0};
        trianglesData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        trianglesData.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        trianglesData.vertexData.deviceAddress = vkrt->core.vertexData.deviceAddress;
        trianglesData.vertexStride = sizeof(Vertex);
        trianglesData.maxVertex = mesh->info.vertexBase + mesh->info.vertexCount - 1;
        trianglesData.indexType = VK_INDEX_TYPE_UINT32;
        trianglesData.indexData.deviceAddress = vkrt->core.indexData.deviceAddress;

        geometries[buildIndex].sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geometries[buildIndex].geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geometries[buildIndex].geometry.triangles = trianglesData;
        geometries[buildIndex].flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

        VkAccelerationStructureBuildSizesInfoKHR sizesInfo = {0};
        sizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

        buildInfos[buildIndex].sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        buildInfos[buildIndex].type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        buildInfos[buildIndex].flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        buildInfos[buildIndex].mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfos[buildIndex].geometryCount = 1;
        buildInfos[buildIndex].pGeometries = &geometries[buildIndex];

        vkrt->core.procs.vkGetAccelerationStructureBuildSizesKHR(
            vkrt->core.device,
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &buildInfos[buildIndex],
            &primitiveCount,
            &sizesInfo);

        VkBufferCreateInfo scratchCreateInfo = {0};
        scratchCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        scratchCreateInfo.size = sizesInfo.buildScratchSize;
        scratchCreateInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        scratchCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(vkrt->core.device, &scratchCreateInfo, NULL, &scratchBuffers[buildIndex]) != VK_SUCCESS) {
            result = VKRT_ERROR_OPERATION_FAILED;
            break;
        }

        VkMemoryRequirements scratchReqs = {0};
        vkGetBufferMemoryRequirements(vkrt->core.device, scratchBuffers[buildIndex], &scratchReqs);

        VkMemoryAllocateInfo scratchAlloc = {0};
        scratchAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        scratchAlloc.pNext = &allocFlags;
        scratchAlloc.allocationSize = scratchReqs.size;
        if (findMemoryType(vkrt, scratchReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &scratchAlloc.memoryTypeIndex) !=
            VKRT_SUCCESS) {
            result = VKRT_ERROR_OPERATION_FAILED;
            break;
        }

        if (vkAllocateMemory(vkrt->core.device, &scratchAlloc, NULL, &scratchMemories[buildIndex]) != VK_SUCCESS) {
            result = VKRT_ERROR_OPERATION_FAILED;
            break;
        }

        if (vkBindBufferMemory(vkrt->core.device, scratchBuffers[buildIndex], scratchMemories[buildIndex], 0) != VK_SUCCESS) {
            result = VKRT_ERROR_OPERATION_FAILED;
            break;
        }

        VkBufferDeviceAddressInfo scratchAddrInfo = {0};
        scratchAddrInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        scratchAddrInfo.buffer = scratchBuffers[buildIndex];

        buildInfos[buildIndex].dstAccelerationStructure = mesh->bottomLevelAccelerationStructure.structure;
        buildInfos[buildIndex].scratchData.deviceAddress =
            vkrt->core.procs.vkGetBufferDeviceAddressKHR(vkrt->core.device, &scratchAddrInfo);

        rangeInfos[buildIndex].primitiveCount = primitiveCount;
        rangeInfos[buildIndex].primitiveOffset = (uint32_t)(mesh->info.indexBase * sizeof(uint32_t));
        rangeInfos[buildIndex].firstVertex = mesh->info.vertexBase;
        rangeInfoPtrs[buildIndex] = &rangeInfos[buildIndex];

        buildIndex++;
    }

    if (result == VKRT_SUCCESS && buildIndex == buildCount) {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        if ((result = beginSingleTimeCommands(vkrt, &commandBuffer)) == VKRT_SUCCESS) {
            vkrt->core.procs.vkCmdBuildAccelerationStructuresKHR(commandBuffer, buildCount, buildInfos, rangeInfoPtrs);

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

            result = endSingleTimeCommands(vkrt, commandBuffer);
        }
    } else if (result == VKRT_SUCCESS) {
        result = VKRT_ERROR_OPERATION_FAILED;
    }

    for (uint32_t i = 0; i < buildCount; i++) {
        if (scratchBuffers[i] != VK_NULL_HANDLE) vkDestroyBuffer(vkrt->core.device, scratchBuffers[i], NULL);
        if (scratchMemories[i] != VK_NULL_HANDLE) vkFreeMemory(vkrt->core.device, scratchMemories[i], NULL);
    }

    free(geometries);
    free(buildInfos);
    free(rangeInfos);
    free(rangeInfoPtrs);
    free(scratchBuffers);
    free(scratchMemories);

    if (result != VKRT_SUCCESS) return result;

    vkrt->core.topLevelAccelerationStructure.needsRebuild = 1;
    return VKRT_SUCCESS;
}
