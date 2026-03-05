#include "descriptor.h"
#include "debug.h"

#include <stdio.h>
#include <stdlib.h>
#include "vkrt_internal.h"
#include <vulkan/vulkan_core.h>

VKRT_Result createDescriptorSetLayout(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    VkDescriptorSetLayoutBinding accelerationStructureLayoutBinding = {0};
    accelerationStructureLayoutBinding.binding = 0;
    accelerationStructureLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    accelerationStructureLayoutBinding.descriptorCount = 1;
    accelerationStructureLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    VkDescriptorSetLayoutBinding storageImageLayoutBinding = {0};
    storageImageLayoutBinding.binding = 1;
    storageImageLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    storageImageLayoutBinding.descriptorCount = 1;
    storageImageLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    VkDescriptorSetLayoutBinding accumulationWriteLayoutBinding = {0};
    accumulationWriteLayoutBinding.binding = 2;
    accumulationWriteLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    accumulationWriteLayoutBinding.descriptorCount = 1;
    accumulationWriteLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    VkDescriptorSetLayoutBinding outputImageLayoutBinding = {0};
    outputImageLayoutBinding.binding = 3;
    outputImageLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    outputImageLayoutBinding.descriptorCount = 1;
    outputImageLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    VkDescriptorSetLayoutBinding vertexBufferLayoutBinding = {0};
    vertexBufferLayoutBinding.binding = 4;
    vertexBufferLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    vertexBufferLayoutBinding.descriptorCount = 1;
    vertexBufferLayoutBinding.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

    VkDescriptorSetLayoutBinding indexBufferLayoutBinding = {0};
    indexBufferLayoutBinding.binding = 5;
    indexBufferLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    indexBufferLayoutBinding.descriptorCount = 1;
    indexBufferLayoutBinding.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

    VkDescriptorSetLayoutBinding sceneDataLayoutBinding = {0};
    sceneDataLayoutBinding.binding = 6;
    sceneDataLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    sceneDataLayoutBinding.descriptorCount = 1;
    sceneDataLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    VkDescriptorSetLayoutBinding pickBufferLayoutBinding = {0};
    pickBufferLayoutBinding.binding = 7;
    pickBufferLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    pickBufferLayoutBinding.descriptorCount = 1;
    pickBufferLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    VkDescriptorSetLayoutBinding meshInfoLayoutBinding = {0};
    meshInfoLayoutBinding.binding = 8;
    meshInfoLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    meshInfoLayoutBinding.descriptorCount = 1;
    meshInfoLayoutBinding.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    VkDescriptorSetLayoutBinding materialLayoutBinding = {0};
    materialLayoutBinding.binding = 9;
    materialLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    materialLayoutBinding.descriptorCount = 1;
    materialLayoutBinding.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    VkDescriptorSetLayoutBinding emissiveMeshLayoutBinding = {0};
    emissiveMeshLayoutBinding.binding = 10;
    emissiveMeshLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    emissiveMeshLayoutBinding.descriptorCount = 1;
    emissiveMeshLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    VkDescriptorSetLayoutBinding emissiveTriangleLayoutBinding = {0};
    emissiveTriangleLayoutBinding.binding = 11;
    emissiveTriangleLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    emissiveTriangleLayoutBinding.descriptorCount = 1;
    emissiveTriangleLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    VkDescriptorSetLayoutBinding bindings[] = {
        accelerationStructureLayoutBinding,
        storageImageLayoutBinding,
        accumulationWriteLayoutBinding,
        outputImageLayoutBinding,
        vertexBufferLayoutBinding,
        indexBufferLayoutBinding,
        sceneDataLayoutBinding,
        pickBufferLayoutBinding,
        meshInfoLayoutBinding,
        materialLayoutBinding,
        emissiveMeshLayoutBinding,
        emissiveTriangleLayoutBinding
    };

    VkDescriptorSetLayoutCreateInfo descriptorSetlayoutCreateInfo = {0};
    descriptorSetlayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetlayoutCreateInfo.bindingCount = VKRT_ARRAY_COUNT(bindings);
    descriptorSetlayoutCreateInfo.pBindings = bindings;

    if (vkCreateDescriptorSetLayout(vkrt->core.device, &descriptorSetlayoutCreateInfo, NULL, &vkrt->core.descriptorSetLayout) != VK_SUCCESS) {
        LOG_ERROR("Failed to create descriptor set layout");
        return VKRT_ERROR_OPERATION_FAILED;
    }
    return VKRT_SUCCESS;
}

VKRT_Result createDescriptorPool(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 9},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}
    };

    VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = {0};
    descriptorPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolCreateInfo.poolSizeCount = VKRT_ARRAY_COUNT(poolSizes);
    descriptorPoolCreateInfo.pPoolSizes = poolSizes;
    descriptorPoolCreateInfo.maxSets = 2;
    descriptorPoolCreateInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

    if (vkCreateDescriptorPool(vkrt->core.device, &descriptorPoolCreateInfo, NULL, &vkrt->core.descriptorPool) != VK_SUCCESS) {
        LOG_ERROR("Failed to create descriptor pool");
        return VKRT_ERROR_OPERATION_FAILED;
    }
    return VKRT_SUCCESS;
}

VKRT_Result createDescriptorSet(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = {0};
    descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptorSetAllocateInfo.descriptorPool = vkrt->core.descriptorPool;
    descriptorSetAllocateInfo.descriptorSetCount = 1;
    descriptorSetAllocateInfo.pSetLayouts = &vkrt->core.descriptorSetLayout;

    if (vkAllocateDescriptorSets(vkrt->core.device, &descriptorSetAllocateInfo, &vkrt->core.descriptorSet) != VK_SUCCESS) {
        LOG_ERROR("Failed to allocate descriptor sets");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    vkrt->core.descriptorSetReady = VK_FALSE;
    return VKRT_SUCCESS;
}

VkBool32 descriptorResourcesReady(VKRT* vkrt) {
    if (!vkrt) return VK_FALSE;
    if (vkrt->core.accumulationReadIndex >= 2u || vkrt->core.accumulationWriteIndex >= 2u) {
        return VK_FALSE;
    }

    return vkrt->core.topLevelAccelerationStructure.structure != VK_NULL_HANDLE &&
           vkrt->core.sceneDataBuffer != VK_NULL_HANDLE &&
           vkrt->core.storageImageView != VK_NULL_HANDLE &&
           vkrt->core.accumulationImageViews[vkrt->core.accumulationReadIndex] != VK_NULL_HANDLE &&
           vkrt->core.accumulationImageViews[vkrt->core.accumulationWriteIndex] != VK_NULL_HANDLE &&
           vkrt->core.vertexData.buffer != VK_NULL_HANDLE &&
           vkrt->core.indexData.buffer != VK_NULL_HANDLE &&
           vkrt->core.pickBuffer.buffer != VK_NULL_HANDLE &&
           vkrt->core.meshData.buffer != VK_NULL_HANDLE &&
           vkrt->core.materialData.buffer != VK_NULL_HANDLE &&
           vkrt->core.emissiveMeshData.buffer != VK_NULL_HANDLE &&
           vkrt->core.emissiveTriangleData.buffer != VK_NULL_HANDLE;
}

VKRT_Result updateDescriptorSet(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    if (!descriptorResourcesReady(vkrt)) {
        vkrt->core.descriptorSetReady = VK_FALSE;
        // Empty scenes or transient rebuild states are valid; keep descriptor set disabled.
        return VKRT_SUCCESS;
    }

    VkWriteDescriptorSetAccelerationStructureKHR accelerationStructureInfo = {0};
    accelerationStructureInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    accelerationStructureInfo.accelerationStructureCount = 1;
    accelerationStructureInfo.pAccelerationStructures = &vkrt->core.topLevelAccelerationStructure.structure;

    VkWriteDescriptorSet accelerationStructureWrite = {0};
    accelerationStructureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    accelerationStructureWrite.pNext = &accelerationStructureInfo;
    accelerationStructureWrite.dstSet = vkrt->core.descriptorSet;
    accelerationStructureWrite.dstArrayElement = 0;
    accelerationStructureWrite.dstBinding = 0;
    accelerationStructureWrite.descriptorCount = 1;
    accelerationStructureWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

    VkDescriptorImageInfo storageImageInfo = {0};
    storageImageInfo.imageView = vkrt->core.accumulationImageViews[vkrt->core.accumulationReadIndex];
    storageImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet storageImageWrite = {0};
    storageImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    storageImageWrite.dstSet = vkrt->core.descriptorSet;
    storageImageWrite.dstBinding = 1;
    storageImageWrite.dstArrayElement = 0;
    storageImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    storageImageWrite.descriptorCount = 1;
    storageImageWrite.pImageInfo = &storageImageInfo;

    VkDescriptorBufferInfo vertexBufferInfo = {0};
    vertexBufferInfo.buffer = vkrt->core.vertexData.buffer;
    vertexBufferInfo.offset = 0;
    vertexBufferInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet vertexBufferWrite = {0};
    vertexBufferWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    vertexBufferWrite.dstSet = vkrt->core.descriptorSet;
    vertexBufferWrite.dstBinding = 4;
    vertexBufferWrite.dstArrayElement = 0;
    vertexBufferWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    vertexBufferWrite.descriptorCount = 1;
    vertexBufferWrite.pBufferInfo = &vertexBufferInfo;

    VkDescriptorBufferInfo indexBufferInfo = {0};
    indexBufferInfo.buffer = vkrt->core.indexData.buffer;
    indexBufferInfo.offset = 0;
    indexBufferInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet indexBufferWrite = {0};
    indexBufferWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    indexBufferWrite.dstSet = vkrt->core.descriptorSet;
    indexBufferWrite.dstBinding = 5;
    indexBufferWrite.dstArrayElement = 0;
    indexBufferWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    indexBufferWrite.descriptorCount = 1;
    indexBufferWrite.pBufferInfo = &indexBufferInfo;

    VkDescriptorBufferInfo sceneDataInfo = {0};
    sceneDataInfo.buffer = vkrt->core.sceneDataBuffer;
    sceneDataInfo.offset = 0;
    sceneDataInfo.range = sizeof(SceneData);

    VkWriteDescriptorSet sceneDataWrite = {0};
    sceneDataWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    sceneDataWrite.dstSet = vkrt->core.descriptorSet;
    sceneDataWrite.dstBinding = 6;
    sceneDataWrite.dstArrayElement = 0;
    sceneDataWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    sceneDataWrite.descriptorCount = 1;
    sceneDataWrite.pBufferInfo = &sceneDataInfo;

    VkDescriptorBufferInfo pickBufferInfo = {0};
    pickBufferInfo.buffer = vkrt->core.pickBuffer.buffer;
    pickBufferInfo.offset = 0;
    pickBufferInfo.range = sizeof(PickBuffer);

    VkWriteDescriptorSet pickBufferWrite = {0};
    pickBufferWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    pickBufferWrite.dstSet = vkrt->core.descriptorSet;
    pickBufferWrite.dstBinding = 7;
    pickBufferWrite.dstArrayElement = 0;
    pickBufferWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    pickBufferWrite.descriptorCount = 1;
    pickBufferWrite.pBufferInfo = &pickBufferInfo;

    VkDescriptorBufferInfo meshInfo = {0};
    meshInfo.buffer = vkrt->core.meshData.buffer;
    meshInfo.offset = 0;
    meshInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet meshInfoWrite = {0};
    meshInfoWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    meshInfoWrite.dstSet = vkrt->core.descriptorSet;
    meshInfoWrite.dstBinding = 8;
    meshInfoWrite.dstArrayElement = 0;
    meshInfoWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    meshInfoWrite.descriptorCount = 1;
    meshInfoWrite.pBufferInfo = &meshInfo;

    VkDescriptorBufferInfo materialInfo = {0};
    materialInfo.buffer = vkrt->core.materialData.buffer;
    materialInfo.offset = 0;
    materialInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet materialWrite = {0};
    materialWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    materialWrite.dstSet = vkrt->core.descriptorSet;
    materialWrite.dstBinding = 9;
    materialWrite.dstArrayElement = 0;
    materialWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    materialWrite.descriptorCount = 1;
    materialWrite.pBufferInfo = &materialInfo;

    VkDescriptorBufferInfo emissiveMeshInfo = {0};
    emissiveMeshInfo.buffer = vkrt->core.emissiveMeshData.buffer;
    emissiveMeshInfo.offset = 0;
    emissiveMeshInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet emissiveMeshWrite = {0};
    emissiveMeshWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    emissiveMeshWrite.dstSet = vkrt->core.descriptorSet;
    emissiveMeshWrite.dstBinding = 10;
    emissiveMeshWrite.dstArrayElement = 0;
    emissiveMeshWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    emissiveMeshWrite.descriptorCount = 1;
    emissiveMeshWrite.pBufferInfo = &emissiveMeshInfo;

    VkDescriptorBufferInfo emissiveTriangleInfo = {0};
    emissiveTriangleInfo.buffer = vkrt->core.emissiveTriangleData.buffer;
    emissiveTriangleInfo.offset = 0;
    emissiveTriangleInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet emissiveTriangleWrite = {0};
    emissiveTriangleWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    emissiveTriangleWrite.dstSet = vkrt->core.descriptorSet;
    emissiveTriangleWrite.dstBinding = 11;
    emissiveTriangleWrite.dstArrayElement = 0;
    emissiveTriangleWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    emissiveTriangleWrite.descriptorCount = 1;
    emissiveTriangleWrite.pBufferInfo = &emissiveTriangleInfo;

    VkDescriptorImageInfo accumulationWriteInfo = {0};
    accumulationWriteInfo.imageView = vkrt->core.accumulationImageViews[vkrt->core.accumulationWriteIndex];
    accumulationWriteInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet accumulationWrite = {0};
    accumulationWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    accumulationWrite.dstSet = vkrt->core.descriptorSet;
    accumulationWrite.dstBinding = 2;
    accumulationWrite.dstArrayElement = 0;
    accumulationWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    accumulationWrite.descriptorCount = 1;
    accumulationWrite.pImageInfo = &accumulationWriteInfo;

    VkDescriptorImageInfo outputImageInfo = {0};
    outputImageInfo.imageView = vkrt->core.storageImageView;
    outputImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet outputImageWrite = {0};
    outputImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    outputImageWrite.dstSet = vkrt->core.descriptorSet;
    outputImageWrite.dstBinding = 3;
    outputImageWrite.dstArrayElement = 0;
    outputImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    outputImageWrite.descriptorCount = 1;
    outputImageWrite.pImageInfo = &outputImageInfo;

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
    vkrt->core.descriptorSetReady = VK_TRUE;
    return VKRT_SUCCESS;
}
