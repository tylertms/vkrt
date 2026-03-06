#include "descriptor.h"
#include "debug.h"

#include "vkrt_internal.h"

#include <vulkan/vulkan_core.h>

static AccelerationStructure* getSceneTLAS(VKRT* vkrt) {
    return &vkrt->core.sceneTopLevelAccelerationStructure;
}

static Buffer* getSceneMeshData(VKRT* vkrt) {
    return &vkrt->core.sceneMeshData;
}

static Buffer* getSceneMaterialData(VKRT* vkrt) {
    return &vkrt->core.sceneMaterialData;
}

static Buffer* getSceneEmissiveMeshData(VKRT* vkrt) {
    return &vkrt->core.sceneEmissiveMeshData;
}

static Buffer* getSceneEmissiveTriangleData(VKRT* vkrt) {
    return &vkrt->core.sceneEmissiveTriangleData;
}

static VkBuffer getFrameSceneDataBuffer(VKRT* vkrt, uint32_t frameIndex) {
    return vkrt->core.sceneDataBuffers[frameIndex];
}

static VkBool32 descriptorResourcesReadyForFrame(VKRT* vkrt, uint32_t frameIndex) {
    if (!vkrt || frameIndex >= VKRT_MAX_FRAMES_IN_FLIGHT) return VK_FALSE;
    if (vkrt->core.accumulationReadIndex >= 2u || vkrt->core.accumulationWriteIndex >= 2u) {
        return VK_FALSE;
    }

    const AccelerationStructure* tlas = getSceneTLAS(vkrt);
    const Buffer* meshData = getSceneMeshData(vkrt);
    const Buffer* materialData = getSceneMaterialData(vkrt);
    const Buffer* emissiveMeshData = getSceneEmissiveMeshData(vkrt);
    const Buffer* emissiveTriangleData = getSceneEmissiveTriangleData(vkrt);

    return tlas->structure != VK_NULL_HANDLE &&
           getFrameSceneDataBuffer(vkrt, frameIndex) != VK_NULL_HANDLE &&
           vkrt->core.storageImageView != VK_NULL_HANDLE &&
           vkrt->core.accumulationImageViews[vkrt->core.accumulationReadIndex] != VK_NULL_HANDLE &&
           vkrt->core.accumulationImageViews[vkrt->core.accumulationWriteIndex] != VK_NULL_HANDLE &&
           vkrt->core.vertexData.buffer != VK_NULL_HANDLE &&
           vkrt->core.indexData.buffer != VK_NULL_HANDLE &&
           vkrt->core.pickBuffer.buffer != VK_NULL_HANDLE &&
           meshData->buffer != VK_NULL_HANDLE &&
           materialData->buffer != VK_NULL_HANDLE &&
           emissiveMeshData->buffer != VK_NULL_HANDLE &&
           emissiveTriangleData->buffer != VK_NULL_HANDLE;
}

static VKRT_Result updateDescriptorSetForFrame(VKRT* vkrt, uint32_t frameIndex) {
    if (!vkrt || frameIndex >= VKRT_MAX_FRAMES_IN_FLIGHT) return VKRT_ERROR_INVALID_ARGUMENT;
    if (!descriptorResourcesReadyForFrame(vkrt, frameIndex)) {
        vkrt->core.descriptorSetReady[frameIndex] = VK_FALSE;
        return VKRT_SUCCESS;
    }

    Buffer* meshData = getSceneMeshData(vkrt);
    Buffer* materialData = getSceneMaterialData(vkrt);
    Buffer* emissiveMeshData = getSceneEmissiveMeshData(vkrt);
    Buffer* emissiveTriangleData = getSceneEmissiveTriangleData(vkrt);
    AccelerationStructure* tlas = getSceneTLAS(vkrt);
    VkDescriptorSet descriptorSet = vkrt->core.descriptorSets[frameIndex];

    VkWriteDescriptorSetAccelerationStructureKHR accelerationStructureInfo = {0};
    accelerationStructureInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    accelerationStructureInfo.accelerationStructureCount = 1;
    accelerationStructureInfo.pAccelerationStructures = &tlas->structure;

    VkWriteDescriptorSet accelerationStructureWrite = {0};
    accelerationStructureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    accelerationStructureWrite.pNext = &accelerationStructureInfo;
    accelerationStructureWrite.dstSet = descriptorSet;
    accelerationStructureWrite.dstBinding = 0;
    accelerationStructureWrite.descriptorCount = 1;
    accelerationStructureWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

    VkDescriptorImageInfo storageImageInfo = {0};
    storageImageInfo.imageView = vkrt->core.accumulationImageViews[vkrt->core.accumulationReadIndex];
    storageImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet storageImageWrite = {0};
    storageImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    storageImageWrite.dstSet = descriptorSet;
    storageImageWrite.dstBinding = 1;
    storageImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    storageImageWrite.descriptorCount = 1;
    storageImageWrite.pImageInfo = &storageImageInfo;

    VkDescriptorImageInfo accumulationWriteInfo = {0};
    accumulationWriteInfo.imageView = vkrt->core.accumulationImageViews[vkrt->core.accumulationWriteIndex];
    accumulationWriteInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet accumulationWrite = {0};
    accumulationWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    accumulationWrite.dstSet = descriptorSet;
    accumulationWrite.dstBinding = 2;
    accumulationWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    accumulationWrite.descriptorCount = 1;
    accumulationWrite.pImageInfo = &accumulationWriteInfo;

    VkDescriptorImageInfo outputImageInfo = {0};
    outputImageInfo.imageView = vkrt->core.storageImageView;
    outputImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet outputImageWrite = {0};
    outputImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    outputImageWrite.dstSet = descriptorSet;
    outputImageWrite.dstBinding = 3;
    outputImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    outputImageWrite.descriptorCount = 1;
    outputImageWrite.pImageInfo = &outputImageInfo;

    VkDescriptorBufferInfo vertexBufferInfo = {
        .buffer = vkrt->core.vertexData.buffer,
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    };

    VkWriteDescriptorSet vertexBufferWrite = {0};
    vertexBufferWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    vertexBufferWrite.dstSet = descriptorSet;
    vertexBufferWrite.dstBinding = 4;
    vertexBufferWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    vertexBufferWrite.descriptorCount = 1;
    vertexBufferWrite.pBufferInfo = &vertexBufferInfo;

    VkDescriptorBufferInfo indexBufferInfo = {
        .buffer = vkrt->core.indexData.buffer,
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    };

    VkWriteDescriptorSet indexBufferWrite = {0};
    indexBufferWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    indexBufferWrite.dstSet = descriptorSet;
    indexBufferWrite.dstBinding = 5;
    indexBufferWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    indexBufferWrite.descriptorCount = 1;
    indexBufferWrite.pBufferInfo = &indexBufferInfo;

    VkDescriptorBufferInfo sceneDataInfo = {
        .buffer = getFrameSceneDataBuffer(vkrt, frameIndex),
        .offset = 0,
        .range = sizeof(SceneData),
    };

    VkWriteDescriptorSet sceneDataWrite = {0};
    sceneDataWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    sceneDataWrite.dstSet = descriptorSet;
    sceneDataWrite.dstBinding = 6;
    sceneDataWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    sceneDataWrite.descriptorCount = 1;
    sceneDataWrite.pBufferInfo = &sceneDataInfo;

    VkDescriptorBufferInfo pickBufferInfo = {
        .buffer = vkrt->core.pickBuffer.buffer,
        .offset = 0,
        .range = sizeof(PickBuffer),
    };

    VkWriteDescriptorSet pickBufferWrite = {0};
    pickBufferWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    pickBufferWrite.dstSet = descriptorSet;
    pickBufferWrite.dstBinding = 7;
    pickBufferWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    pickBufferWrite.descriptorCount = 1;
    pickBufferWrite.pBufferInfo = &pickBufferInfo;

    VkDescriptorBufferInfo meshInfo = {
        .buffer = meshData->buffer,
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    };

    VkWriteDescriptorSet meshInfoWrite = {0};
    meshInfoWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    meshInfoWrite.dstSet = descriptorSet;
    meshInfoWrite.dstBinding = 8;
    meshInfoWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    meshInfoWrite.descriptorCount = 1;
    meshInfoWrite.pBufferInfo = &meshInfo;

    VkDescriptorBufferInfo materialInfo = {
        .buffer = materialData->buffer,
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    };

    VkWriteDescriptorSet materialWrite = {0};
    materialWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    materialWrite.dstSet = descriptorSet;
    materialWrite.dstBinding = 9;
    materialWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    materialWrite.descriptorCount = 1;
    materialWrite.pBufferInfo = &materialInfo;

    VkDescriptorBufferInfo emissiveMeshInfo = {
        .buffer = emissiveMeshData->buffer,
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    };

    VkWriteDescriptorSet emissiveMeshWrite = {0};
    emissiveMeshWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    emissiveMeshWrite.dstSet = descriptorSet;
    emissiveMeshWrite.dstBinding = 10;
    emissiveMeshWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    emissiveMeshWrite.descriptorCount = 1;
    emissiveMeshWrite.pBufferInfo = &emissiveMeshInfo;

    VkDescriptorBufferInfo emissiveTriangleInfo = {
        .buffer = emissiveTriangleData->buffer,
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    };

    VkWriteDescriptorSet emissiveTriangleWrite = {0};
    emissiveTriangleWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    emissiveTriangleWrite.dstSet = descriptorSet;
    emissiveTriangleWrite.dstBinding = 11;
    emissiveTriangleWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    emissiveTriangleWrite.descriptorCount = 1;
    emissiveTriangleWrite.pBufferInfo = &emissiveTriangleInfo;

    VkWriteDescriptorSet writeDescriptorSets[] = {
        accelerationStructureWrite,
        storageImageWrite,
        accumulationWrite,
        outputImageWrite,
        vertexBufferWrite,
        indexBufferWrite,
        sceneDataWrite,
        pickBufferWrite,
        meshInfoWrite,
        materialWrite,
        emissiveMeshWrite,
        emissiveTriangleWrite,
    };

    vkUpdateDescriptorSets(vkrt->core.device, VKRT_ARRAY_COUNT(writeDescriptorSets), writeDescriptorSets, 0, VK_NULL_HANDLE);
    vkrt->core.descriptorSetReady[frameIndex] = VK_TRUE;
    return VKRT_SUCCESS;
}

VKRT_Result createDescriptorSetLayout(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    VkDescriptorSetLayoutBinding bindings[] = {
        {.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR},
        {.binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR},
        {.binding = 2, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR},
        {.binding = 3, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR},
        {.binding = 4, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR},
        {.binding = 5, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR},
        {.binding = 6, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR},
        {.binding = 7, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR},
        {.binding = 8, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_RAYGEN_BIT_KHR},
        {.binding = 9, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_RAYGEN_BIT_KHR},
        {.binding = 10, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR},
        {.binding = 11, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR},
    };

    VkDescriptorSetLayoutCreateInfo createInfo = {0};
    createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    createInfo.bindingCount = VKRT_ARRAY_COUNT(bindings);
    createInfo.pBindings = bindings;

    if (vkCreateDescriptorSetLayout(vkrt->core.device, &createInfo, NULL, &vkrt->core.descriptorSetLayout) != VK_SUCCESS) {
        LOG_ERROR("Failed to create descriptor set layout");
        return VKRT_ERROR_OPERATION_FAILED;
    }
    return VKRT_SUCCESS;
}

VKRT_Result createDescriptorPool(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VKRT_MAX_FRAMES_IN_FLIGHT},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3u * VKRT_MAX_FRAMES_IN_FLIGHT},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 7u * VKRT_MAX_FRAMES_IN_FLIGHT},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VKRT_MAX_FRAMES_IN_FLIGHT},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4u},
    };

    VkDescriptorPoolCreateInfo createInfo = {0};
    createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    createInfo.poolSizeCount = VKRT_ARRAY_COUNT(poolSizes);
    createInfo.pPoolSizes = poolSizes;
    createInfo.maxSets = VKRT_MAX_FRAMES_IN_FLIGHT + 4u;
    createInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

    if (vkCreateDescriptorPool(vkrt->core.device, &createInfo, NULL, &vkrt->core.descriptorPool) != VK_SUCCESS) {
        LOG_ERROR("Failed to create descriptor pool");
        return VKRT_ERROR_OPERATION_FAILED;
    }
    return VKRT_SUCCESS;
}

VKRT_Result createDescriptorSet(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    VkDescriptorSetLayout layouts[VKRT_MAX_FRAMES_IN_FLIGHT];
    for (uint32_t i = 0; i < VKRT_MAX_FRAMES_IN_FLIGHT; i++) {
        layouts[i] = vkrt->core.descriptorSetLayout;
    }

    VkDescriptorSetAllocateInfo allocateInfo = {0};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = vkrt->core.descriptorPool;
    allocateInfo.descriptorSetCount = VKRT_MAX_FRAMES_IN_FLIGHT;
    allocateInfo.pSetLayouts = layouts;

    if (vkAllocateDescriptorSets(vkrt->core.device, &allocateInfo, vkrt->core.descriptorSets) != VK_SUCCESS) {
        LOG_ERROR("Failed to allocate descriptor sets");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    for (uint32_t i = 0; i < VKRT_MAX_FRAMES_IN_FLIGHT; i++) {
        vkrt->core.descriptorSetReady[i] = VK_FALSE;
    }
    return VKRT_SUCCESS;
}

VkBool32 descriptorResourcesReady(VKRT* vkrt) {
    if (!vkrt) return VK_FALSE;
    return descriptorResourcesReadyForFrame(vkrt, vkrt->runtime.currentFrame);
}

VKRT_Result updateDescriptorSet(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    return updateDescriptorSetForFrame(vkrt, vkrt->runtime.currentFrame);
}

VKRT_Result updateAllDescriptorSets(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    for (uint32_t frameIndex = 0; frameIndex < VKRT_MAX_FRAMES_IN_FLIGHT; frameIndex++) {
        VKRT_Result result = updateDescriptorSetForFrame(vkrt, frameIndex);
        if (result != VKRT_SUCCESS) return result;
    }
    return VKRT_SUCCESS;
}
