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

    VkDescriptorSetLayoutBinding vertexBufferLayoutBinding = {0};
    vertexBufferLayoutBinding.binding = 2;
    vertexBufferLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    vertexBufferLayoutBinding.descriptorCount = 1;
    vertexBufferLayoutBinding.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

    VkDescriptorSetLayoutBinding indexBufferLayoutBinding = {0};
    indexBufferLayoutBinding.binding = 3;
    indexBufferLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    indexBufferLayoutBinding.descriptorCount = 1;
    indexBufferLayoutBinding.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

    VkDescriptorSetLayoutBinding uniformBufferLayoutBinding = {0};
    uniformBufferLayoutBinding.binding = 4;
    uniformBufferLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uniformBufferLayoutBinding.descriptorCount = 1;
    uniformBufferLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    VkDescriptorSetLayoutBinding bindings[] = {
        accelerationStructureLayoutBinding,
        storageImageLayoutBinding,
        vertexBufferLayoutBinding,
        indexBufferLayoutBinding,
        uniformBufferLayoutBinding};

    VkDescriptorSetLayoutCreateInfo descriptorSetlayoutCreateInfo = {0};
    descriptorSetlayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetlayoutCreateInfo.bindingCount = COUNT_OF(bindings);
    descriptorSetlayoutCreateInfo.pBindings = bindings;

    if (vkCreateDescriptorSetLayout(vkrt->device, &descriptorSetlayoutCreateInfo, NULL, &vkrt->descriptorSetLayout) != VK_SUCCESS) {
        perror("ERROR: Failed to create descriptor set layout");
        exit(EXIT_FAILURE);
    }
}

void createDescriptorPool(VKRT* vkrt) {
    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}};

    VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = {0};
    descriptorPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolCreateInfo.poolSizeCount = COUNT_OF(poolSizes);
    descriptorPoolCreateInfo.pPoolSizes = poolSizes;
    descriptorPoolCreateInfo.maxSets = MAX_FRAMES_IN_FLIGHT;
    descriptorPoolCreateInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

    if (vkCreateDescriptorPool(vkrt->device, &descriptorPoolCreateInfo, NULL, &vkrt->descriptorPool) != VK_SUCCESS) {
        perror("ERROR: Failed to create descriptor pool");
        exit(EXIT_FAILURE);
    }
}

void createDescriptorSet(VKRT* vkrt) {
    VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = {0};
    descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptorSetAllocateInfo.descriptorPool = vkrt->descriptorPool;
    descriptorSetAllocateInfo.descriptorSetCount = 1;
    descriptorSetAllocateInfo.pSetLayouts = &vkrt->descriptorSetLayout;

    if (vkAllocateDescriptorSets(vkrt->device, &descriptorSetAllocateInfo, &vkrt->descriptorSet) != VK_SUCCESS) {
        perror("ERROR: Failed to allocate descriptor sets");
        exit(EXIT_FAILURE);
    }

    updateDescriptorSet(vkrt);
}

void updateDescriptorSet(VKRT* vkrt) {
    VkWriteDescriptorSetAccelerationStructureKHR accelerationStructureInfo = {0};
    accelerationStructureInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    accelerationStructureInfo.accelerationStructureCount = 1;
    accelerationStructureInfo.pAccelerationStructures = &vkrt->topLevelAccelerationStructure;

    VkWriteDescriptorSet accelerationStructureWrite = {0};
    accelerationStructureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    accelerationStructureWrite.pNext = &accelerationStructureInfo;
    accelerationStructureWrite.dstSet = vkrt->descriptorSet;
    accelerationStructureWrite.dstArrayElement = 0;
    accelerationStructureWrite.dstBinding = 0;
    accelerationStructureWrite.descriptorCount = 1;
    accelerationStructureWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

    VkDescriptorImageInfo storageImageInfo = {0};
    storageImageInfo.imageView = vkrt->storageImageView;
    storageImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet storageImageWrite = {0};
    storageImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    storageImageWrite.dstSet = vkrt->descriptorSet;
    storageImageWrite.dstBinding = 1;
    storageImageWrite.dstArrayElement = 0;
    storageImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    storageImageWrite.descriptorCount = 1;
    storageImageWrite.pImageInfo = &storageImageInfo;

    VkDescriptorBufferInfo vertexBufferInfo = {0};
    vertexBufferInfo.buffer = vkrt->vertexBuffer;
    vertexBufferInfo.offset = 0;
    vertexBufferInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet vertexBufferWrite = {0};
    vertexBufferWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    vertexBufferWrite.dstSet = vkrt->descriptorSet;
    vertexBufferWrite.dstBinding = 2;
    vertexBufferWrite.dstArrayElement = 0;
    vertexBufferWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    vertexBufferWrite.descriptorCount = 1;
    vertexBufferWrite.pBufferInfo = &vertexBufferInfo;

    VkDescriptorBufferInfo indexBufferInfo = {0};
    indexBufferInfo.buffer = vkrt->indexBuffer;
    indexBufferInfo.offset = 0;
    indexBufferInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet indexBufferWrite = {0};
    indexBufferWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    indexBufferWrite.dstSet = vkrt->descriptorSet;
    indexBufferWrite.dstBinding = 3;
    indexBufferWrite.dstArrayElement = 0;
    indexBufferWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    indexBufferWrite.descriptorCount = 1;
    indexBufferWrite.pBufferInfo = &indexBufferInfo;

    VkDescriptorBufferInfo sceneUniformInfo = {0};
    sceneUniformInfo.buffer = vkrt->uniformBuffer;
    sceneUniformInfo.offset = 0;
    sceneUniformInfo.range = sizeof(SceneUniform);

    VkWriteDescriptorSet sceneUniformWrite = {0};
    sceneUniformWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    sceneUniformWrite.dstSet = vkrt->descriptorSet;
    sceneUniformWrite.dstBinding = 4;
    sceneUniformWrite.dstArrayElement = 0;
    sceneUniformWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    sceneUniformWrite.descriptorCount = 1;
    sceneUniformWrite.pBufferInfo = &sceneUniformInfo;

    VkWriteDescriptorSet writeDescriptorSets[] = {
        accelerationStructureWrite,
        storageImageWrite,
        vertexBufferWrite,
        indexBufferWrite,
        sceneUniformWrite};

    vkUpdateDescriptorSets(vkrt->device, COUNT_OF(writeDescriptorSets), writeDescriptorSets, 0, VK_NULL_HANDLE);
}