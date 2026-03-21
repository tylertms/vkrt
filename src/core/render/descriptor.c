#include "descriptor.h"
#include "debug.h"

#include "vkrt_internal.h"
#include <vulkan/vulkan_core.h>

static VkBool32 textureDescriptorsReady(const VKRT* vkrt) {
    if (!vkrt ||
        vkrt->core.textureFallbackView == VK_NULL_HANDLE ||
        vkrt->core.textureCount > VKRT_MAX_BINDLESS_TEXTURES) {
        return VK_FALSE;
    }

    for (uint32_t i = 0; i < VKRT_TEXTURE_SAMPLER_VARIANT_COUNT; i++) {
        if (vkrt->core.textureSamplers[i] == VK_NULL_HANDLE) {
            return VK_FALSE;
        }
    }

    for (uint32_t i = 0; i < vkrt->core.textureCount; i++) {
        if (vkrt->core.textures[i].view == VK_NULL_HANDLE) {
            return VK_FALSE;
        }
    }
    return VK_TRUE;
}

static VkBool32 descriptorResourcesReadyForFrame(VKRT* vkrt, uint32_t frameIndex) {
    if (!vkrt || frameIndex >= VKRT_MAX_FRAMES_IN_FLIGHT) return VK_FALSE;
    if (vkrt->core.accumulationReadIndex >= 2u || vkrt->core.accumulationWriteIndex >= 2u) {
        return VK_FALSE;
    }

    return vkrt->core.sceneDataBuffers[frameIndex] != VK_NULL_HANDLE &&
           vkrt->core.outputImageView != VK_NULL_HANDLE &&
           vkrt->core.selectionMaskImageView != VK_NULL_HANDLE &&
           vkrt->core.accumulationImageViews[vkrt->core.accumulationReadIndex] != VK_NULL_HANDLE &&
           vkrt->core.accumulationImageViews[vkrt->core.accumulationWriteIndex] != VK_NULL_HANDLE &&
           vkrt->core.vertexData.buffer != VK_NULL_HANDLE &&
           vkrt->core.indexData.buffer != VK_NULL_HANDLE &&
           vkrt->core.selection.buffer != VK_NULL_HANDLE &&
           vkrt->core.sceneMeshData.buffer != VK_NULL_HANDLE &&
           vkrt->core.sceneMaterialData.buffer != VK_NULL_HANDLE &&
           vkrt->core.sceneEmissiveMeshData.buffer != VK_NULL_HANDLE &&
           vkrt->core.sceneEmissiveTriangleData.buffer != VK_NULL_HANDLE &&
           vkrt->core.sceneMeshAliasQ.buffer != VK_NULL_HANDLE &&
           vkrt->core.sceneMeshAliasIdx.buffer != VK_NULL_HANDLE &&
           vkrt->core.sceneTriAliasQ.buffer != VK_NULL_HANDLE &&
           vkrt->core.sceneTriAliasIdx.buffer != VK_NULL_HANDLE &&
           textureDescriptorsReady(vkrt);
}

static VKRT_Result updateDescriptorSetForFrame(VKRT* vkrt, uint32_t frameIndex) {
    if (!vkrt || frameIndex >= VKRT_MAX_FRAMES_IN_FLIGHT) return VKRT_ERROR_INVALID_ARGUMENT;
    if (!descriptorResourcesReadyForFrame(vkrt, frameIndex) ||
        vkrt->core.sceneTopLevelAccelerationStructure.structure == VK_NULL_HANDLE) {
        vkrt->core.descriptorSetReady[frameIndex] = VK_FALSE;
        return VKRT_SUCCESS;
    }
    Buffer* meshData = &vkrt->core.sceneMeshData;
    Buffer* materialData = &vkrt->core.sceneMaterialData;
    Buffer* emissiveMeshData = &vkrt->core.sceneEmissiveMeshData;
    Buffer* emissiveTriangleData = &vkrt->core.sceneEmissiveTriangleData;
    Buffer* meshAliasQBuf = &vkrt->core.sceneMeshAliasQ;
    Buffer* meshAliasIdxBuf = &vkrt->core.sceneMeshAliasIdx;
    Buffer* triAliasQBuf = &vkrt->core.sceneTriAliasQ;
    Buffer* triAliasIdxBuf = &vkrt->core.sceneTriAliasIdx;
    VkDescriptorSet descriptorSet = vkrt->core.descriptorSets[frameIndex];
    VkAccelerationStructureKHR sceneTLAS = vkrt->core.sceneTopLevelAccelerationStructure.structure;
    VkAccelerationStructureKHR selectionTLAS = vkrt->core.selectionTopLevelAccelerationStructure.structure;

    if (selectionTLAS == VK_NULL_HANDLE) {
        selectionTLAS = sceneTLAS;
    }

    VkAccelerationStructureKHR accelerationStructures[] = {
        sceneTLAS,
        selectionTLAS,
    };

    VkWriteDescriptorSetAccelerationStructureKHR accelerationStructureInfos[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
            .accelerationStructureCount = 1,
            .pAccelerationStructures = &accelerationStructures[0],
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
            .accelerationStructureCount = 1,
            .pAccelerationStructures = &accelerationStructures[1],
        },
    };

    VkWriteDescriptorSet accelerationStructureWrites[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = &accelerationStructureInfos[0],
            .dstSet = descriptorSet,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = &accelerationStructureInfos[1],
            .dstSet = descriptorSet,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
        },
    };

    VkDescriptorImageInfo accumulationReadInfo = {0};
    accumulationReadInfo.imageView = vkrt->core.accumulationImageViews[vkrt->core.accumulationReadIndex];
    accumulationReadInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet accumulationReadWrite = {0};
    accumulationReadWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    accumulationReadWrite.dstSet = descriptorSet;
    accumulationReadWrite.dstBinding = 2;
    accumulationReadWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    accumulationReadWrite.descriptorCount = 1;
    accumulationReadWrite.pImageInfo = &accumulationReadInfo;

    VkDescriptorImageInfo accumulationWriteInfo = {0};
    accumulationWriteInfo.imageView = vkrt->core.accumulationImageViews[vkrt->core.accumulationWriteIndex];
    accumulationWriteInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet accumulationWrite = {0};
    accumulationWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    accumulationWrite.dstSet = descriptorSet;
    accumulationWrite.dstBinding = 3;
    accumulationWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    accumulationWrite.descriptorCount = 1;
    accumulationWrite.pImageInfo = &accumulationWriteInfo;

    VkDescriptorImageInfo outputImageInfo = {0};
    outputImageInfo.imageView = vkrt->core.outputImageView;
    outputImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet outputImageWrite = {0};
    outputImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    outputImageWrite.dstSet = descriptorSet;
    outputImageWrite.dstBinding = 4;
    outputImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    outputImageWrite.descriptorCount = 1;
    outputImageWrite.pImageInfo = &outputImageInfo;

    VkDescriptorImageInfo selectionMaskImageInfo = {0};
    selectionMaskImageInfo.imageView = vkrt->core.selectionMaskImageView;
    selectionMaskImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet selectionMaskImageWrite = {0};
    selectionMaskImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    selectionMaskImageWrite.dstSet = descriptorSet;
    selectionMaskImageWrite.dstBinding = 5;
    selectionMaskImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    selectionMaskImageWrite.descriptorCount = 1;
    selectionMaskImageWrite.pImageInfo = &selectionMaskImageInfo;

    VkDescriptorBufferInfo verticesInfo = {
        .buffer = vkrt->core.vertexData.buffer,
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    };

    VkWriteDescriptorSet verticesWrite = {0};
    verticesWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    verticesWrite.dstSet = descriptorSet;
    verticesWrite.dstBinding = 6;
    verticesWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    verticesWrite.descriptorCount = 1;
    verticesWrite.pBufferInfo = &verticesInfo;

    VkDescriptorBufferInfo indicesInfo = {
        .buffer = vkrt->core.indexData.buffer,
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    };

    VkWriteDescriptorSet indicesWrite = {0};
    indicesWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    indicesWrite.dstSet = descriptorSet;
    indicesWrite.dstBinding = 7;
    indicesWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    indicesWrite.descriptorCount = 1;
    indicesWrite.pBufferInfo = &indicesInfo;

    VkDescriptorBufferInfo sceneDataInfo = {
        .buffer = vkrt->core.sceneDataBuffers[frameIndex],
        .offset = 0,
        .range = sizeof(SceneData),
    };

    VkWriteDescriptorSet sceneDataWrite = {0};
    sceneDataWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    sceneDataWrite.dstSet = descriptorSet;
    sceneDataWrite.dstBinding = 8;
    sceneDataWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    sceneDataWrite.descriptorCount = 1;
    sceneDataWrite.pBufferInfo = &sceneDataInfo;

    VkDescriptorBufferInfo selectionInfo = {
        .buffer = vkrt->core.selection.buffer,
        .offset = 0,
        .range = sizeof(Selection),
    };

    VkWriteDescriptorSet selectionWrite = {0};
    selectionWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    selectionWrite.dstSet = descriptorSet;
    selectionWrite.dstBinding = 9;
    selectionWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    selectionWrite.descriptorCount = 1;
    selectionWrite.pBufferInfo = &selectionInfo;

    VkDescriptorBufferInfo meshInfo = {
        .buffer = meshData->buffer,
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    };

    VkWriteDescriptorSet meshInfoWrite = {0};
    meshInfoWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    meshInfoWrite.dstSet = descriptorSet;
    meshInfoWrite.dstBinding = 10;
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
    materialWrite.dstBinding = 11;
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
    emissiveMeshWrite.dstBinding = 12;
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
    emissiveTriangleWrite.dstBinding = 13;
    emissiveTriangleWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    emissiveTriangleWrite.descriptorCount = 1;
    emissiveTriangleWrite.pBufferInfo = &emissiveTriangleInfo;

    VkDescriptorBufferInfo meshAliasQInfo = {
        .buffer = meshAliasQBuf->buffer,
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    };

    VkWriteDescriptorSet meshAliasQWrite = {0};
    meshAliasQWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    meshAliasQWrite.dstSet = descriptorSet;
    meshAliasQWrite.dstBinding = 14;
    meshAliasQWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    meshAliasQWrite.descriptorCount = 1;
    meshAliasQWrite.pBufferInfo = &meshAliasQInfo;

    VkDescriptorBufferInfo meshAliasIdxInfo = {
        .buffer = meshAliasIdxBuf->buffer,
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    };

    VkWriteDescriptorSet meshAliasIdxWrite = {0};
    meshAliasIdxWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    meshAliasIdxWrite.dstSet = descriptorSet;
    meshAliasIdxWrite.dstBinding = 15;
    meshAliasIdxWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    meshAliasIdxWrite.descriptorCount = 1;
    meshAliasIdxWrite.pBufferInfo = &meshAliasIdxInfo;

    VkDescriptorBufferInfo triAliasQInfo = {
        .buffer = triAliasQBuf->buffer,
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    };

    VkWriteDescriptorSet triAliasQWrite = {0};
    triAliasQWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    triAliasQWrite.dstSet = descriptorSet;
    triAliasQWrite.dstBinding = 16;
    triAliasQWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    triAliasQWrite.descriptorCount = 1;
    triAliasQWrite.pBufferInfo = &triAliasQInfo;

    VkDescriptorBufferInfo triAliasIdxInfo = {
        .buffer = triAliasIdxBuf->buffer,
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    };

    VkWriteDescriptorSet triAliasIdxWrite = {0};
    triAliasIdxWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    triAliasIdxWrite.dstSet = descriptorSet;
    triAliasIdxWrite.dstBinding = 17;
    triAliasIdxWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    triAliasIdxWrite.descriptorCount = 1;
    triAliasIdxWrite.pBufferInfo = &triAliasIdxInfo;

    VkDescriptorImageInfo samplerInfos[VKRT_TEXTURE_SAMPLER_VARIANT_COUNT] = {0};
    for (uint32_t i = 0; i < VKRT_TEXTURE_SAMPLER_VARIANT_COUNT; i++) {
        samplerInfos[i].sampler = vkrt->core.textureSamplers[i];
    }

    VkWriteDescriptorSet samplerWrite = {0};
    samplerWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    samplerWrite.dstSet = descriptorSet;
    samplerWrite.dstBinding = 18;
    samplerWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    samplerWrite.descriptorCount = VKRT_TEXTURE_SAMPLER_VARIANT_COUNT;
    samplerWrite.pImageInfo = samplerInfos;

    VkDescriptorImageInfo textureBindings[VKRT_MAX_BINDLESS_TEXTURES] = {0};
    for (uint32_t i = 0; i < VKRT_MAX_BINDLESS_TEXTURES; i++) {
        textureBindings[i].imageView = vkrt->core.textureFallbackView;
        textureBindings[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    for (uint32_t i = 0; i < vkrt->core.textureCount; i++) {
        textureBindings[i].imageView = vkrt->core.textures[i].view;
    }

    VkWriteDescriptorSet textureWrite = {0};
    textureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    textureWrite.dstSet = descriptorSet;
    textureWrite.dstBinding = 19;
    textureWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    textureWrite.descriptorCount = VKRT_MAX_BINDLESS_TEXTURES;
    textureWrite.pImageInfo = textureBindings;

    VkWriteDescriptorSet writeDescriptorSets[] = {
        accelerationStructureWrites[0],
        accelerationStructureWrites[1],
        accumulationReadWrite,
        accumulationWrite,
        outputImageWrite,
        selectionMaskImageWrite,
        verticesWrite,
        indicesWrite,
        sceneDataWrite,
        selectionWrite,
        meshInfoWrite,
        materialWrite,
        emissiveMeshWrite,
        emissiveTriangleWrite,
        meshAliasQWrite,
        meshAliasIdxWrite,
        triAliasQWrite,
        triAliasIdxWrite,
        samplerWrite,
        textureWrite,
    };

    vkUpdateDescriptorSets(vkrt->core.device, VKRT_ARRAY_COUNT(writeDescriptorSets), writeDescriptorSets, 0, VK_NULL_HANDLE);
    vkrt->core.descriptorSetReady[frameIndex] = VK_TRUE;
    return VKRT_SUCCESS;
}

VKRT_Result createDescriptorSetLayout(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    VkShaderStageFlags rtAll = VK_SHADER_STAGE_RAYGEN_BIT_KHR |
        VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |
        VK_SHADER_STAGE_ANY_HIT_BIT_KHR |
        VK_SHADER_STAGE_MISS_BIT_KHR;
    VkShaderStageFlags rgen = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    VkShaderStageFlags rhit = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
    VkShaderStageFlags comp = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding bindings[] = {
        {.binding = 0,  .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, .descriptorCount = 1, .stageFlags = rgen},
        {.binding = 1,  .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, .descriptorCount = 1, .stageFlags = rgen},
        {.binding = 2,  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,              .descriptorCount = 1, .stageFlags = rgen},
        {.binding = 3,  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,              .descriptorCount = 1, .stageFlags = rgen},
        {.binding = 4,  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,              .descriptorCount = 1, .stageFlags = rgen | comp},
        {.binding = 5,  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,              .descriptorCount = 1, .stageFlags = rgen | comp},
        {.binding = 6,  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             .descriptorCount = 1, .stageFlags = rhit},
        {.binding = 7,  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             .descriptorCount = 1, .stageFlags = rhit},
        {.binding = 8,  .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,             .descriptorCount = 1, .stageFlags = rtAll | comp},
        {.binding = 9,  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             .descriptorCount = 1, .stageFlags = rgen},
        {.binding = 10, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             .descriptorCount = 1, .stageFlags = rgen | rhit},
        {.binding = 11, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             .descriptorCount = 1, .stageFlags = rgen | rhit},
        {.binding = 12, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             .descriptorCount = 1, .stageFlags = rgen},
        {.binding = 13, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             .descriptorCount = 1, .stageFlags = rgen},
        {.binding = 14, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             .descriptorCount = 1, .stageFlags = rgen},
        {.binding = 15, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             .descriptorCount = 1, .stageFlags = rgen},
        {.binding = 16, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             .descriptorCount = 1, .stageFlags = rgen},
        {.binding = 17, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             .descriptorCount = 1, .stageFlags = rgen},
        {.binding = 18, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,                    .descriptorCount = VKRT_TEXTURE_SAMPLER_VARIANT_COUNT, .stageFlags = rtAll},
        {.binding = 19, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,              .descriptorCount = VKRT_MAX_BINDLESS_TEXTURES, .stageFlags = rtAll},
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

    static const VkDescriptorPoolSize rendererPoolSizes[] = {
        {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 2u * VKRT_MAX_FRAMES_IN_FLIGHT},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4u * VKRT_MAX_FRAMES_IN_FLIGHT},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 11u * VKRT_MAX_FRAMES_IN_FLIGHT},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VKRT_MAX_FRAMES_IN_FLIGHT},
        {VK_DESCRIPTOR_TYPE_SAMPLER, VKRT_TEXTURE_SAMPLER_VARIANT_COUNT * VKRT_MAX_FRAMES_IN_FLIGHT},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VKRT_MAX_BINDLESS_TEXTURES * VKRT_MAX_FRAMES_IN_FLIGHT},
    };
    static const VkDescriptorPoolSize overlayPoolSizes[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLER, 128u},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 128u},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 128u},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 128u},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 32u},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 32u},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 128u},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 128u},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 32u},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 32u},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 32u},
    };

    VkDescriptorPoolCreateInfo createInfo = {0};
    createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    createInfo.poolSizeCount = VKRT_ARRAY_COUNT(rendererPoolSizes);
    createInfo.pPoolSizes = rendererPoolSizes;
    createInfo.maxSets = VKRT_MAX_FRAMES_IN_FLIGHT + 4u;
    createInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

    if (vkCreateDescriptorPool(vkrt->core.device, &createInfo, NULL, &vkrt->core.descriptorPool) != VK_SUCCESS) {
        LOG_ERROR("Failed to create descriptor pool");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    createInfo.poolSizeCount = VKRT_ARRAY_COUNT(overlayPoolSizes);
    createInfo.pPoolSizes = overlayPoolSizes;
    createInfo.maxSets = 512u;

    if (vkCreateDescriptorPool(vkrt->core.device, &createInfo, NULL, &vkrt->core.overlayDescriptorPool) != VK_SUCCESS) {
        LOG_ERROR("Failed to create overlay descriptor pool");
        vkDestroyDescriptorPool(vkrt->core.device, vkrt->core.descriptorPool, NULL);
        vkrt->core.descriptorPool = VK_NULL_HANDLE;
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
    return descriptorResourcesReadyForFrame(vkrt, vkrt->runtime.currentFrame) &&
           vkrt->core.sceneTopLevelAccelerationStructure.structure != VK_NULL_HANDLE;
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
