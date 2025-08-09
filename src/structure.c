#include "structure.h"
#include "buffer.h"
#include "command.h"
#include "device.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void createShaderBindingTable(VKRT* vkrt) {
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingPipelineProperties = {0};
    rayTracingPipelineProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;

    VkPhysicalDeviceProperties2 physicalDeviceProperties2 = {0};
    physicalDeviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    physicalDeviceProperties2.pNext = &rayTracingPipelineProperties;

    vkGetPhysicalDeviceProperties2(vkrt->physicalDevice, &physicalDeviceProperties2);

    uint32_t groupCount = 3;
    VkDeviceSize handleSize = rayTracingPipelineProperties.shaderGroupHandleSize;
    VkDeviceSize stride = rayTracingPipelineProperties.shaderGroupBaseAlignment;
    VkDeviceSize sbtSize = groupCount * stride;

    PFN_vkGetRayTracingShaderGroupHandlesKHR pvkGetRayTracingShaderGroupHandlesKHR = (PFN_vkGetRayTracingShaderGroupHandlesKHR)vkGetDeviceProcAddr(vkrt->device, "vkGetRayTracingShaderGroupHandlesKHR");
    PFN_vkGetBufferDeviceAddressKHR pvkGetBufferDeviceAddressKHR = (PFN_vkGetBufferDeviceAddressKHR)vkGetDeviceProcAddr(vkrt->device, "vkGetBufferDeviceAddressKHR");

    VkBuffer stageBuffer;
    VkDeviceMemory stageMemory;

    createBuffer(vkrt, sbtSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stageBuffer, &stageMemory);

    uint8_t* handles = (uint8_t*)malloc(groupCount * handleSize);
    pvkGetRayTracingShaderGroupHandlesKHR(vkrt->device, vkrt->rayTracingPipeline, 0, groupCount, groupCount * handleSize, handles);

    void* mapped;
    vkMapMemory(vkrt->device, stageMemory, 0, sbtSize, 0, &mapped);
    for (uint32_t i = 0; i < groupCount; i++) {
        memcpy((uint8_t*)mapped + i * stride, handles + i * handleSize, handleSize);
    }
    vkUnmapMemory(vkrt->device, stageMemory);
    free(handles);

    VkBufferCreateInfo bufferCreateInfo = {0};
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.size = sbtSize;
    bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(vkrt->device, &bufferCreateInfo, NULL, &vkrt->shaderBindingTableBuffer);

    VkMemoryRequirements memoryRequirements;
    vkGetBufferMemoryRequirements(vkrt->device, vkrt->shaderBindingTableBuffer, &memoryRequirements);

    VkMemoryAllocateFlagsInfo memoryAllocateFlagsInfo = {0};
    memoryAllocateFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    memoryAllocateFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    VkMemoryAllocateInfo memoryAllocateInfo = {0};
    memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memoryAllocateInfo.pNext = &memoryAllocateFlagsInfo;
    memoryAllocateInfo.allocationSize = memoryRequirements.size;
    memoryAllocateInfo.memoryTypeIndex = findMemoryType(vkrt, memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vkAllocateMemory(vkrt->device, &memoryAllocateInfo, NULL, &vkrt->shaderBindingTableMemory);
    vkBindBufferMemory(vkrt->device, vkrt->shaderBindingTableBuffer, vkrt->shaderBindingTableMemory, 0);

    copyBuffer(vkrt, stageBuffer, vkrt->shaderBindingTableBuffer, sbtSize);
    vkDestroyBuffer(vkrt->device, stageBuffer, NULL);
    vkFreeMemory(vkrt->device, stageMemory, NULL);

    VkBufferDeviceAddressInfo bufferDeviceAddressInfo = {0};
    bufferDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    bufferDeviceAddressInfo.buffer = vkrt->shaderBindingTableBuffer;
    VkDeviceAddress base = pvkGetBufferDeviceAddressKHR(vkrt->device, &bufferDeviceAddressInfo);

    for (int i = 0; i < 3; i++) {
        vkrt->shaderBindingTables[i].deviceAddress = base + i * stride;
        vkrt->shaderBindingTables[i].stride = stride;
        vkrt->shaderBindingTables[i].size = stride;
    }

    vkrt->shaderBindingTables[3].deviceAddress = 0;
    vkrt->shaderBindingTables[3].stride = 0;
    vkrt->shaderBindingTables[3].size = 0;
}

void createBottomLevelAccelerationStructure(VKRT* vkrt, Mesh* mesh) {
    VkAccelerationStructureGeometryTrianglesDataKHR accelerationStructureGeometryTrianglesData = {0};
    accelerationStructureGeometryTrianglesData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    accelerationStructureGeometryTrianglesData.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    accelerationStructureGeometryTrianglesData.vertexData.deviceAddress = vkrt->vertexBufferDeviceAddress;
    accelerationStructureGeometryTrianglesData.vertexStride = sizeof(Vertex);
    accelerationStructureGeometryTrianglesData.maxVertex = mesh->firstVertex + mesh->vertexCount - 1;
    accelerationStructureGeometryTrianglesData.indexType = VK_INDEX_TYPE_UINT32;
    accelerationStructureGeometryTrianglesData.indexData.deviceAddress = vkrt->indexBufferDeviceAddress;
    accelerationStructureGeometryTrianglesData.transformData.deviceAddress = 0;

    VkAccelerationStructureGeometryKHR accelerationStructureGeometry = {0};
    accelerationStructureGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    accelerationStructureGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    accelerationStructureGeometry.geometry.triangles = accelerationStructureGeometryTrianglesData;
    accelerationStructureGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

    VkAccelerationStructureBuildGeometryInfoKHR accelerationStructureBuildGeometryInfo = {0};
    accelerationStructureBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    accelerationStructureBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    accelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    accelerationStructureBuildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    accelerationStructureBuildGeometryInfo.geometryCount = 1;
    accelerationStructureBuildGeometryInfo.pGeometries = &accelerationStructureGeometry;
    accelerationStructureBuildGeometryInfo.srcAccelerationStructure = VK_NULL_HANDLE;
    accelerationStructureBuildGeometryInfo.dstAccelerationStructure = VK_NULL_HANDLE;
    accelerationStructureBuildGeometryInfo.scratchData.deviceAddress = 0;

    uint32_t primitiveCount = mesh->indexCount / 3;
    VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo = {0};
    accelerationStructureBuildSizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

    PFN_vkGetAccelerationStructureBuildSizesKHR pvkGetAccelerationStructureBuildSizesKHR = (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetDeviceProcAddr(vkrt->device, "vkGetAccelerationStructureBuildSizesKHR");
    pvkGetAccelerationStructureBuildSizesKHR(vkrt->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &accelerationStructureBuildGeometryInfo, &primitiveCount, &accelerationStructureBuildSizesInfo);

    QueueFamily indices = findQueueFamilies(vkrt);

    VkBufferCreateInfo blasBufferCreateInfo = {0};
    blasBufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    blasBufferCreateInfo.size = accelerationStructureBuildSizesInfo.accelerationStructureSize;
    blasBufferCreateInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    blasBufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    blasBufferCreateInfo.queueFamilyIndexCount = 1;
    blasBufferCreateInfo.pQueueFamilyIndices = (uint32_t*)&indices.graphics;

    if (vkCreateBuffer(vkrt->device, &blasBufferCreateInfo, NULL, &mesh->bottomLevelAccelerationStructure.buffer) != VK_SUCCESS) {
        perror("ERROR: Failed to create BLAS buffer");
        exit(EXIT_FAILURE);
    }

    VkMemoryRequirements memoryRequirements = {0};
    vkGetBufferMemoryRequirements(vkrt->device, mesh->bottomLevelAccelerationStructure.buffer, &memoryRequirements);
    VkPhysicalDeviceMemoryProperties physicalDeviceMemoryProperties = {0};
    vkGetPhysicalDeviceMemoryProperties(vkrt->physicalDevice, &physicalDeviceMemoryProperties);

    uint32_t blasMemoryTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < physicalDeviceMemoryProperties.memoryTypeCount; i++) {
        if ((memoryRequirements.memoryTypeBits & (1 << i)) && (physicalDeviceMemoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            blasMemoryTypeIndex = i;
            break;
        }
    }

    VkMemoryAllocateFlagsInfo memoryAllocateFlagsInfo = {0};
    memoryAllocateFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    memoryAllocateFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    VkMemoryAllocateInfo memoryAllocateInfo = {0};
    memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memoryAllocateInfo.pNext = &memoryAllocateFlagsInfo;
    memoryAllocateInfo.allocationSize = memoryRequirements.size;
    memoryAllocateInfo.memoryTypeIndex = blasMemoryTypeIndex;

    if (vkAllocateMemory(vkrt->device, &memoryAllocateInfo, NULL, &mesh->bottomLevelAccelerationStructure.memory) != VK_SUCCESS) {
        perror("ERROR: Failed to allocate BLAS memory");
        exit(EXIT_FAILURE);
    }

    vkBindBufferMemory(vkrt->device, mesh->bottomLevelAccelerationStructure.buffer, mesh->bottomLevelAccelerationStructure.memory, 0);

    VkAccelerationStructureCreateInfoKHR accelerationStructureCreateInfo = {0};
    accelerationStructureCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    accelerationStructureCreateInfo.buffer = mesh->bottomLevelAccelerationStructure.buffer;
    accelerationStructureCreateInfo.offset = 0;
    accelerationStructureCreateInfo.size = accelerationStructureBuildSizesInfo.accelerationStructureSize;
    accelerationStructureCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

    PFN_vkCreateAccelerationStructureKHR pvkCreateAccelerationStructureKHR = (PFN_vkCreateAccelerationStructureKHR)vkGetDeviceProcAddr(vkrt->device, "vkCreateAccelerationStructureKHR");
    if (pvkCreateAccelerationStructureKHR(vkrt->device, &accelerationStructureCreateInfo, NULL, &mesh->bottomLevelAccelerationStructure.structure) != VK_SUCCESS) {
        perror("ERROR: Failed to create BLAS");
        exit(EXIT_FAILURE);
    }

    VkBufferCreateInfo scratchBufferCreateInfo = {0};
    scratchBufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    scratchBufferCreateInfo.size = accelerationStructureBuildSizesInfo.buildScratchSize;
    scratchBufferCreateInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    scratchBufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    scratchBufferCreateInfo.queueFamilyIndexCount = 1;
    scratchBufferCreateInfo.pQueueFamilyIndices = (uint32_t*)&indices.graphics;

    VkBuffer scratchBuffer;
    if (vkCreateBuffer(vkrt->device, &scratchBufferCreateInfo, NULL, &scratchBuffer) != VK_SUCCESS) {
        perror("ERROR: Failed to create scratch buffer");
        exit(EXIT_FAILURE);
    }

    VkMemoryRequirements scratchBufferMemoryRequirements = {0};
    vkGetBufferMemoryRequirements(vkrt->device, scratchBuffer, &scratchBufferMemoryRequirements);

    uint32_t scratchMemoryTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < physicalDeviceMemoryProperties.memoryTypeCount; i++) {
        if ((scratchBufferMemoryRequirements.memoryTypeBits & (1 << i)) && (physicalDeviceMemoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            scratchMemoryTypeIndex = i;
            break;
        }
    }

    VkMemoryAllocateInfo scratchMemoryAllocateInfo = {0};
    scratchMemoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    scratchMemoryAllocateInfo.pNext = &memoryAllocateFlagsInfo;
    scratchMemoryAllocateInfo.allocationSize = scratchBufferMemoryRequirements.size;
    scratchMemoryAllocateInfo.memoryTypeIndex = scratchMemoryTypeIndex;

    VkDeviceMemory scratchDeviceMemory;
    if (vkAllocateMemory(vkrt->device, &scratchMemoryAllocateInfo, NULL, &scratchDeviceMemory) != VK_SUCCESS) {
        perror("ERROR: Failed to allocate scratch memory");
        exit(EXIT_FAILURE);
    }
    vkBindBufferMemory(vkrt->device, scratchBuffer, scratchDeviceMemory, 0);

    VkBufferDeviceAddressInfo scratchBufferDeviceAddressInfo = {0};
    scratchBufferDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    scratchBufferDeviceAddressInfo.buffer = scratchBuffer;

    PFN_vkGetBufferDeviceAddressKHR pvkGetBufferDeviceAddressKHR = (PFN_vkGetBufferDeviceAddressKHR)vkGetDeviceProcAddr(vkrt->device, "vkGetBufferDeviceAddressKHR");
    VkDeviceAddress scratchBufferDeviceAddress = pvkGetBufferDeviceAddressKHR(vkrt->device, &scratchBufferDeviceAddressInfo);

    accelerationStructureBuildGeometryInfo.dstAccelerationStructure = mesh->bottomLevelAccelerationStructure.structure;
    accelerationStructureBuildGeometryInfo.scratchData.deviceAddress = scratchBufferDeviceAddress;

    VkAccelerationStructureBuildRangeInfoKHR accelerationStructureBuildRangeInfo = {0};
    accelerationStructureBuildRangeInfo.primitiveCount = primitiveCount;
    accelerationStructureBuildRangeInfo.primitiveOffset = (uint32_t)(mesh->firstIndex * sizeof(uint32_t));
    accelerationStructureBuildRangeInfo.firstVertex = mesh->firstVertex;
    accelerationStructureBuildRangeInfo.transformOffset = 0;
    const VkAccelerationStructureBuildRangeInfoKHR* pBuildRangeInfo = &accelerationStructureBuildRangeInfo;

    VkCommandBuffer commandBuffer = beginSingleTimeCommands(vkrt);
    PFN_vkCmdBuildAccelerationStructuresKHR pvkCmdBuildAccelerationStructuresKHR = (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetDeviceProcAddr(vkrt->device, "vkCmdBuildAccelerationStructuresKHR");
    pvkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &accelerationStructureBuildGeometryInfo, &pBuildRangeInfo);

    VkMemoryBarrier memoryBarrier = {0};
    memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memoryBarrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    memoryBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 0, 1, &memoryBarrier, 0, NULL, 0, NULL);

    endSingleTimeCommands(vkrt, commandBuffer);

    VkAccelerationStructureDeviceAddressInfoKHR accelerationStructureDeviceAddressInfo = {0};
    accelerationStructureDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    accelerationStructureDeviceAddressInfo.accelerationStructure = mesh->bottomLevelAccelerationStructure.structure;

    PFN_vkGetAccelerationStructureDeviceAddressKHR pvkGetAccelerationStructureDeviceAddressKHR = (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetDeviceProcAddr(vkrt->device, "vkGetAccelerationStructureDeviceAddressKHR");
    mesh->bottomLevelAccelerationStructure.deviceAddress = pvkGetAccelerationStructureDeviceAddressKHR(vkrt->device, &accelerationStructureDeviceAddressInfo);

    vkDestroyBuffer(vkrt->device, scratchBuffer, NULL);
    vkFreeMemory(vkrt->device, scratchDeviceMemory, NULL);

    vkrt->topLevelAccelerationStructure.needsRebuild = 1;
}

void createTopLevelAccelerationStructure(VKRT* vkrt) {
    if (vkrt->topLevelAccelerationStructure.structure) {    
        PFN_vkDestroyAccelerationStructureKHR pvkDestroyAccelerationStructureKHR = (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(vkrt->device, "vkDestroyAccelerationStructureKHR");
        pvkDestroyAccelerationStructureKHR(vkrt->device, vkrt->topLevelAccelerationStructure.structure, NULL);
        vkDestroyBuffer(vkrt->device, vkrt->topLevelAccelerationStructure.buffer, NULL);
        vkFreeMemory(vkrt->device, vkrt->topLevelAccelerationStructure.memory, NULL);
        
        vkrt->topLevelAccelerationStructure.structure = VK_NULL_HANDLE;
        vkrt->topLevelAccelerationStructure.buffer = VK_NULL_HANDLE;
        vkrt->topLevelAccelerationStructure.memory = VK_NULL_HANDLE;
    }

    if (vkrt->meshInfoBuffer) {
        vkDestroyBuffer(vkrt->device, vkrt->meshInfoBuffer, NULL);
        vkrt->meshInfoBuffer = VK_NULL_HANDLE;
    }

    if (vkrt->meshInfoMemory) {
        vkFreeMemory(vkrt->device, vkrt->meshInfoMemory, NULL);
        vkrt->meshInfoMemory = VK_NULL_HANDLE;
    }

    QueueFamily indices = findQueueFamilies(vkrt);

    uint32_t instanceCount = vkrt->meshCount;
    VkAccelerationStructureInstanceKHR* instances = (VkAccelerationStructureInstanceKHR*)malloc(sizeof(VkAccelerationStructureInstanceKHR) * instanceCount);
    MeshInfo* meshInfos = (MeshInfo*)malloc(sizeof(MeshInfo) * instanceCount);

    for (uint32_t i = 0; i < instanceCount; ++i) {
        VkAccelerationStructureInstanceKHR inst = {0};
        inst.transform.matrix[0][0] = 1.0f;
        inst.transform.matrix[1][1] = 1.0f;
        inst.transform.matrix[2][2] = 1.0f;
        inst.instanceCustomIndex = i;
        inst.mask = 0xFF;
        inst.instanceShaderBindingTableRecordOffset = 0;
        inst.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        inst.accelerationStructureReference = vkrt->meshes[i].bottomLevelAccelerationStructure.deviceAddress;

        instances[i] = inst;

        meshInfos[i].vertexBase = vkrt->meshes[i].firstVertex;
        meshInfos[i].triBase = vkrt->meshes[i].firstIndex / 3u;
    }

    VkBufferCreateInfo instanceBufferCreateInfo = {0};
    instanceBufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    instanceBufferCreateInfo.size = sizeof(VkAccelerationStructureInstanceKHR) * instanceCount;
    instanceBufferCreateInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    instanceBufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    instanceBufferCreateInfo.queueFamilyIndexCount = 1;
    instanceBufferCreateInfo.pQueueFamilyIndices = (uint32_t*)&indices.graphics;

    VkBuffer instanceBuffer;
    if (vkCreateBuffer(vkrt->device, &instanceBufferCreateInfo, NULL, &instanceBuffer) != VK_SUCCESS) {
        perror("ERROR: Failed to create instance buffer");
        exit(EXIT_FAILURE);
    }

    VkMemoryRequirements instanceMemoryRequirements = {0};
    vkGetBufferMemoryRequirements(vkrt->device, instanceBuffer, &instanceMemoryRequirements);
    VkPhysicalDeviceMemoryProperties physicalDeviceMemoryProperties = {0};
    vkGetPhysicalDeviceMemoryProperties(vkrt->physicalDevice, &physicalDeviceMemoryProperties);

    uint32_t instanceMemoryTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < physicalDeviceMemoryProperties.memoryTypeCount; i++) {
        if ((instanceMemoryRequirements.memoryTypeBits & (1u << i)) && (physicalDeviceMemoryProperties.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) == (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            instanceMemoryTypeIndex = i;
            break;
        }
    }

    VkMemoryAllocateFlagsInfo memoryAllocateFlagsInfo = {0};
    memoryAllocateFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    memoryAllocateFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    VkMemoryAllocateInfo instanceMemoryAllocateInfo = {0};
    instanceMemoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    instanceMemoryAllocateInfo.pNext = &memoryAllocateFlagsInfo;
    instanceMemoryAllocateInfo.allocationSize = instanceMemoryRequirements.size;
    instanceMemoryAllocateInfo.memoryTypeIndex = instanceMemoryTypeIndex;

    VkDeviceMemory instanceMemory;
    if (vkAllocateMemory(vkrt->device, &instanceMemoryAllocateInfo, NULL, &instanceMemory) != VK_SUCCESS) {
        perror("ERROR: allocate instance memory");
        exit(EXIT_FAILURE);
    }
    vkBindBufferMemory(vkrt->device, instanceBuffer, instanceMemory, 0);

    void* mapped;
    vkMapMemory(vkrt->device, instanceMemory, 0, instanceMemoryRequirements.size, 0, &mapped);
    memcpy(mapped, instances, sizeof(VkAccelerationStructureInstanceKHR) * instanceCount);
    vkUnmapMemory(vkrt->device, instanceMemory);
    free(instances);

    VkDeviceSize meshInfoSize = sizeof(MeshInfo) * instanceCount;
    VkBuffer stagingMeshInfo;
    VkDeviceMemory stagingMeshInfoMem;
    createBuffer(vkrt, meshInfoSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingMeshInfo, &stagingMeshInfoMem);
    vkMapMemory(vkrt->device, stagingMeshInfoMem, 0, meshInfoSize, 0, &mapped);
    memcpy(mapped, meshInfos, (size_t)meshInfoSize);
    vkUnmapMemory(vkrt->device, stagingMeshInfoMem);
    free(meshInfos);

    createBuffer(vkrt, meshInfoSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &vkrt->meshInfoBuffer, &vkrt->meshInfoMemory);
    copyBuffer(vkrt, stagingMeshInfo, vkrt->meshInfoBuffer, meshInfoSize);
    vkDestroyBuffer(vkrt->device, stagingMeshInfo, NULL);
    vkFreeMemory(vkrt->device, stagingMeshInfoMem, NULL);

    VkBufferDeviceAddressInfo instanceBufferDeviceAddressInfo = {0};
    instanceBufferDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    instanceBufferDeviceAddressInfo.buffer = instanceBuffer;
    PFN_vkGetBufferDeviceAddressKHR pvkGetBufferDeviceAddressKHR = (PFN_vkGetBufferDeviceAddressKHR)vkGetDeviceProcAddr(vkrt->device, "vkGetBufferDeviceAddressKHR");
    VkDeviceAddress instanceDeviceAddress = pvkGetBufferDeviceAddressKHR(vkrt->device, &instanceBufferDeviceAddressInfo);

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
    PFN_vkGetAccelerationStructureBuildSizesKHR pvkGetAccelerationStructureBuildSizesKHR = (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetDeviceProcAddr(vkrt->device, "vkGetAccelerationStructureBuildSizesKHR");
    pvkGetAccelerationStructureBuildSizesKHR(vkrt->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &accelerationStructureBuildGeometryInfo, &instanceCount, &accelerationStructureBuildSizesInfo);

    VkBufferCreateInfo tlasBufferCreateInfo = {0};
    tlasBufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    tlasBufferCreateInfo.size = accelerationStructureBuildSizesInfo.accelerationStructureSize;
    tlasBufferCreateInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    tlasBufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    tlasBufferCreateInfo.queueFamilyIndexCount = 1;
    tlasBufferCreateInfo.pQueueFamilyIndices = (uint32_t*)&indices.graphics;

    if (vkCreateBuffer(vkrt->device, &tlasBufferCreateInfo, NULL, &vkrt->topLevelAccelerationStructure.buffer) != VK_SUCCESS) {
        perror("ERROR: Failed to create TLAS buffer");
        exit(EXIT_FAILURE);
    }
    VkMemoryRequirements bufferMemoryRequirements = {0};
    vkGetBufferMemoryRequirements(vkrt->device, vkrt->topLevelAccelerationStructure.buffer, &bufferMemoryRequirements);

    uint32_t tlasMemoryTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < physicalDeviceMemoryProperties.memoryTypeCount; i++) {
        if ((bufferMemoryRequirements.memoryTypeBits & (1u << i)) && (physicalDeviceMemoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            tlasMemoryTypeIndex = i;
            break;
        }
    }

    VkMemoryAllocateInfo bufferMemoryAllocateInfo = {0};
    bufferMemoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    bufferMemoryAllocateInfo.pNext = &memoryAllocateFlagsInfo;
    bufferMemoryAllocateInfo.allocationSize = bufferMemoryRequirements.size;
    bufferMemoryAllocateInfo.memoryTypeIndex = tlasMemoryTypeIndex;

    if (vkAllocateMemory(vkrt->device, &bufferMemoryAllocateInfo, NULL, &vkrt->topLevelAccelerationStructure.memory) != VK_SUCCESS) {
        perror("ERROR: allocate top level acceleration structure memory");
        exit(EXIT_FAILURE);
    }
    vkBindBufferMemory(vkrt->device, vkrt->topLevelAccelerationStructure.buffer, vkrt->topLevelAccelerationStructure.memory, 0);

    VkAccelerationStructureCreateInfoKHR accelerationStructureCreateInfo = {0};
    accelerationStructureCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    accelerationStructureCreateInfo.buffer = vkrt->topLevelAccelerationStructure.buffer;
    accelerationStructureCreateInfo.offset = 0;
    accelerationStructureCreateInfo.size = accelerationStructureBuildSizesInfo.accelerationStructureSize;
    accelerationStructureCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

    PFN_vkCreateAccelerationStructureKHR pvkCreateAccelerationStructureKHR = (PFN_vkCreateAccelerationStructureKHR)vkGetDeviceProcAddr(vkrt->device, "vkCreateAccelerationStructureKHR");
    if (pvkCreateAccelerationStructureKHR(vkrt->device, &accelerationStructureCreateInfo, NULL, &vkrt->topLevelAccelerationStructure.structure) != VK_SUCCESS) {
        perror("ERROR: Failed to create TLAS");
        exit(EXIT_FAILURE);
    };

    VkBufferCreateInfo scratchBufferCreateInfo = {0};
    scratchBufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    scratchBufferCreateInfo.size = accelerationStructureBuildSizesInfo.buildScratchSize;
    scratchBufferCreateInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    scratchBufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    scratchBufferCreateInfo.queueFamilyIndexCount = 1;
    scratchBufferCreateInfo.pQueueFamilyIndices = (uint32_t*)&indices.graphics;

    VkBuffer scratchBuffer;
    vkCreateBuffer(vkrt->device, &scratchBufferCreateInfo, NULL, &scratchBuffer);
    VkMemoryRequirements scratchBufferMemoryRequirements = {0};
    vkGetBufferMemoryRequirements(vkrt->device, scratchBuffer, &scratchBufferMemoryRequirements);

    uint32_t scratchMemoryType = UINT32_MAX;
    for (uint32_t i = 0; i < physicalDeviceMemoryProperties.memoryTypeCount; i++) {
        if ((scratchBufferMemoryRequirements.memoryTypeBits & (1u << i)) && (physicalDeviceMemoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            scratchMemoryType = i;
            break;
        }
    }

    VkMemoryAllocateInfo scratchMemoryAllocateInfo = {0};
    scratchMemoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    scratchMemoryAllocateInfo.pNext = &memoryAllocateFlagsInfo;
    scratchMemoryAllocateInfo.allocationSize = scratchBufferMemoryRequirements.size;
    scratchMemoryAllocateInfo.memoryTypeIndex = scratchMemoryType;

    VkDeviceMemory scratchDeviceMemory;
    vkAllocateMemory(vkrt->device, &scratchMemoryAllocateInfo, NULL, &scratchDeviceMemory);
    vkBindBufferMemory(vkrt->device, scratchBuffer, scratchDeviceMemory, 0);

    VkBufferDeviceAddressInfo scratchBufferDeviceAddressInfo = {0};
    scratchBufferDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    scratchBufferDeviceAddressInfo.buffer = scratchBuffer;

    VkDeviceAddress scratchBufferDeviceAddress = pvkGetBufferDeviceAddressKHR(vkrt->device, &scratchBufferDeviceAddressInfo);
    accelerationStructureBuildGeometryInfo.dstAccelerationStructure = vkrt->topLevelAccelerationStructure.structure;
    accelerationStructureBuildGeometryInfo.scratchData.deviceAddress = scratchBufferDeviceAddress;

    VkAccelerationStructureBuildRangeInfoKHR accelerationStructureBuildRangeInfo = {0};
    accelerationStructureBuildRangeInfo.primitiveCount = instanceCount;
    accelerationStructureBuildRangeInfo.primitiveOffset = 0;
    accelerationStructureBuildRangeInfo.firstVertex = 0;
    accelerationStructureBuildRangeInfo.transformOffset = 0;
    const VkAccelerationStructureBuildRangeInfoKHR* pBuildRangeInfo = &accelerationStructureBuildRangeInfo;

    VkCommandBuffer commandBuffer = beginSingleTimeCommands(vkrt);
    PFN_vkCmdBuildAccelerationStructuresKHR pvkCmdBuildAccelerationStructuresKHR = (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetDeviceProcAddr(vkrt->device, "vkCmdBuildAccelerationStructuresKHR");
    pvkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &accelerationStructureBuildGeometryInfo, &pBuildRangeInfo);

    VkMemoryBarrier memoryBarrier = {0};
    memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memoryBarrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    memoryBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 0, 1, &memoryBarrier, 0, NULL, 0, NULL);

    endSingleTimeCommands(vkrt, commandBuffer);

    VkAccelerationStructureDeviceAddressInfoKHR accelerationStructureDeviceAddressInfo = {0};
    accelerationStructureDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    accelerationStructureDeviceAddressInfo.accelerationStructure = vkrt->topLevelAccelerationStructure.structure;
    PFN_vkGetAccelerationStructureDeviceAddressKHR fnGetASAddr = (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetDeviceProcAddr(vkrt->device, "vkGetAccelerationStructureDeviceAddressKHR");
    vkrt->topLevelAccelerationStructure.deviceAddress = fnGetASAddr(vkrt->device, &accelerationStructureDeviceAddressInfo);

    vkDestroyBuffer(vkrt->device, instanceBuffer, NULL);
    vkFreeMemory(vkrt->device, instanceMemory, NULL);
    vkDestroyBuffer(vkrt->device, scratchBuffer, NULL);
    vkFreeMemory(vkrt->device, scratchDeviceMemory, NULL);
}
