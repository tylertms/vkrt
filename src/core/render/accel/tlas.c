#include "accel.h"

#include "buffer.h"
#include "device.h"
#include "scene.h"
#include "debug.h"

#include <stdlib.h>
#include <string.h>

static AccelerationStructure* getSceneTLAS(VKRT* vkrt) {
    return &vkrt->core.sceneTopLevelAccelerationStructure;
}

static Buffer* getSceneMeshData(VKRT* vkrt) {
    return &vkrt->core.sceneMeshData;
}

static FrameSceneUpdate* getCurrentFrameSceneUpdate(VKRT* vkrt) {
    return &vkrt->runtime.frameSceneUpdates[vkrt->runtime.currentFrame];
}

static void destroyBufferResources(VKRT* vkrt, Buffer* buffer) {
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

static void destroySceneBuffers(VKRT* vkrt) {
    if (!vkrt) return;

    Buffer* meshData = getSceneMeshData(vkrt);
    destroyBufferResources(vkrt, meshData);

    AccelerationStructure* tlas = getSceneTLAS(vkrt);
    if (tlas->structure != VK_NULL_HANDLE) {
        vkrt->core.procs.vkDestroyAccelerationStructureKHR(vkrt->core.device, tlas->structure, NULL);
        tlas->structure = VK_NULL_HANDLE;
    }
    if (tlas->buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vkrt->core.device, tlas->buffer, NULL);
        tlas->buffer = VK_NULL_HANDLE;
    }
    if (tlas->memory != VK_NULL_HANDLE) {
        vkFreeMemory(vkrt->core.device, tlas->memory, NULL);
        tlas->memory = VK_NULL_HANDLE;
    }
    tlas->deviceAddress = 0;
}

VKRT_Result createTopLevelAccelerationStructure(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    uint64_t startTime = getMicroseconds();
    destroySceneBuffers(vkrt);

    FrameSceneUpdate* update = getCurrentFrameSceneUpdate(vkrt);
    if (update->instanceBuffer.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vkrt->core.device, update->instanceBuffer.buffer, NULL);
        update->instanceBuffer.buffer = VK_NULL_HANDLE;
    }
    if (update->instanceBuffer.memory != VK_NULL_HANDLE) {
        vkFreeMemory(vkrt->core.device, update->instanceBuffer.memory, NULL);
        update->instanceBuffer.memory = VK_NULL_HANDLE;
    }
    if (update->tlasScratch.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vkrt->core.device, update->tlasScratch.buffer, NULL);
        update->tlasScratch.buffer = VK_NULL_HANDLE;
    }
    if (update->tlasScratch.memory != VK_NULL_HANDLE) {
        vkFreeMemory(vkrt->core.device, update->tlasScratch.memory, NULL);
        update->tlasScratch.memory = VK_NULL_HANDLE;
    }
    update->tlasBuildPending = VK_FALSE;

    uint32_t instanceCount = vkrt->core.meshCount;
    if (instanceCount == 0) {
        LOG_TRACE("TLAS prepared. Instances: 0, in %.3f ms", (double)(getMicroseconds() - startTime) / 1e3);
        return VKRT_SUCCESS;
    }

    VKRT_Result result = VKRT_SUCCESS;
    uint32_t graphicsFamily = vkrt->core.indices.graphics;
    VkAccelerationStructureInstanceKHR* instances = NULL;
    MeshInfo* meshInfos = NULL;
    Buffer* meshData = getSceneMeshData(vkrt);
    AccelerationStructure* tlas = getSceneTLAS(vkrt);

    instances = (VkAccelerationStructureInstanceKHR*)malloc(sizeof(*instances) * instanceCount);
    meshInfos = (MeshInfo*)malloc(sizeof(*meshInfos) * instanceCount);
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
        if (meshInfo.renderBackfaces) {
            instance.flags |= VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        }
        instance.accelerationStructureReference = vkrt->core.meshes[i].bottomLevelAccelerationStructure.deviceAddress;

        instances[i] = instance;
        meshInfos[i] = meshInfo;
    }

    result = createHostBufferFromData(
        vkrt,
        instances,
        sizeof(*instances) * instanceCount,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        &update->instanceBuffer.buffer,
        &update->instanceBuffer.memory,
        NULL);
    if (result != VKRT_SUCCESS) {
        goto cleanup;
    }

    VkDeviceSize meshInfoSize = sizeof(*meshInfos) * instanceCount;
    result = createDeviceBufferFromData(
        vkrt,
        meshInfos,
        meshInfoSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        &meshData->buffer,
        &meshData->memory,
        &meshData->deviceAddress);
    if (result != VKRT_SUCCESS) {
        goto cleanup;
    }
    meshData->count = instanceCount;

    VkBufferDeviceAddressInfo instanceAddressInfo = {0};
    instanceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    instanceAddressInfo.buffer = update->instanceBuffer.buffer;
    VkDeviceAddress instanceDeviceAddress =
        vkrt->core.procs.vkGetBufferDeviceAddressKHR(vkrt->core.device, &instanceAddressInfo);

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

    if (vkCreateBuffer(vkrt->core.device, &tlasBufferCreateInfo, NULL, &tlas->buffer) != VK_SUCCESS) {
        result = VKRT_ERROR_OPERATION_FAILED;
        goto cleanup;
    }

    VkMemoryRequirements tlasMemoryRequirements = {0};
    vkGetBufferMemoryRequirements(vkrt->core.device, tlas->buffer, &tlasMemoryRequirements);

    VkMemoryAllocateFlagsInfo allocateFlags = {0};
    allocateFlags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    allocateFlags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    VkMemoryAllocateInfo tlasAllocateInfo = {0};
    tlasAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    tlasAllocateInfo.pNext = &allocateFlags;
    tlasAllocateInfo.allocationSize = tlasMemoryRequirements.size;
    if (findMemoryType(vkrt,
            tlasMemoryRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &tlasAllocateInfo.memoryTypeIndex) != VKRT_SUCCESS) {
        result = VKRT_ERROR_OPERATION_FAILED;
        goto cleanup;
    }

    if (vkAllocateMemory(vkrt->core.device, &tlasAllocateInfo, NULL, &tlas->memory) != VK_SUCCESS) {
        result = VKRT_ERROR_OPERATION_FAILED;
        goto cleanup;
    }

    if (vkBindBufferMemory(vkrt->core.device, tlas->buffer, tlas->memory, 0) != VK_SUCCESS) {
        result = VKRT_ERROR_OPERATION_FAILED;
        goto cleanup;
    }

    VkAccelerationStructureCreateInfoKHR createInfo = {0};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = tlas->buffer;
    createInfo.size = buildSizesInfo.accelerationStructureSize;
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

    if (vkrt->core.procs.vkCreateAccelerationStructureKHR(
            vkrt->core.device,
            &createInfo,
            NULL,
            &tlas->structure) != VK_SUCCESS) {
        result = VKRT_ERROR_OPERATION_FAILED;
        goto cleanup;
    }

    VkAccelerationStructureDeviceAddressInfoKHR addressInfo = {0};
    addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addressInfo.accelerationStructure = tlas->structure;
    tlas->deviceAddress =
        vkrt->core.procs.vkGetAccelerationStructureDeviceAddressKHR(vkrt->core.device, &addressInfo);

    result = createBuffer(
        vkrt,
        buildSizesInfo.buildScratchSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        &update->tlasScratch.buffer,
        &update->tlasScratch.memory);
    if (result != VKRT_SUCCESS) {
        goto cleanup;
    }

    update->tlasBuildPending = VK_TRUE;

cleanup:
    free(instances);
    free(meshInfos);

    if (result != VKRT_SUCCESS) {
        destroySceneBuffers(vkrt);
        if (update->instanceBuffer.buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(vkrt->core.device, update->instanceBuffer.buffer, NULL);
            update->instanceBuffer.buffer = VK_NULL_HANDLE;
        }
        if (update->instanceBuffer.memory != VK_NULL_HANDLE) {
            vkFreeMemory(vkrt->core.device, update->instanceBuffer.memory, NULL);
            update->instanceBuffer.memory = VK_NULL_HANDLE;
        }
        if (update->tlasScratch.buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(vkrt->core.device, update->tlasScratch.buffer, NULL);
            update->tlasScratch.buffer = VK_NULL_HANDLE;
        }
        if (update->tlasScratch.memory != VK_NULL_HANDLE) {
            vkFreeMemory(vkrt->core.device, update->tlasScratch.memory, NULL);
            update->tlasScratch.memory = VK_NULL_HANDLE;
        }
        update->tlasBuildPending = VK_FALSE;
        return result;
    }

    LOG_TRACE("TLAS prepared. Instances: %u, in %.3f ms",
        instanceCount,
        (double)(getMicroseconds() - startTime) / 1e3);
    return VKRT_SUCCESS;
}

VKRT_Result recordTopLevelAccelerationStructureBuild(VKRT* vkrt, VkCommandBuffer commandBuffer) {
    if (!vkrt || commandBuffer == VK_NULL_HANDLE) return VKRT_ERROR_INVALID_ARGUMENT;

    FrameSceneUpdate* update = getCurrentFrameSceneUpdate(vkrt);
    if (!update->tlasBuildPending) return VKRT_SUCCESS;

    AccelerationStructure* tlas = getSceneTLAS(vkrt);
    uint32_t instanceCount = getSceneMeshData(vkrt)->count;
    if (tlas->structure == VK_NULL_HANDLE || update->instanceBuffer.buffer == VK_NULL_HANDLE || instanceCount == 0) {
        return VKRT_SUCCESS;
    }

    VkBufferDeviceAddressInfo instanceAddressInfo = {0};
    instanceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    instanceAddressInfo.buffer = update->instanceBuffer.buffer;
    VkDeviceAddress instanceDeviceAddress =
        vkrt->core.procs.vkGetBufferDeviceAddressKHR(vkrt->core.device, &instanceAddressInfo);

    VkBufferDeviceAddressInfo scratchAddressInfo = {0};
    scratchAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    scratchAddressInfo.buffer = update->tlasScratch.buffer;

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
    buildInfo.dstAccelerationStructure = tlas->structure;
    buildInfo.scratchData.deviceAddress =
        vkrt->core.procs.vkGetBufferDeviceAddressKHR(vkrt->core.device, &scratchAddressInfo);

    VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo = {0};
    buildRangeInfo.primitiveCount = instanceCount;
    const VkAccelerationStructureBuildRangeInfoKHR* buildRangeInfos[] = {&buildRangeInfo};

    vkrt->core.procs.vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &buildInfo, buildRangeInfos);
    return VKRT_SUCCESS;
}
