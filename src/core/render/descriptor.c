#include "descriptor.h"

#include <stdio.h>
#include <stdlib.h>

void createDescriptorSetLayout(VKRT* vkrt) {
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

    VkDescriptorSetLayoutBinding meshInfoLayoutBinding = {0};
    meshInfoLayoutBinding.binding = 7;
    meshInfoLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    meshInfoLayoutBinding.descriptorCount = 1;
    meshInfoLayoutBinding.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

    VkDescriptorSetLayoutBinding materialLayoutBinding = {0};
    materialLayoutBinding.binding = 8;
    materialLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    materialLayoutBinding.descriptorCount = 1;
    materialLayoutBinding.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    VkDescriptorSetLayoutBinding bindings[] = {
        accelerationStructureLayoutBinding,
        storageImageLayoutBinding,
        accumulationWriteLayoutBinding,
        outputImageLayoutBinding,
        vertexBufferLayoutBinding,
        indexBufferLayoutBinding,
        sceneDataLayoutBinding,
        meshInfoLayoutBinding,
        materialLayoutBinding
    };

    VkDescriptorSetLayoutCreateInfo descriptorSetlayoutCreateInfo = {0};
    descriptorSetlayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetlayoutCreateInfo.bindingCount = COUNT_OF(bindings);
    descriptorSetlayoutCreateInfo.pBindings = bindings;

    if (vkCreateDescriptorSetLayout(vkrt->core.device, &descriptorSetlayoutCreateInfo, NULL, &vkrt->core.descriptorSetLayout) != VK_SUCCESS) {
        perror("[ERROR]: Failed to create descriptor set layout");
        exit(EXIT_FAILURE);
    }
}

void createDescriptorPool(VKRT* vkrt) {
    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}
    };

    VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = {0};
    descriptorPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolCreateInfo.poolSizeCount = COUNT_OF(poolSizes);
    descriptorPoolCreateInfo.pPoolSizes = poolSizes;
    descriptorPoolCreateInfo.maxSets = 2;
    descriptorPoolCreateInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

    if (vkCreateDescriptorPool(vkrt->core.device, &descriptorPoolCreateInfo, NULL, &vkrt->core.descriptorPool) != VK_SUCCESS) {
        perror("[ERROR]: Failed to create descriptor pool");
        exit(EXIT_FAILURE);
    }
}

void createDescriptorSet(VKRT* vkrt) {
    VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = {0};
    descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptorSetAllocateInfo.descriptorPool = vkrt->core.descriptorPool;
    descriptorSetAllocateInfo.descriptorSetCount = 1;
    descriptorSetAllocateInfo.pSetLayouts = &vkrt->core.descriptorSetLayout;

    if (vkAllocateDescriptorSets(vkrt->core.device, &descriptorSetAllocateInfo, &vkrt->core.descriptorSet) != VK_SUCCESS) {
        perror("[ERROR]: Failed to allocate descriptor sets");
        exit(EXIT_FAILURE);
    }

    vkrt->core.descriptorSetReady = VK_FALSE;

}

VkBool32 descriptorResourcesReady(VKRT* vkrt) {
    if (!vkrt) return VK_FALSE;

    return vkrt->core.topLevelAccelerationStructure.structure != VK_NULL_HANDLE &&
           vkrt->core.vertexData.buffer != VK_NULL_HANDLE &&
           vkrt->core.indexData.buffer != VK_NULL_HANDLE &&
           vkrt->core.meshData.buffer != VK_NULL_HANDLE &&
           vkrt->core.materialData.buffer != VK_NULL_HANDLE;
}

void updateDescriptorSet(VKRT* vkrt) {
    if (!descriptorResourcesReady(vkrt)) {
        vkrt->core.descriptorSetReady = VK_FALSE;
        return;
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

    VkDescriptorBufferInfo meshInfo = {0};
    meshInfo.buffer = vkrt->core.meshData.buffer;
    meshInfo.offset = 0;
    meshInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet meshInfoWrite = {0};
    meshInfoWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    meshInfoWrite.dstSet = vkrt->core.descriptorSet;
    meshInfoWrite.dstBinding = 7;
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
    materialWrite.dstBinding = 8;
    materialWrite.dstArrayElement = 0;
    materialWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    materialWrite.descriptorCount = 1;
    materialWrite.pBufferInfo = &materialInfo;

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
        meshInfoWrite,
        materialWrite,
    };

    vkUpdateDescriptorSets(vkrt->core.device, COUNT_OF(writeDescriptorSets), writeDescriptorSets, 0, VK_NULL_HANDLE);
    vkrt->core.descriptorSetReady = VK_TRUE;
}
