#include "accel.h"
#include "buffer.h"
#include "device.h"
#include "rebuild.h"
#include "types.h"
#include "vkrt_engine_types.h"
#include "vkrt_internal.h"
#include "vkrt_types.h"
#include "vulkan/vulkan_core.h"

#include <stdint.h>
#include <stdlib.h>

static void buildBLASGeometryInfo(
    const MeshInfo* meshInfo,
    VkDeviceAddress vertexDataAddress,
    VkDeviceAddress indexDataAddress,
    VkAccelerationStructureGeometryKHR* outGeometry,
    VkAccelerationStructureBuildGeometryInfoKHR* outBuildInfo
) {
    VkAccelerationStructureGeometryTrianglesDataKHR trianglesData = {0};
    trianglesData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    trianglesData.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    trianglesData.vertexData.deviceAddress = vertexDataAddress;
    trianglesData.vertexStride = sizeof(ShaderVertex);
    trianglesData.maxVertex = meshInfo->vertexBase + meshInfo->vertexCount - 1;
    trianglesData.indexType = VK_INDEX_TYPE_UINT32;
    trianglesData.indexData.deviceAddress = indexDataAddress;

    *outGeometry = (VkAccelerationStructureGeometryKHR){0};
    outGeometry->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    outGeometry->geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    outGeometry->geometry.triangles = trianglesData;
    outGeometry->flags = VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR;

    *outBuildInfo = (VkAccelerationStructureBuildGeometryInfoKHR){0};
    outBuildInfo->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    outBuildInfo->type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    outBuildInfo->flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    outBuildInfo->mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    outBuildInfo->geometryCount = 1;
    outBuildInfo->pGeometries = outGeometry;
}

static VKRT_Result prepareBLAS(
    VKRT* vkrt,
    const MeshInfo* meshInfo,
    VkDeviceAddress vertexDataAddress,
    VkDeviceAddress indexDataAddress,
    AccelerationStructure* outAccelerationStructure
) {
    if (!vkrt || !meshInfo || !outAccelerationStructure) return VKRT_ERROR_INVALID_ARGUMENT;

    VkAccelerationStructureGeometryKHR geometry;
    VkAccelerationStructureBuildGeometryInfoKHR buildInfo;
    buildBLASGeometryInfo(meshInfo, vertexDataAddress, indexDataAddress, &geometry, &buildInfo);

    uint32_t primitiveCount = meshInfo->indexCount / 3;
    VkAccelerationStructureBuildSizesInfoKHR sizesInfo = {0};
    sizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vkrt->core.procs.vkGetAccelerationStructureBuildSizesKHR(
        vkrt->core.device,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &buildInfo,
        &primitiveCount,
        &sizesInfo
    );

    VkMemoryAllocateFlagsInfo allocFlags = {0};
    allocFlags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    allocFlags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    VkBufferCreateInfo blasBufferCreateInfo = {0};
    blasBufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    blasBufferCreateInfo.size = sizesInfo.accelerationStructureSize;
    blasBufferCreateInfo.usage =
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    blasBufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(vkrt->core.device, &blasBufferCreateInfo, NULL, &outAccelerationStructure->buffer) !=
        VK_SUCCESS) {
        return VKRT_ERROR_OPERATION_FAILED;
    }

    VkMemoryRequirements memReqs = {0};
    vkGetBufferMemoryRequirements(vkrt->core.device, outAccelerationStructure->buffer, &memReqs);

    VkMemoryAllocateInfo memAlloc = {0};
    memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memAlloc.pNext = &allocFlags;
    memAlloc.allocationSize = memReqs.size;
    if (findMemoryType(vkrt, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &memAlloc.memoryTypeIndex) !=
        VKRT_SUCCESS) {
        vkDestroyBuffer(vkrt->core.device, outAccelerationStructure->buffer, NULL);
        outAccelerationStructure->buffer = VK_NULL_HANDLE;
        return VKRT_ERROR_OPERATION_FAILED;
    }

    if (vkAllocateMemory(vkrt->core.device, &memAlloc, NULL, &outAccelerationStructure->memory) != VK_SUCCESS) {
        vkDestroyBuffer(vkrt->core.device, outAccelerationStructure->buffer, NULL);
        outAccelerationStructure->buffer = VK_NULL_HANDLE;
        return VKRT_ERROR_OPERATION_FAILED;
    }

    if (vkBindBufferMemory(vkrt->core.device, outAccelerationStructure->buffer, outAccelerationStructure->memory, 0) !=
        VK_SUCCESS) {
        vkDestroyBuffer(vkrt->core.device, outAccelerationStructure->buffer, NULL);
        vkFreeMemory(vkrt->core.device, outAccelerationStructure->memory, NULL);
        outAccelerationStructure->buffer = VK_NULL_HANDLE;
        outAccelerationStructure->memory = VK_NULL_HANDLE;
        return VKRT_ERROR_OPERATION_FAILED;
    }

    VkAccelerationStructureCreateInfoKHR asCreateInfo = {0};
    asCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    asCreateInfo.buffer = outAccelerationStructure->buffer;
    asCreateInfo.size = sizesInfo.accelerationStructureSize;
    asCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

    if (vkrt->core.procs.vkCreateAccelerationStructureKHR(
            vkrt->core.device,
            &asCreateInfo,
            NULL,
            &outAccelerationStructure->structure
        ) != VK_SUCCESS) {
        vkDestroyBuffer(vkrt->core.device, outAccelerationStructure->buffer, NULL);
        vkFreeMemory(vkrt->core.device, outAccelerationStructure->memory, NULL);
        outAccelerationStructure->buffer = VK_NULL_HANDLE;
        outAccelerationStructure->memory = VK_NULL_HANDLE;
        return VKRT_ERROR_OPERATION_FAILED;
    }

    VkAccelerationStructureDeviceAddressInfoKHR addrInfo = {0};
    addrInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addrInfo.accelerationStructure = outAccelerationStructure->structure;
    outAccelerationStructure->deviceAddress =
        vkrt->core.procs.vkGetAccelerationStructureDeviceAddressKHR(vkrt->core.device, &addrInfo);

    return VKRT_SUCCESS;
}

VKRT_Result createBottomLevelAccelerationStructureForGeometry(
    VKRT* vkrt,
    const MeshInfo* meshInfo,
    VkDeviceAddress vertexDataAddress,
    VkDeviceAddress indexDataAddress,
    AccelerationStructure* outAccelerationStructure
) {
    if (!vkrt || !meshInfo || !outAccelerationStructure) return VKRT_ERROR_INVALID_ARGUMENT;
    if (meshInfo->vertexCount == 0 || meshInfo->indexCount == 0) return VKRT_ERROR_INVALID_ARGUMENT;
    if (vertexDataAddress == 0 || indexDataAddress == 0) return VKRT_ERROR_INVALID_ARGUMENT;

    *outAccelerationStructure = (AccelerationStructure){0};
    return prepareBLAS(vkrt, meshInfo, vertexDataAddress, indexDataAddress, outAccelerationStructure);
}

VKRT_Result prepareBottomLevelAccelerationStructureBuilds(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    FrameSceneUpdate* update = vkrtCurrentFrameSceneUpdate(vkrt);
    vkrtCleanupPendingBLASBuilds(vkrt, update);

    uint32_t pendingCount = 0;
    for (uint32_t i = 0; i < vkrt->core.meshCount; i++) {
        if (vkrt->core.meshes[i].ownsGeometry && vkrt->core.meshes[i].blasBuildPending) {
            pendingCount++;
        }
    }

    if (pendingCount == 0) return VKRT_SUCCESS;

    update->blasBuilds = (PendingBLASBuild*)calloc(pendingCount, sizeof(PendingBLASBuild));
    if (!update->blasBuilds) return VKRT_ERROR_OPERATION_FAILED;
    update->blasBuildCount = pendingCount;

    uint32_t writeIndex = 0;
    for (uint32_t i = 0; i < vkrt->core.meshCount; i++) {
        Mesh* mesh = &vkrt->core.meshes[i];
        if (!mesh->ownsGeometry || !mesh->blasBuildPending) continue;

        uint32_t primitiveCount = mesh->info.indexCount / 3u;
        VkAccelerationStructureGeometryKHR geometry;
        VkAccelerationStructureBuildGeometryInfoKHR buildInfo;
        buildBLASGeometryInfo(
            &mesh->info,
            vkrt->core.vertexData.deviceAddress,
            vkrt->core.indexData.deviceAddress,
            &geometry,
            &buildInfo
        );

        VkAccelerationStructureBuildSizesInfoKHR sizesInfo = {0};
        sizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        vkrt->core.procs.vkGetAccelerationStructureBuildSizesKHR(
            vkrt->core.device,
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &buildInfo,
            &primitiveCount,
            &sizesInfo
        );

        PendingBLASBuild* build = &update->blasBuilds[writeIndex];
        build->meshIndex = i;
        if (createBuffer(
                vkrt,
                sizesInfo.buildScratchSize,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                &build->scratchBuffer,
                &build->scratchMemory
            ) != VKRT_SUCCESS) {
            update->blasBuildCount = writeIndex + 1u;
            return VKRT_ERROR_OPERATION_FAILED;
        }

        writeIndex++;
    }

    return VKRT_SUCCESS;
}

VKRT_Result recordBottomLevelAccelerationStructureBuilds(VKRT* vkrt, VkCommandBuffer commandBuffer) {
    if (!vkrt || commandBuffer == VK_NULL_HANDLE) return VKRT_ERROR_INVALID_ARGUMENT;

    FrameSceneUpdate* update = vkrtCurrentFrameSceneUpdate(vkrt);
    for (uint32_t i = 0; i < update->blasBuildCount; i++) {
        PendingBLASBuild* pending = &update->blasBuilds[i];
        Mesh* mesh = &vkrt->core.meshes[pending->meshIndex];
        if (!mesh->ownsGeometry || !mesh->blasBuildPending ||
            mesh->bottomLevelAccelerationStructure.structure == VK_NULL_HANDLE) {
            continue;
        }

        VkAccelerationStructureGeometryKHR geometry;
        VkAccelerationStructureBuildGeometryInfoKHR buildInfo;
        buildBLASGeometryInfo(
            &mesh->info,
            vkrt->core.vertexData.deviceAddress,
            vkrt->core.indexData.deviceAddress,
            &geometry,
            &buildInfo
        );
        buildInfo.dstAccelerationStructure = mesh->bottomLevelAccelerationStructure.structure;

        VkBufferDeviceAddressInfo scratchAddressInfo = {0};
        scratchAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        scratchAddressInfo.buffer = pending->scratchBuffer;
        buildInfo.scratchData.deviceAddress =
            vkrt->core.procs.vkGetBufferDeviceAddressKHR(vkrt->core.device, &scratchAddressInfo);

        uint32_t primitiveCount = mesh->info.indexCount / 3u;
        VkAccelerationStructureBuildRangeInfoKHR rangeInfo = {0};
        rangeInfo.primitiveCount = primitiveCount;
        rangeInfo.primitiveOffset = (uint32_t)(mesh->info.indexBase * sizeof(uint32_t));
        rangeInfo.firstVertex = mesh->info.vertexBase;
        const VkAccelerationStructureBuildRangeInfoKHR* rangeInfos[] = {&rangeInfo};

        vkrt->core.procs.vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &buildInfo, rangeInfos);
    }

    return VKRT_SUCCESS;
}
