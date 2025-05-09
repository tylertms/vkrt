#include "descriptor.h"
#include <stdlib.h>
#include <stdio.h>

void createDescriptorSetLayout(VKRT* vkrt) {
    VkDescriptorSetLayoutBinding accelerationStructureLayoutBinding = {0};
    accelerationStructureLayoutBinding.binding = 0;
    accelerationStructureLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    accelerationStructureLayoutBinding.descriptorCount = 1;
    accelerationStructureLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    VkDescriptorSetLayoutBinding resultImageLayoutBinding = {0};
    resultImageLayoutBinding.binding = 1;
    resultImageLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    resultImageLayoutBinding.descriptorCount = 1;
    resultImageLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    VkDescriptorSetLayoutBinding uniformBufferBinding = {0};
    uniformBufferBinding.binding = 2;
    uniformBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uniformBufferBinding.descriptorCount = 1;
    uniformBufferBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    VkDescriptorSetLayoutBinding bindings[] = {
        accelerationStructureLayoutBinding,
        resultImageLayoutBinding,
        uniformBufferBinding
    };

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
        { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 }
    };

    VkDescriptorPoolCreateInfo poolInfo = {0};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = COUNT_OF(poolSizes);
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = MAX_FRAMES_IN_FLIGHT;

    if (vkCreateDescriptorPool(vkrt->device, &poolInfo, NULL, &vkrt->descriptorPool) != VK_SUCCESS) {
        perror("ERROR: Failed to create descriptor pool");
        exit(EXIT_FAILURE);
    }
}

void createDescriptorSet(VKRT* vkrt) {
    VkDescriptorSetAllocateInfo allocInfo = {0};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = vkrt->descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &vkrt->descriptorSetLayout;

    if (vkAllocateDescriptorSets(vkrt->device, &allocInfo, &vkrt->descriptorSet) != VK_SUCCESS) {
        perror("ERROR: Failed to allocate descriptor sets");
        exit(EXIT_FAILURE);
    }

    VkWriteDescriptorSetAccelerationStructureKHR accelerationStructureInfo = {0};
    accelerationStructureInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    accelerationStructureInfo.accelerationStructureCount = 1;
    accelerationStructureInfo.pAccelerationStructures = &vkrt->topLevelAccelerationStructure;
    
    VkWriteDescriptorSet accelerationStructureWrite = {0};
    accelerationStructureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    accelerationStructureWrite.pNext = &accelerationStructureInfo;
    accelerationStructureWrite.dstSet = vkrt->descriptorSet;
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
    
    VkDescriptorBufferInfo sceneUniformInfo = {0};
    sceneUniformInfo.buffer = vkrt->uniformBuffer;
    sceneUniformInfo.offset = 0;
    sceneUniformInfo.range = sizeof(SceneUniform);

    VkWriteDescriptorSet sceneUniformWrite = {0};
    sceneUniformWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    sceneUniformWrite.dstSet = vkrt->descriptorSet;
    sceneUniformWrite.dstBinding = 2;
    sceneUniformWrite.dstArrayElement = 0;
    sceneUniformWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    sceneUniformWrite.descriptorCount = 1;
    sceneUniformWrite.pBufferInfo = &sceneUniformInfo;

    VkWriteDescriptorSet writeDescriptorSets[] = {
        accelerationStructureWrite,
        storageImageWrite,
        sceneUniformWrite
    };

    vkUpdateDescriptorSets(vkrt->device, COUNT_OF(writeDescriptorSets), writeDescriptorSets, 0, VK_NULL_HANDLE);
}