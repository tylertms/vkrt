#include "accel.h"
#include "buffer.h"
#include "constants.h"
#include "device.h"
#include "geometry.h"
#include "scene.h"
#include "state.h"
#include "types.h"
#include "vkrt_engine_types.h"
#include "vkrt_internal.h"
#include "vkrt_types.h"
#include "vulkan/vulkan_core.h"

#include <stdint.h>
#include <stdlib.h>

typedef struct TLASBuildResources {
    AccelerationStructure* accelerationStructure;
    FrameTransfer* instanceBuffer;
    FrameTransfer* scratchBuffer;
    VkBool32* buildPending;
} TLASBuildResources;

typedef struct PreparedTLASState {
    Buffer meshData;
    AccelerationStructure sceneTLAS;
    AccelerationStructure selectionTLAS;
    FrameTransfer sceneTLASInstanceBuffer;
    FrameTransfer sceneTLASScratch;
    FrameTransfer selectionTLASInstanceBuffer;
    FrameTransfer selectionTLASScratch;
    uint32_t sceneTLASInstanceCount;
    uint32_t selectionTLASInstanceCount;
    VkBool32 sceneTLASBuildPending;
    VkBool32 selectionTLASBuildPending;
} PreparedTLASState;

static TLASBuildResources querySceneTLASBuildResources(PreparedTLASState* state) {
    return (TLASBuildResources){
        .accelerationStructure = &state->sceneTLAS,
        .instanceBuffer = &state->sceneTLASInstanceBuffer,
        .scratchBuffer = &state->sceneTLASScratch,
        .buildPending = &state->sceneTLASBuildPending,
    };
}

static TLASBuildResources querySelectionTLASBuildResources(PreparedTLASState* state) {
    return (TLASBuildResources){
        .accelerationStructure = &state->selectionTLAS,
        .instanceBuffer = &state->selectionTLASInstanceBuffer,
        .scratchBuffer = &state->selectionTLASScratch,
        .buildPending = &state->selectionTLASBuildPending,
    };
}

static void resetTLASBuildResources(VKRT* vkrt, const TLASBuildResources* resources) {
    if (!vkrt || !resources) return;
    destroyTransfer(vkrt, resources->instanceBuffer);
    destroyTransfer(vkrt, resources->scratchBuffer);
    vkrtDestroyAccelerationStructureResources(vkrt, resources->accelerationStructure);
    if (resources->buildPending) {
        *resources->buildPending = VK_FALSE;
    }
}

static VKRT_Result createTLASAccelerationStructure(
    VKRT* vkrt,
    TLASBuildResources* resources,
    const VkAccelerationStructureBuildSizesInfoKHR* buildSizesInfo
) {
    if (!vkrt || !resources || !buildSizesInfo) return VKRT_ERROR_INVALID_ARGUMENT;

    uint32_t graphicsFamily = vkrt->core.indices.graphics;
    VkBufferCreateInfo tlasBufferCreateInfo = {0};
    tlasBufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    tlasBufferCreateInfo.size = buildSizesInfo->accelerationStructureSize;
    tlasBufferCreateInfo.usage =
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    tlasBufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    tlasBufferCreateInfo.queueFamilyIndexCount = 1;
    tlasBufferCreateInfo.pQueueFamilyIndices = &graphicsFamily;

    if (vkCreateBuffer(vkrt->core.device, &tlasBufferCreateInfo, NULL, &resources->accelerationStructure->buffer) !=
        VK_SUCCESS) {
        return VKRT_ERROR_OPERATION_FAILED;
    }

    VkMemoryRequirements tlasMemoryRequirements = {0};
    vkGetBufferMemoryRequirements(vkrt->core.device, resources->accelerationStructure->buffer, &tlasMemoryRequirements);

    VkMemoryAllocateFlagsInfo allocateFlags = {0};
    allocateFlags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    allocateFlags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    VkMemoryAllocateInfo tlasAllocateInfo = {0};
    tlasAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    tlasAllocateInfo.pNext = &allocateFlags;
    tlasAllocateInfo.allocationSize = tlasMemoryRequirements.size;
    if (findMemoryType(
            vkrt,
            tlasMemoryRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &tlasAllocateInfo.memoryTypeIndex
        ) != VKRT_SUCCESS) {
        return VKRT_ERROR_OPERATION_FAILED;
    }

    if (vkAllocateMemory(vkrt->core.device, &tlasAllocateInfo, NULL, &resources->accelerationStructure->memory) !=
        VK_SUCCESS) {
        return VKRT_ERROR_OPERATION_FAILED;
    }

    if (vkBindBufferMemory(
            vkrt->core.device,
            resources->accelerationStructure->buffer,
            resources->accelerationStructure->memory,
            0
        ) != VK_SUCCESS) {
        return VKRT_ERROR_OPERATION_FAILED;
    }

    VkAccelerationStructureCreateInfoKHR createInfo = {0};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = resources->accelerationStructure->buffer;
    createInfo.size = buildSizesInfo->accelerationStructureSize;
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

    if (vkrt->core.procs.vkCreateAccelerationStructureKHR(
            vkrt->core.device,
            &createInfo,
            NULL,
            &resources->accelerationStructure->structure
        ) != VK_SUCCESS) {
        return VKRT_ERROR_OPERATION_FAILED;
    }

    VkAccelerationStructureDeviceAddressInfoKHR addressInfo = {0};
    addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addressInfo.accelerationStructure = resources->accelerationStructure->structure;
    resources->accelerationStructure->deviceAddress =
        vkrt->core.procs.vkGetAccelerationStructureDeviceAddressKHR(vkrt->core.device, &addressInfo);
    return VKRT_SUCCESS;
}

static VKRT_Result prepareTLASBuild(
    VKRT* vkrt,
    const TLASBuildResources* resources,
    const VkAccelerationStructureInstanceKHR* instances,
    uint32_t instanceCount
) {
    if (!vkrt || !resources) return VKRT_ERROR_INVALID_ARGUMENT;

    resetTLASBuildResources(vkrt, resources);

    VkDeviceAddress instanceDeviceAddress = 0;
    VkAccelerationStructureInstanceKHR emptyInstance = {0};
    const VkAccelerationStructureInstanceKHR* instanceData = instances ? instances : &emptyInstance;
    uint32_t sizeQueryCount = instanceCount > 0u ? instanceCount : 1u;

    VKRT_Result result = createDeviceBufferFromData(
        vkrt,
        instanceData,
        sizeof(*instanceData) * sizeQueryCount,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        &resources->instanceBuffer->buffer,
        &resources->instanceBuffer->memory,
        &instanceDeviceAddress
    );
    if (result != VKRT_SUCCESS) {
        return result;
    }

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
        &sizeQueryCount,
        &buildSizesInfo
    );

    result = createTLASAccelerationStructure(vkrt, (TLASBuildResources*)resources, &buildSizesInfo);
    if (result != VKRT_SUCCESS) {
        resetTLASBuildResources(vkrt, resources);
        return result;
    }

    result = createBuffer(
        vkrt,
        buildSizesInfo.buildScratchSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        &resources->scratchBuffer->buffer,
        &resources->scratchBuffer->memory
    );
    if (result != VKRT_SUCCESS) {
        resetTLASBuildResources(vkrt, resources);
        return result;
    }

    *resources->buildPending = VK_TRUE;
    return VKRT_SUCCESS;
}

static void destroyPreparedTLASState(VKRT* vkrt, PreparedTLASState* state) {
    if (!vkrt || !state) return;

    TLASBuildResources sceneResources = querySceneTLASBuildResources(state);
    TLASBuildResources selectionResources = querySelectionTLASBuildResources(state);
    destroyBufferResources(vkrt, &state->meshData);
    resetTLASBuildResources(vkrt, &sceneResources);
    resetTLASBuildResources(vkrt, &selectionResources);
}

static VKRT_Result recordTLASBuild(
    VKRT* vkrt,
    VkCommandBuffer commandBuffer,
    const TLASBuildResources* resources,
    uint32_t instanceCount
) {
    if (!vkrt || commandBuffer == VK_NULL_HANDLE || !resources || !resources->buildPending) {
        return VKRT_ERROR_INVALID_ARGUMENT;
    }
    if (!*resources->buildPending) return VKRT_SUCCESS;
    if (resources->accelerationStructure->structure == VK_NULL_HANDLE ||
        resources->instanceBuffer->buffer == VK_NULL_HANDLE || resources->scratchBuffer->buffer == VK_NULL_HANDLE) {
        return VKRT_SUCCESS;
    }

    VkBufferDeviceAddressInfo instanceAddressInfo = {0};
    instanceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    instanceAddressInfo.buffer = resources->instanceBuffer->buffer;
    VkDeviceAddress instanceDeviceAddress =
        vkrt->core.procs.vkGetBufferDeviceAddressKHR(vkrt->core.device, &instanceAddressInfo);

    VkBufferDeviceAddressInfo scratchAddressInfo = {0};
    scratchAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    scratchAddressInfo.buffer = resources->scratchBuffer->buffer;

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
    buildInfo.dstAccelerationStructure = resources->accelerationStructure->structure;
    buildInfo.scratchData.deviceAddress =
        vkrt->core.procs.vkGetBufferDeviceAddressKHR(vkrt->core.device, &scratchAddressInfo);

    VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo = {0};
    buildRangeInfo.primitiveCount = instanceCount;
    const VkAccelerationStructureBuildRangeInfoKHR* buildRangeInfos[] = {&buildRangeInfo};

    vkrt->core.procs.vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &buildInfo, buildRangeInfos);
    return VKRT_SUCCESS;
}

static VkBool32 materialMayRejectRayHit(const Material* material, float meshOpacity) {
    if (!material) return VK_TRUE;
    if (material->alphaMode != VKRT_MATERIAL_ALPHA_MODE_OPAQUE) return VK_TRUE;
    if (material->opacity < 0.999f || meshOpacity < 0.999f) return VK_TRUE;
    return VK_FALSE;
}

static VkGeometryInstanceFlagsKHR queryTLASInstanceFlags(const VKRT* vkrt, const Mesh* mesh) {
    VkGeometryInstanceFlagsKHR flags = 0;
    if (!vkrt || !mesh) return flags;

    if (mesh->info.renderBackfaces) {
        flags |= VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    }

    const Material* material = vkrtGetSceneMaterialData(vkrt, mesh->info.materialIndex);
    if (materialMayRejectRayHit(material, mesh->info.opacity)) {
        flags |= VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR;
    } else {
        flags |= VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
    }

    return flags;
}

static VkBool32 buildTLASInstanceForMesh(
    const VKRT* vkrt,
    uint32_t meshIndex,
    VkAccelerationStructureInstanceKHR* outInstance
) {
    if (!vkrt || !outInstance || meshIndex >= vkrt->core.meshCount) {
        return VK_FALSE;
    }

    const Mesh* mesh = &vkrt->core.meshes[meshIndex];
    if (mesh->bottomLevelAccelerationStructure.deviceAddress == 0) {
        return VK_FALSE;
    }

    *outInstance = (VkAccelerationStructureInstanceKHR){0};
    outInstance->transform = getMeshWorldTransform(mesh);
    outInstance->instanceCustomIndex = meshIndex;
    outInstance->mask = 0xFF;
    outInstance->flags = queryTLASInstanceFlags(vkrt, mesh);
    outInstance->accelerationStructureReference = mesh->bottomLevelAccelerationStructure.deviceAddress;
    return VK_TRUE;
}

static VkBool32 buildSelectionTLASInstance(const VKRT* vkrt, VkAccelerationStructureInstanceKHR* outInstance) {
    if (!vkrt || !vkrt->sceneSettings.selectionEnabled) {
        return VK_FALSE;
    }

    return buildTLASInstanceForMesh(vkrt, vkrt->sceneSettings.selectedMeshIndex, outInstance);
}

static VKRT_Result allocateSceneTLASInstances(
    const VKRT* vkrt,
    VkAccelerationStructureInstanceKHR** outInstances,
    uint32_t* outInstanceCount
) {
    if (outInstances) *outInstances = NULL;
    if (outInstanceCount) *outInstanceCount = 0u;
    if (!vkrt || !outInstances || !outInstanceCount) return VKRT_ERROR_INVALID_ARGUMENT;

    uint32_t meshCount = vkrt->core.meshCount;
    if (meshCount == 0u) return VKRT_SUCCESS;

    VkAccelerationStructureInstanceKHR* instances =
        (VkAccelerationStructureInstanceKHR*)malloc(sizeof(*instances) * meshCount);
    if (!instances) return VKRT_ERROR_OPERATION_FAILED;

    for (uint32_t meshIndex = 0; meshIndex < meshCount; meshIndex++) {
        if (!buildTLASInstanceForMesh(vkrt, meshIndex, &instances[meshIndex])) {
            free(instances);
            return VKRT_ERROR_OPERATION_FAILED;
        }
    }

    *outInstances = instances;
    *outInstanceCount = meshCount;
    return VKRT_SUCCESS;
}

static VKRT_Result prepareSceneTLASState(
    VKRT* vkrt,
    PreparedTLASState* state,
    const VkAccelerationStructureInstanceKHR* sceneInstances,
    uint32_t sceneInstanceCount
) {
    if (!vkrt || !state) return VKRT_ERROR_INVALID_ARGUMENT;

    VKRT_Result result = vkrtSceneBuildMeshInfoBuffer(vkrt, &state->meshData);
    if (result != VKRT_SUCCESS) return result;

    TLASBuildResources sceneResources = querySceneTLASBuildResources(state);
    result = prepareTLASBuild(vkrt, &sceneResources, sceneInstances, sceneInstanceCount);
    if (result != VKRT_SUCCESS) return result;

    state->sceneTLASInstanceCount = sceneInstanceCount;
    return VKRT_SUCCESS;
}

static VKRT_Result prepareSelectionTLASState(VKRT* vkrt, PreparedTLASState* state) {
    if (!vkrt || !state) return VKRT_ERROR_INVALID_ARGUMENT;

    VkAccelerationStructureInstanceKHR selectionInstance = {0};
    if (!buildSelectionTLASInstance(vkrt, &selectionInstance)) return VKRT_SUCCESS;

    TLASBuildResources selectionResources = querySelectionTLASBuildResources(state);
    VKRT_Result result = prepareTLASBuild(vkrt, &selectionResources, &selectionInstance, 1u);
    if (result != VKRT_SUCCESS) return result;

    state->selectionTLASInstanceCount = 1u;
    return VKRT_SUCCESS;
}

static void commitPreparedTLASState(VKRT* vkrt, PreparedTLASState* state) {
    if (!vkrt || !state) return;

    FrameSceneUpdate* update = vkrtCurrentFrameSceneUpdate(vkrt);
    Buffer previousMeshData = vkrt->core.sceneMeshData;
    AccelerationStructure previousSceneTLAS = vkrt->core.sceneTopLevelAccelerationStructure;
    AccelerationStructure previousSelectionTLAS = vkrt->core.selectionTopLevelAccelerationStructure;
    FrameTransfer previousSceneTLASInstanceBuffer = update->sceneTLASInstanceBuffer;
    FrameTransfer previousSceneTLASScratch = update->sceneTLASScratch;
    FrameTransfer previousSelectionTLASInstanceBuffer = update->selectionTLASInstanceBuffer;
    FrameTransfer previousSelectionTLASScratch = update->selectionTLASScratch;

    vkrt->core.sceneMeshData = state->meshData;
    vkrt->core.sceneTopLevelAccelerationStructure = state->sceneTLAS;
    vkrt->core.selectionTopLevelAccelerationStructure = state->selectionTLAS;
    update->sceneTLASInstanceBuffer = state->sceneTLASInstanceBuffer;
    update->sceneTLASScratch = state->sceneTLASScratch;
    update->selectionTLASInstanceBuffer = state->selectionTLASInstanceBuffer;
    update->selectionTLASScratch = state->selectionTLASScratch;
    update->sceneTLASInstanceCount = state->sceneTLASInstanceCount;
    update->selectionTLASInstanceCount = state->selectionTLASInstanceCount;
    update->sceneTLASBuildPending = state->sceneTLASBuildPending;
    update->selectionTLASBuildPending = state->selectionTLASBuildPending;

    state->meshData = (Buffer){0};
    state->sceneTLAS = (AccelerationStructure){0};
    state->selectionTLAS = (AccelerationStructure){0};
    state->sceneTLASInstanceBuffer = (FrameTransfer){0};
    state->sceneTLASScratch = (FrameTransfer){0};
    state->selectionTLASInstanceBuffer = (FrameTransfer){0};
    state->selectionTLASScratch = (FrameTransfer){0};
    state->sceneTLASInstanceCount = 0u;
    state->selectionTLASInstanceCount = 0u;
    state->sceneTLASBuildPending = VK_FALSE;
    state->selectionTLASBuildPending = VK_FALSE;

    destroyBufferResources(vkrt, &previousMeshData);
    destroyTransfer(vkrt, &previousSceneTLASInstanceBuffer);
    destroyTransfer(vkrt, &previousSceneTLASScratch);
    destroyTransfer(vkrt, &previousSelectionTLASInstanceBuffer);
    destroyTransfer(vkrt, &previousSelectionTLASScratch);
    vkrtDestroyAccelerationStructureResources(vkrt, &previousSceneTLAS);
    vkrtDestroyAccelerationStructureResources(vkrt, &previousSelectionTLAS);
}

VKRT_Result createSelectionTopLevelAccelerationStructure(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    FrameSceneUpdate* update = vkrtCurrentFrameSceneUpdate(vkrt);
    AccelerationStructure previousSelectionTLAS = vkrt->core.selectionTopLevelAccelerationStructure;
    FrameTransfer previousInstanceBuffer = update->selectionTLASInstanceBuffer;
    FrameTransfer previousScratchBuffer = update->selectionTLASScratch;

    AccelerationStructure nextSelectionTLAS = {0};
    FrameTransfer nextInstanceBuffer = {0};
    FrameTransfer nextScratchBuffer = {0};
    VkBool32 nextBuildPending = VK_FALSE;
    uint32_t nextInstanceCount = 0u;
    TLASBuildResources nextSelectionResources = {
        .accelerationStructure = &nextSelectionTLAS,
        .instanceBuffer = &nextInstanceBuffer,
        .scratchBuffer = &nextScratchBuffer,
        .buildPending = &nextBuildPending,
    };

    VkAccelerationStructureInstanceKHR selectionInstance = {0};
    if (buildSelectionTLASInstance(vkrt, &selectionInstance)) {
        VKRT_Result result = prepareTLASBuild(vkrt, &nextSelectionResources, &selectionInstance, 1u);
        if (result != VKRT_SUCCESS) {
            resetTLASBuildResources(vkrt, &nextSelectionResources);
            return result;
        }
        nextInstanceCount = 1u;
    }

    vkrt->core.selectionTopLevelAccelerationStructure = nextSelectionTLAS;
    update->selectionTLASInstanceBuffer = nextInstanceBuffer;
    update->selectionTLASScratch = nextScratchBuffer;
    update->selectionTLASInstanceCount = nextInstanceCount;
    update->selectionTLASBuildPending = nextBuildPending;

    destroyTransfer(vkrt, &previousInstanceBuffer);
    destroyTransfer(vkrt, &previousScratchBuffer);
    vkrtDestroyAccelerationStructureResources(vkrt, &previousSelectionTLAS);
    return VKRT_SUCCESS;
}

VKRT_Result createTopLevelAccelerationStructures(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    PreparedTLASState state = {0};
    VkAccelerationStructureInstanceKHR* sceneInstances = NULL;
    uint32_t sceneInstanceCount = 0u;

    VKRT_Result result = allocateSceneTLASInstances(vkrt, &sceneInstances, &sceneInstanceCount);
    if (result != VKRT_SUCCESS) {
        return result;
    }

    result = prepareSceneTLASState(vkrt, &state, sceneInstances, sceneInstanceCount);
    if (result == VKRT_SUCCESS) {
        result = prepareSelectionTLASState(vkrt, &state);
    }

    free(sceneInstances);
    if (result != VKRT_SUCCESS) {
        destroyPreparedTLASState(vkrt, &state);
        return result;
    }

    commitPreparedTLASState(vkrt, &state);
    return VKRT_SUCCESS;
}

VKRT_Result recordTopLevelAccelerationStructureBuilds(VKRT* vkrt, VkCommandBuffer commandBuffer) {
    if (!vkrt || commandBuffer == VK_NULL_HANDLE) return VKRT_ERROR_INVALID_ARGUMENT;

    FrameSceneUpdate* update = vkrtCurrentFrameSceneUpdate(vkrt);
    TLASBuildResources sceneTLASResources = {
        .accelerationStructure = &vkrt->core.sceneTopLevelAccelerationStructure,
        .instanceBuffer = &update->sceneTLASInstanceBuffer,
        .scratchBuffer = &update->sceneTLASScratch,
        .buildPending = &update->sceneTLASBuildPending,
    };
    TLASBuildResources selectionTLASResources = {
        .accelerationStructure = &vkrt->core.selectionTopLevelAccelerationStructure,
        .instanceBuffer = &update->selectionTLASInstanceBuffer,
        .scratchBuffer = &update->selectionTLASScratch,
        .buildPending = &update->selectionTLASBuildPending,
    };

    VKRT_Result result = recordTLASBuild(vkrt, commandBuffer, &sceneTLASResources, update->sceneTLASInstanceCount);
    if (result != VKRT_SUCCESS) return result;

    result = recordTLASBuild(vkrt, commandBuffer, &selectionTLASResources, update->selectionTLASInstanceCount);
    if (result != VKRT_SUCCESS) return result;

    return VKRT_SUCCESS;
}
