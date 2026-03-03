#include "accel.h"
#include "buffer.h"
#include "command.h"
#include "device.h"
#include "scene.h"
#include "debug.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void createShaderBindingTable(VKRT* vkrt) {
    uint64_t startTime = getMicroseconds();

    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingPipelineProperties = {0};
    rayTracingPipelineProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;

    VkPhysicalDeviceProperties2 physicalDeviceProperties2 = {0};
    physicalDeviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    physicalDeviceProperties2.pNext = &rayTracingPipelineProperties;

    vkGetPhysicalDeviceProperties2(vkrt->core.physicalDevice, &physicalDeviceProperties2);

    uint32_t groupCount = 3;
    VkDeviceSize handleSize = rayTracingPipelineProperties.shaderGroupHandleSize;
    VkDeviceSize stride = rayTracingPipelineProperties.shaderGroupBaseAlignment;
    VkDeviceSize sbtSize = groupCount * stride;

    VkBuffer stageBuffer;
    VkDeviceMemory stageMemory;

    createBuffer(vkrt, sbtSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stageBuffer, &stageMemory);

    uint8_t* handles = (uint8_t*)malloc(groupCount * handleSize);
    if (!handles) {
        vkDestroyBuffer(vkrt->core.device, stageBuffer, NULL);
        vkFreeMemory(vkrt->core.device, stageMemory, NULL);
        LOG_ERROR("Failed to allocate shader group handle buffer");
        exit(EXIT_FAILURE);
    }
    vkrt->core.procs.vkGetRayTracingShaderGroupHandlesKHR(vkrt->core.device, vkrt->core.rayTracingPipeline, 0, groupCount, groupCount * handleSize, handles);

    void* mapped;
    vkMapMemory(vkrt->core.device, stageMemory, 0, sbtSize, 0, &mapped);
    for (uint32_t i = 0; i < groupCount; i++) {
        memcpy((uint8_t*)mapped + i * stride, handles + i * handleSize, handleSize);
    }
    vkUnmapMemory(vkrt->core.device, stageMemory);
    free(handles);

    VkBufferCreateInfo bufferCreateInfo = {0};
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.size = sbtSize;
    bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(vkrt->core.device, &bufferCreateInfo, NULL, &vkrt->core.shaderBindingTableBuffer);

    VkMemoryRequirements memoryRequirements;
    vkGetBufferMemoryRequirements(vkrt->core.device, vkrt->core.shaderBindingTableBuffer, &memoryRequirements);

    VkMemoryAllocateFlagsInfo memoryAllocateFlagsInfo = {0};
    memoryAllocateFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    memoryAllocateFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    VkMemoryAllocateInfo memoryAllocateInfo = {0};
    memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memoryAllocateInfo.pNext = &memoryAllocateFlagsInfo;
    memoryAllocateInfo.allocationSize = memoryRequirements.size;
    memoryAllocateInfo.memoryTypeIndex = findMemoryType(vkrt, memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vkAllocateMemory(vkrt->core.device, &memoryAllocateInfo, NULL, &vkrt->core.shaderBindingTableMemory);
    vkBindBufferMemory(vkrt->core.device, vkrt->core.shaderBindingTableBuffer, vkrt->core.shaderBindingTableMemory, 0);

    copyBuffer(vkrt, stageBuffer, vkrt->core.shaderBindingTableBuffer, sbtSize);
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
}

static void prepareBLAS(VKRT* vkrt, Mesh* mesh) {
    VkAccelerationStructureGeometryTrianglesDataKHR trianglesData = {0};
    trianglesData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    trianglesData.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    trianglesData.vertexData.deviceAddress = vkrt->core.vertexData.deviceAddress;
    trianglesData.vertexStride = sizeof(Vertex);
    trianglesData.maxVertex = mesh->info.vertexBase + mesh->info.vertexCount - 1;
    trianglesData.indexType = VK_INDEX_TYPE_UINT32;
    trianglesData.indexData.deviceAddress = vkrt->core.indexData.deviceAddress;
    trianglesData.transformData.deviceAddress = 0;

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
    vkrt->core.procs.vkGetAccelerationStructureBuildSizesKHR(vkrt->core.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &primitiveCount, &sizesInfo);

    VkMemoryAllocateFlagsInfo allocFlags = {0};
    allocFlags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    allocFlags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    VkBufferCreateInfo blasBufferCreateInfo = {0};
    blasBufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    blasBufferCreateInfo.size = sizesInfo.accelerationStructureSize;
    blasBufferCreateInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    blasBufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(vkrt->core.device, &blasBufferCreateInfo, NULL, &mesh->bottomLevelAccelerationStructure.buffer) != VK_SUCCESS) {
        LOG_ERROR("Failed to create BLAS buffer");
        exit(EXIT_FAILURE);
    }

    VkMemoryRequirements memReqs = {0};
    vkGetBufferMemoryRequirements(vkrt->core.device, mesh->bottomLevelAccelerationStructure.buffer, &memReqs);

    VkMemoryAllocateInfo memAlloc = {0};
    memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memAlloc.pNext = &allocFlags;
    memAlloc.allocationSize = memReqs.size;
    memAlloc.memoryTypeIndex = findMemoryType(vkrt, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(vkrt->core.device, &memAlloc, NULL, &mesh->bottomLevelAccelerationStructure.memory) != VK_SUCCESS) {
        LOG_ERROR("Failed to allocate BLAS memory");
        exit(EXIT_FAILURE);
    }
    vkBindBufferMemory(vkrt->core.device, mesh->bottomLevelAccelerationStructure.buffer, mesh->bottomLevelAccelerationStructure.memory, 0);

    VkAccelerationStructureCreateInfoKHR asCreateInfo = {0};
    asCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    asCreateInfo.buffer = mesh->bottomLevelAccelerationStructure.buffer;
    asCreateInfo.size = sizesInfo.accelerationStructureSize;
    asCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

    if (vkrt->core.procs.vkCreateAccelerationStructureKHR(vkrt->core.device, &asCreateInfo, NULL, &mesh->bottomLevelAccelerationStructure.structure) != VK_SUCCESS) {
        LOG_ERROR("Failed to create BLAS");
        exit(EXIT_FAILURE);
    }

    VkAccelerationStructureDeviceAddressInfoKHR addrInfo = {0};
    addrInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addrInfo.accelerationStructure = mesh->bottomLevelAccelerationStructure.structure;
    mesh->bottomLevelAccelerationStructure.deviceAddress = vkrt->core.procs.vkGetAccelerationStructureDeviceAddressKHR(vkrt->core.device, &addrInfo);
}

void createBottomLevelAccelerationStructure(VKRT* vkrt, Mesh* mesh) {
    prepareBLAS(vkrt, mesh);
    vkrt->core.topLevelAccelerationStructure.needsRebuild = 1;
}

void buildAllBLAS(VKRT* vkrt) {
    uint32_t meshCount = vkrt->core.meshData.count;
    uint32_t buildCount = 0;

    for (uint32_t i = 0; i < meshCount; i++) {
        if (!vkrt->core.meshes[i].ownsGeometry) continue;
        prepareBLAS(vkrt, &vkrt->core.meshes[i]);
        buildCount++;
    }

    if (buildCount == 0) {
        vkrt->core.topLevelAccelerationStructure.needsRebuild = 1;
        return;
    }

    VkAccelerationStructureGeometryKHR* geometries = (VkAccelerationStructureGeometryKHR*)malloc(buildCount * sizeof(VkAccelerationStructureGeometryKHR));
    VkAccelerationStructureBuildGeometryInfoKHR* buildInfos = (VkAccelerationStructureBuildGeometryInfoKHR*)malloc(buildCount * sizeof(VkAccelerationStructureBuildGeometryInfoKHR));
    VkAccelerationStructureBuildRangeInfoKHR* rangeInfos = (VkAccelerationStructureBuildRangeInfoKHR*)malloc(buildCount * sizeof(VkAccelerationStructureBuildRangeInfoKHR));
    const VkAccelerationStructureBuildRangeInfoKHR** rangeInfoPtrs = (const VkAccelerationStructureBuildRangeInfoKHR**)malloc(buildCount * sizeof(void*));
    VkBuffer* scratchBuffers = (VkBuffer*)calloc(buildCount, sizeof(VkBuffer));
    VkDeviceMemory* scratchMemories = (VkDeviceMemory*)calloc(buildCount, sizeof(VkDeviceMemory));

    if (!geometries || !buildInfos || !rangeInfos || !rangeInfoPtrs || !scratchBuffers || !scratchMemories) {
        free(geometries); free(buildInfos); free(rangeInfos); free(rangeInfoPtrs);
        free(scratchBuffers); free(scratchMemories);
        LOG_ERROR("Failed to allocate BLAS batch build arrays");
        exit(EXIT_FAILURE);
    }

    VkMemoryAllocateFlagsInfo allocFlags = {0};
    allocFlags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    allocFlags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    uint32_t buildIndex = 0;
    for (uint32_t i = 0; i < meshCount; i++) {
        Mesh* m = &vkrt->core.meshes[i];
        if (!m->ownsGeometry) continue;

        uint32_t primitiveCount = m->info.indexCount / 3;

        VkAccelerationStructureGeometryTrianglesDataKHR trianglesData = {0};
        trianglesData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        trianglesData.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        trianglesData.vertexData.deviceAddress = vkrt->core.vertexData.deviceAddress;
        trianglesData.vertexStride = sizeof(Vertex);
        trianglesData.maxVertex = m->info.vertexBase + m->info.vertexCount - 1;
        trianglesData.indexType = VK_INDEX_TYPE_UINT32;
        trianglesData.indexData.deviceAddress = vkrt->core.indexData.deviceAddress;

        geometries[buildIndex] = (VkAccelerationStructureGeometryKHR){0};
        geometries[buildIndex].sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geometries[buildIndex].geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geometries[buildIndex].geometry.triangles = trianglesData;
        geometries[buildIndex].flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

        VkAccelerationStructureBuildSizesInfoKHR sizesInfo = {0};
        sizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

        buildInfos[buildIndex] = (VkAccelerationStructureBuildGeometryInfoKHR){0};
        buildInfos[buildIndex].sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        buildInfos[buildIndex].type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        buildInfos[buildIndex].flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        buildInfos[buildIndex].mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfos[buildIndex].geometryCount = 1;
        buildInfos[buildIndex].pGeometries = &geometries[buildIndex];

        vkrt->core.procs.vkGetAccelerationStructureBuildSizesKHR(vkrt->core.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfos[buildIndex], &primitiveCount, &sizesInfo);

        VkBufferCreateInfo scratchCreateInfo = {0};
        scratchCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        scratchCreateInfo.size = sizesInfo.buildScratchSize;
        scratchCreateInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        scratchCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(vkrt->core.device, &scratchCreateInfo, NULL, &scratchBuffers[buildIndex]) != VK_SUCCESS) {
            LOG_ERROR("Failed to create BLAS scratch buffer");
            exit(EXIT_FAILURE);
        }

        VkMemoryRequirements scratchReqs = {0};
        vkGetBufferMemoryRequirements(vkrt->core.device, scratchBuffers[buildIndex], &scratchReqs);

        VkMemoryAllocateInfo scratchAlloc = {0};
        scratchAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        scratchAlloc.pNext = &allocFlags;
        scratchAlloc.allocationSize = scratchReqs.size;
        scratchAlloc.memoryTypeIndex = findMemoryType(vkrt, scratchReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(vkrt->core.device, &scratchAlloc, NULL, &scratchMemories[buildIndex]) != VK_SUCCESS) {
            LOG_ERROR("Failed to allocate BLAS scratch memory");
            exit(EXIT_FAILURE);
        }
        vkBindBufferMemory(vkrt->core.device, scratchBuffers[buildIndex], scratchMemories[buildIndex], 0);

        VkBufferDeviceAddressInfo scratchAddrInfo = {0};
        scratchAddrInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        scratchAddrInfo.buffer = scratchBuffers[buildIndex];

        buildInfos[buildIndex].dstAccelerationStructure = m->bottomLevelAccelerationStructure.structure;
        buildInfos[buildIndex].scratchData.deviceAddress = vkrt->core.procs.vkGetBufferDeviceAddressKHR(vkrt->core.device, &scratchAddrInfo);

        rangeInfos[buildIndex] = (VkAccelerationStructureBuildRangeInfoKHR){0};
        rangeInfos[buildIndex].primitiveCount = primitiveCount;
        rangeInfos[buildIndex].primitiveOffset = (uint32_t)(m->info.indexBase * sizeof(uint32_t));
        rangeInfos[buildIndex].firstVertex = m->info.vertexBase;
        rangeInfoPtrs[buildIndex] = &rangeInfos[buildIndex];

        buildIndex++;
    }

    VkCommandBuffer commandBuffer = beginSingleTimeCommands(vkrt);
    vkrt->core.procs.vkCmdBuildAccelerationStructuresKHR(commandBuffer, buildCount, buildInfos, rangeInfoPtrs);

    VkMemoryBarrier memoryBarrier = {0};
    memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memoryBarrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    memoryBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 0, 1, &memoryBarrier, 0, NULL, 0, NULL);

    endSingleTimeCommands(vkrt, commandBuffer);

    for (uint32_t i = 0; i < buildCount; i++) {
        vkDestroyBuffer(vkrt->core.device, scratchBuffers[i], NULL);
        vkFreeMemory(vkrt->core.device, scratchMemories[i], NULL);
    }

    free(geometries);
    free(buildInfos);
    free(rangeInfos);
    free(rangeInfoPtrs);
    free(scratchBuffers);
    free(scratchMemories);

    vkrt->core.topLevelAccelerationStructure.needsRebuild = 1;
}

void createTopLevelAccelerationStructure(VKRT* vkrt) {
    uint64_t startTime = getMicroseconds();

    if (vkrt->core.topLevelAccelerationStructure.structure) {
        vkrt->core.procs.vkDestroyAccelerationStructureKHR(vkrt->core.device, vkrt->core.topLevelAccelerationStructure.structure, NULL);
        vkDestroyBuffer(vkrt->core.device, vkrt->core.topLevelAccelerationStructure.buffer, NULL);
        vkFreeMemory(vkrt->core.device, vkrt->core.topLevelAccelerationStructure.memory, NULL);

        vkrt->core.topLevelAccelerationStructure.structure = VK_NULL_HANDLE;
        vkrt->core.topLevelAccelerationStructure.buffer = VK_NULL_HANDLE;
        vkrt->core.topLevelAccelerationStructure.memory = VK_NULL_HANDLE;
    }

    if (vkrt->core.meshData.buffer) {
        vkDestroyBuffer(vkrt->core.device, vkrt->core.meshData.buffer, NULL);
        vkrt->core.meshData.buffer = VK_NULL_HANDLE;
    }

    if (vkrt->core.meshData.memory) {
        vkFreeMemory(vkrt->core.device, vkrt->core.meshData.memory, NULL);
        vkrt->core.meshData.memory = VK_NULL_HANDLE;
    }

    uint32_t instanceCount = vkrt->core.meshData.count;
    if (instanceCount == 0) {
        vkrt->core.topLevelAccelerationStructure.deviceAddress = 0;
        LOG_TRACE("TLAS created. Instances: 0, in %.3f ms", (double)(getMicroseconds() - startTime) / 1e3);
        return;
    }

    uint32_t graphicsFamily = vkrt->core.indices.graphics;
    VkAccelerationStructureInstanceKHR* instances = (VkAccelerationStructureInstanceKHR*)malloc(sizeof(VkAccelerationStructureInstanceKHR) * instanceCount);
    MeshInfo* meshInfos = (MeshInfo*)malloc(sizeof(MeshInfo) * instanceCount);
    if (!instances || !meshInfos) {
        free(instances);
        free(meshInfos);
        LOG_ERROR("Failed to allocate TLAS instance staging data");
        exit(EXIT_FAILURE);
    }

    for (uint32_t i = 0; i < instanceCount; i++) {
        VkAccelerationStructureInstanceKHR inst = {0};

        MeshInfo meshInfo = vkrt->core.meshes[i].info;
        inst.transform = getMeshTransform(&meshInfo);
        inst.instanceCustomIndex = i;
        inst.mask = 0xFF;
        inst.instanceShaderBindingTableRecordOffset = 0;
        inst.flags = 0;
        if (meshInfo.renderBackfaces) {
            inst.flags |= VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        }
        inst.accelerationStructureReference = vkrt->core.meshes[i].bottomLevelAccelerationStructure.deviceAddress;

        instances[i] = inst;
        meshInfos[i] = meshInfo;
    }

    VkBufferCreateInfo instanceBufferCreateInfo = {0};
    instanceBufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    instanceBufferCreateInfo.size = sizeof(VkAccelerationStructureInstanceKHR) * instanceCount;
    instanceBufferCreateInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    instanceBufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    instanceBufferCreateInfo.queueFamilyIndexCount = 1;
    instanceBufferCreateInfo.pQueueFamilyIndices = &graphicsFamily;

    VkBuffer instanceBuffer;
    if (vkCreateBuffer(vkrt->core.device, &instanceBufferCreateInfo, NULL, &instanceBuffer) != VK_SUCCESS) {
        LOG_ERROR("Failed to create instance buffer");
        exit(EXIT_FAILURE);
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
    instanceMemoryAllocateInfo.memoryTypeIndex = findMemoryType(vkrt, instanceMemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VkDeviceMemory instanceMemory;
    if (vkAllocateMemory(vkrt->core.device, &instanceMemoryAllocateInfo, NULL, &instanceMemory) != VK_SUCCESS) {
        LOG_ERROR("Failed to allocate instance memory");
        exit(EXIT_FAILURE);
    }
    vkBindBufferMemory(vkrt->core.device, instanceBuffer, instanceMemory, 0);

    void* mapped;
    vkMapMemory(vkrt->core.device, instanceMemory, 0, instanceMemoryRequirements.size, 0, &mapped);
    memcpy(mapped, instances, sizeof(VkAccelerationStructureInstanceKHR) * instanceCount);
    vkUnmapMemory(vkrt->core.device, instanceMemory);
    free(instances);

    VkDeviceSize meshInfoSize = sizeof(MeshInfo) * instanceCount;
    VkBuffer stagingMeshInfo;
    VkDeviceMemory stagingMeshInfoMem;
    createBuffer(vkrt, meshInfoSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingMeshInfo, &stagingMeshInfoMem);
    vkMapMemory(vkrt->core.device, stagingMeshInfoMem, 0, meshInfoSize, 0, &mapped);
    memcpy(mapped, meshInfos, (size_t)meshInfoSize);
    vkUnmapMemory(vkrt->core.device, stagingMeshInfoMem);
    free(meshInfos);

    createBuffer(vkrt, meshInfoSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &vkrt->core.meshData.buffer, &vkrt->core.meshData.memory);
    copyBuffer(vkrt, stagingMeshInfo, vkrt->core.meshData.buffer, meshInfoSize);
    vkDestroyBuffer(vkrt->core.device, stagingMeshInfo, NULL);
    vkFreeMemory(vkrt->core.device, stagingMeshInfoMem, NULL);

    VkBufferDeviceAddressInfo instanceBufferDeviceAddressInfo = {0};
    instanceBufferDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    instanceBufferDeviceAddressInfo.buffer = instanceBuffer;
    VkDeviceAddress instanceDeviceAddress = vkrt->core.procs.vkGetBufferDeviceAddressKHR(vkrt->core.device, &instanceBufferDeviceAddressInfo);

    VkAccelerationStructureGeometryInstancesDataKHR accelerationStructureGeometryInstancesData = {0};
    accelerationStructureGeometryInstancesData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    accelerationStructureGeometryInstancesData.arrayOfPointers = VK_FALSE;
    accelerationStructureGeometryInstancesData.data.deviceAddress = instanceDeviceAddress;

    VkAccelerationStructureGeometryKHR accelerationStructureGeometry = {0};
    accelerationStructureGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    accelerationStructureGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    accelerationStructureGeometry.geometry.instances = accelerationStructureGeometryInstancesData;
    accelerationStructureGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

    VkAccelerationStructureBuildGeometryInfoKHR accelerationStructureBuildGeometryInfo = {0};
    accelerationStructureBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    accelerationStructureBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    accelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    accelerationStructureBuildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    accelerationStructureBuildGeometryInfo.geometryCount = 1;
    accelerationStructureBuildGeometryInfo.pGeometries = &accelerationStructureGeometry;
    accelerationStructureBuildGeometryInfo.srcAccelerationStructure = VK_NULL_HANDLE;
    accelerationStructureBuildGeometryInfo.dstAccelerationStructure = VK_NULL_HANDLE;
    accelerationStructureBuildGeometryInfo.scratchData.deviceAddress = 0;

    VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo = {0};
    accelerationStructureBuildSizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vkrt->core.procs.vkGetAccelerationStructureBuildSizesKHR(vkrt->core.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &accelerationStructureBuildGeometryInfo, &instanceCount, &accelerationStructureBuildSizesInfo);

    VkBufferCreateInfo tlasBufferCreateInfo = {0};
    tlasBufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    tlasBufferCreateInfo.size = accelerationStructureBuildSizesInfo.accelerationStructureSize;
    tlasBufferCreateInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    tlasBufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    tlasBufferCreateInfo.queueFamilyIndexCount = 1;
    tlasBufferCreateInfo.pQueueFamilyIndices = &graphicsFamily;

    if (vkCreateBuffer(vkrt->core.device, &tlasBufferCreateInfo, NULL, &vkrt->core.topLevelAccelerationStructure.buffer) != VK_SUCCESS) {
        LOG_ERROR("Failed to create TLAS buffer");
        exit(EXIT_FAILURE);
    }
    VkMemoryRequirements bufferMemoryRequirements = {0};
    vkGetBufferMemoryRequirements(vkrt->core.device, vkrt->core.topLevelAccelerationStructure.buffer, &bufferMemoryRequirements);

    VkMemoryAllocateInfo bufferMemoryAllocateInfo = {0};
    bufferMemoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    bufferMemoryAllocateInfo.pNext = &memoryAllocateFlagsInfo;
    bufferMemoryAllocateInfo.allocationSize = bufferMemoryRequirements.size;
    bufferMemoryAllocateInfo.memoryTypeIndex = findMemoryType(vkrt, bufferMemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(vkrt->core.device, &bufferMemoryAllocateInfo, NULL, &vkrt->core.topLevelAccelerationStructure.memory) != VK_SUCCESS) {
        LOG_ERROR("Failed to allocate top-level acceleration structure memory");
        exit(EXIT_FAILURE);
    }
    vkBindBufferMemory(vkrt->core.device, vkrt->core.topLevelAccelerationStructure.buffer, vkrt->core.topLevelAccelerationStructure.memory, 0);

    VkAccelerationStructureCreateInfoKHR accelerationStructureCreateInfo = {0};
    accelerationStructureCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    accelerationStructureCreateInfo.buffer = vkrt->core.topLevelAccelerationStructure.buffer;
    accelerationStructureCreateInfo.offset = 0;
    accelerationStructureCreateInfo.size = accelerationStructureBuildSizesInfo.accelerationStructureSize;
    accelerationStructureCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

    if (vkrt->core.procs.vkCreateAccelerationStructureKHR(vkrt->core.device, &accelerationStructureCreateInfo, NULL, &vkrt->core.topLevelAccelerationStructure.structure) != VK_SUCCESS) {
        LOG_ERROR("Failed to create TLAS");
        exit(EXIT_FAILURE);
    };

    VkBufferCreateInfo scratchBufferCreateInfo = {0};
    scratchBufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    scratchBufferCreateInfo.size = accelerationStructureBuildSizesInfo.buildScratchSize;
    scratchBufferCreateInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    scratchBufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    scratchBufferCreateInfo.queueFamilyIndexCount = 1;
    scratchBufferCreateInfo.pQueueFamilyIndices = &graphicsFamily;

    VkBuffer scratchBuffer;
    vkCreateBuffer(vkrt->core.device, &scratchBufferCreateInfo, NULL, &scratchBuffer);
    VkMemoryRequirements scratchBufferMemoryRequirements = {0};
    vkGetBufferMemoryRequirements(vkrt->core.device, scratchBuffer, &scratchBufferMemoryRequirements);

    VkMemoryAllocateInfo scratchMemoryAllocateInfo = {0};
    scratchMemoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    scratchMemoryAllocateInfo.pNext = &memoryAllocateFlagsInfo;
    scratchMemoryAllocateInfo.allocationSize = scratchBufferMemoryRequirements.size;
    scratchMemoryAllocateInfo.memoryTypeIndex = findMemoryType(vkrt, scratchBufferMemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkDeviceMemory scratchDeviceMemory;
    vkAllocateMemory(vkrt->core.device, &scratchMemoryAllocateInfo, NULL, &scratchDeviceMemory);
    vkBindBufferMemory(vkrt->core.device, scratchBuffer, scratchDeviceMemory, 0);

    VkBufferDeviceAddressInfo scratchBufferDeviceAddressInfo = {0};
    scratchBufferDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    scratchBufferDeviceAddressInfo.buffer = scratchBuffer;

    VkDeviceAddress scratchBufferDeviceAddress = vkrt->core.procs.vkGetBufferDeviceAddressKHR(vkrt->core.device, &scratchBufferDeviceAddressInfo);
    accelerationStructureBuildGeometryInfo.dstAccelerationStructure = vkrt->core.topLevelAccelerationStructure.structure;
    accelerationStructureBuildGeometryInfo.scratchData.deviceAddress = scratchBufferDeviceAddress;

    VkAccelerationStructureBuildRangeInfoKHR accelerationStructureBuildRangeInfo = {0};
    accelerationStructureBuildRangeInfo.primitiveCount = instanceCount;
    accelerationStructureBuildRangeInfo.primitiveOffset = 0;
    accelerationStructureBuildRangeInfo.firstVertex = 0;
    accelerationStructureBuildRangeInfo.transformOffset = 0;
    const VkAccelerationStructureBuildRangeInfoKHR* pBuildRangeInfo = &accelerationStructureBuildRangeInfo;

    VkCommandBuffer commandBuffer = beginSingleTimeCommands(vkrt);
    vkrt->core.procs.vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &accelerationStructureBuildGeometryInfo, &pBuildRangeInfo);

    VkMemoryBarrier memoryBarrier = {0};
    memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memoryBarrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    memoryBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 0, 1, &memoryBarrier, 0, NULL, 0, NULL);

    endSingleTimeCommands(vkrt, commandBuffer);

    VkAccelerationStructureDeviceAddressInfoKHR accelerationStructureDeviceAddressInfo = {0};
    accelerationStructureDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    accelerationStructureDeviceAddressInfo.accelerationStructure = vkrt->core.topLevelAccelerationStructure.structure;
    vkrt->core.topLevelAccelerationStructure.deviceAddress = vkrt->core.procs.vkGetAccelerationStructureDeviceAddressKHR(vkrt->core.device, &accelerationStructureDeviceAddressInfo);

    vkDestroyBuffer(vkrt->core.device, instanceBuffer, NULL);
    vkFreeMemory(vkrt->core.device, instanceMemory, NULL);
    vkDestroyBuffer(vkrt->core.device, scratchBuffer, NULL);
    vkFreeMemory(vkrt->core.device, scratchDeviceMemory, NULL);

    LOG_TRACE("TLAS created. Instances: %u, in %.3f ms", instanceCount, (double)(getMicroseconds() - startTime) / 1e3);
}
