#include "descriptor.h"

#include "config.h"
#include "constants.h"
#include "debug.h"
#include "types.h"
#include "vkrt_internal.h"
#include "vkrt_types.h"

#include <stdint.h>
#include <vulkan/vulkan_core.h>

enum {
    K_ACCELERATION_STRUCTURE_DESCRIPTOR_COUNT = 2u,
    K_IMAGE_DESCRIPTOR_COUNT = 8u,
    K_BUFFER_DESCRIPTOR_COUNT = 12u,
    K_RENDER_DESCRIPTOR_BINDING_COUNT = 24u,
};

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
           vkrt->core.albedoImageViews[vkrt->core.accumulationReadIndex] != VK_NULL_HANDLE &&
           vkrt->core.albedoImageViews[vkrt->core.accumulationWriteIndex] != VK_NULL_HANDLE &&
           vkrt->core.normalImageViews[vkrt->core.accumulationReadIndex] != VK_NULL_HANDLE &&
           vkrt->core.normalImageViews[vkrt->core.accumulationWriteIndex] != VK_NULL_HANDLE &&
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

static VkWriteDescriptorSet makeDescriptorWrite(
    VkDescriptorSet descriptorSet,
    uint32_t binding,
    VkDescriptorType descriptorType,
    uint32_t descriptorCount
) {
    return (VkWriteDescriptorSet){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = descriptorSet,
        .dstBinding = binding,
        .descriptorType = descriptorType,
        .descriptorCount = descriptorCount,
    };
}

static VkDescriptorImageInfo makeImageInfo(VkImageView imageView, VkImageLayout imageLayout) {
    return (VkDescriptorImageInfo){
        .imageView = imageView,
        .imageLayout = imageLayout,
    };
}

static VkDescriptorBufferInfo makeBufferInfo(VkBuffer buffer, VkDeviceSize range) {
    return (VkDescriptorBufferInfo){
        .buffer = buffer,
        .offset = 0,
        .range = range,
    };
}

static void appendAccelerationStructureWrite(
    VkWriteDescriptorSet* write,
    VkWriteDescriptorSetAccelerationStructureKHR* info,
    VkDescriptorSet descriptorSet,
    uint32_t binding,
    VkAccelerationStructureKHR* structure
) {
    if (!write || !info || !structure) return;

    *info = (VkWriteDescriptorSetAccelerationStructureKHR){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
        .accelerationStructureCount = 1,
        .pAccelerationStructures = structure,
    };
    *write = makeDescriptorWrite(descriptorSet, binding, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1);
    write->pNext = info;
}

typedef struct ImageDescriptorBinding {
    uint32_t binding;
    VkImageView imageView;
} ImageDescriptorBinding;

typedef struct BufferDescriptorBinding {
    uint32_t binding;
    VkDescriptorType descriptorType;
    VkBuffer buffer;
    VkDeviceSize range;
} BufferDescriptorBinding;

typedef struct AccelerationStructureWriteState {
    VkAccelerationStructureKHR structures[K_ACCELERATION_STRUCTURE_DESCRIPTOR_COUNT];
    VkWriteDescriptorSetAccelerationStructureKHR infos[K_ACCELERATION_STRUCTURE_DESCRIPTOR_COUNT];
    VkWriteDescriptorSet writes[K_ACCELERATION_STRUCTURE_DESCRIPTOR_COUNT];
} AccelerationStructureWriteState;

typedef struct ImageDescriptorWriteState {
    VkDescriptorImageInfo infos[K_IMAGE_DESCRIPTOR_COUNT];
    VkWriteDescriptorSet writes[K_IMAGE_DESCRIPTOR_COUNT];
} ImageDescriptorWriteState;

typedef struct BufferDescriptorWriteState {
    VkDescriptorBufferInfo infos[K_BUFFER_DESCRIPTOR_COUNT];
    VkWriteDescriptorSet writes[K_BUFFER_DESCRIPTOR_COUNT];
} BufferDescriptorWriteState;

typedef struct TextureDescriptorWriteState {
    VkDescriptorImageInfo samplerInfos[VKRT_TEXTURE_SAMPLER_VARIANT_COUNT];
    VkDescriptorImageInfo textureBindings[VKRT_MAX_BINDLESS_TEXTURES];
    VkWriteDescriptorSet samplerWrite;
    VkWriteDescriptorSet textureWrite;
} TextureDescriptorWriteState;

static void initializeAccelerationStructureWrites(
    VkDescriptorSet descriptorSet,
    VkAccelerationStructureKHR sceneTLAS,
    VkAccelerationStructureKHR selectionTLAS,
    AccelerationStructureWriteState* state
) {
    if (!state) return;

    state->structures[0] = sceneTLAS;
    state->structures[1] = selectionTLAS != VK_NULL_HANDLE ? selectionTLAS : sceneTLAS;
    appendAccelerationStructureWrite(&state->writes[0], &state->infos[0], descriptorSet, 0u, &state->structures[0]);
    appendAccelerationStructureWrite(&state->writes[1], &state->infos[1], descriptorSet, 1u, &state->structures[1]);
}

static void appendImageDescriptorWrites(
    VkDescriptorSet descriptorSet,
    const ImageDescriptorBinding* bindings,
    uint32_t bindingCount,
    ImageDescriptorWriteState* state
) {
    if (!bindings || !state) return;

    for (uint32_t i = 0; i < bindingCount; i++) {
        state->infos[i] = makeImageInfo(bindings[i].imageView, VK_IMAGE_LAYOUT_GENERAL);
        state->writes[i] = makeDescriptorWrite(descriptorSet, bindings[i].binding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1);
        state->writes[i].pImageInfo = &state->infos[i];
    }
}

static void appendBufferDescriptorWrites(
    VkDescriptorSet descriptorSet,
    const BufferDescriptorBinding* bindings,
    uint32_t bindingCount,
    BufferDescriptorWriteState* state
) {
    if (!bindings || !state) return;

    for (uint32_t i = 0; i < bindingCount; i++) {
        state->infos[i] = makeBufferInfo(bindings[i].buffer, bindings[i].range);
        state->writes[i] = makeDescriptorWrite(
            descriptorSet,
            bindings[i].binding,
            bindings[i].descriptorType,
            1
        );
        state->writes[i].pBufferInfo = &state->infos[i];
    }
}

static void initializeTextureDescriptorWrites(VKRT* vkrt, VkDescriptorSet descriptorSet, TextureDescriptorWriteState* state) {
    if (!vkrt || !state) return;

    for (uint32_t i = 0; i < VKRT_TEXTURE_SAMPLER_VARIANT_COUNT; i++) {
        state->samplerInfos[i].sampler = vkrt->core.textureSamplers[i];
    }
    state->samplerWrite = makeDescriptorWrite(
        descriptorSet,
        22u,
        VK_DESCRIPTOR_TYPE_SAMPLER,
        VKRT_TEXTURE_SAMPLER_VARIANT_COUNT
    );
    state->samplerWrite.pImageInfo = state->samplerInfos;

    for (uint32_t i = 0; i < VKRT_MAX_BINDLESS_TEXTURES; i++) {
        state->textureBindings[i].imageView = vkrt->core.textureFallbackView;
        state->textureBindings[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    for (uint32_t i = 0; i < vkrt->core.textureCount; i++) {
        state->textureBindings[i].imageView = vkrt->core.textures[i].view;
    }
    state->textureWrite = makeDescriptorWrite(
        descriptorSet,
        23u,
        VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        VKRT_MAX_BINDLESS_TEXTURES
    );
    state->textureWrite.pImageInfo = state->textureBindings;
}

static VKRT_Result updateDescriptorSetForFrame(VKRT* vkrt, uint32_t frameIndex) {
    if (!vkrt || frameIndex >= VKRT_MAX_FRAMES_IN_FLIGHT) return VKRT_ERROR_INVALID_ARGUMENT;
    if (!descriptorResourcesReadyForFrame(vkrt, frameIndex) ||
        vkrt->core.sceneTopLevelAccelerationStructure.structure == VK_NULL_HANDLE) {
        vkrt->core.descriptorSetReady[frameIndex] = VK_FALSE;
        return VKRT_SUCCESS;
    }

    VkDescriptorSet descriptorSet = vkrt->core.descriptorSets[frameIndex];
    AccelerationStructureWriteState accelerationState = {0};
    initializeAccelerationStructureWrites(
        descriptorSet,
        vkrt->core.sceneTopLevelAccelerationStructure.structure,
        vkrt->core.selectionTopLevelAccelerationStructure.structure,
        &accelerationState
    );

    ImageDescriptorBinding imageBindings[] = {
        {2u, vkrt->core.accumulationImageViews[vkrt->core.accumulationReadIndex]},
        {3u, vkrt->core.accumulationImageViews[vkrt->core.accumulationWriteIndex]},
        {4u, vkrt->core.outputImageView},
        {5u, vkrt->core.selectionMaskImageView},
        {6u, vkrt->core.albedoImageViews[vkrt->core.accumulationReadIndex]},
        {7u, vkrt->core.albedoImageViews[vkrt->core.accumulationWriteIndex]},
        {8u, vkrt->core.normalImageViews[vkrt->core.accumulationReadIndex]},
        {9u, vkrt->core.normalImageViews[vkrt->core.accumulationWriteIndex]},
    };
    ImageDescriptorWriteState imageState = {0};
    appendImageDescriptorWrites(
        descriptorSet,
        imageBindings,
        (uint32_t)(sizeof(imageBindings) / sizeof(imageBindings[0])),
        &imageState
    );

    BufferDescriptorBinding bufferBindings[] = {
        {10u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, vkrt->core.vertexData.buffer, VK_WHOLE_SIZE},
        {11u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, vkrt->core.indexData.buffer, VK_WHOLE_SIZE},
        {12u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, vkrt->core.sceneDataBuffers[frameIndex], sizeof(SceneData)},
        {13u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, vkrt->core.selection.buffer, sizeof(Selection)},
        {14u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, vkrt->core.sceneMeshData.buffer, VK_WHOLE_SIZE},
        {15u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, vkrt->core.sceneMaterialData.buffer, VK_WHOLE_SIZE},
        {16u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, vkrt->core.sceneEmissiveMeshData.buffer, VK_WHOLE_SIZE},
        {17u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, vkrt->core.sceneEmissiveTriangleData.buffer, VK_WHOLE_SIZE},
        {18u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, vkrt->core.sceneMeshAliasQ.buffer, VK_WHOLE_SIZE},
        {19u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, vkrt->core.sceneMeshAliasIdx.buffer, VK_WHOLE_SIZE},
        {20u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, vkrt->core.sceneTriAliasQ.buffer, VK_WHOLE_SIZE},
        {21u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, vkrt->core.sceneTriAliasIdx.buffer, VK_WHOLE_SIZE},
    };
    BufferDescriptorWriteState bufferState = {0};
    appendBufferDescriptorWrites(
        descriptorSet,
        bufferBindings,
        (uint32_t)(sizeof(bufferBindings) / sizeof(bufferBindings[0])),
        &bufferState
    );
    TextureDescriptorWriteState textureState = {0};
    initializeTextureDescriptorWrites(vkrt, descriptorSet, &textureState);

    VkWriteDescriptorSet writeDescriptorSets[K_RENDER_DESCRIPTOR_BINDING_COUNT] = {0};
    uint32_t writeCount = 0u;
    for (uint32_t i = 0; i < K_ACCELERATION_STRUCTURE_DESCRIPTOR_COUNT; i++) {
        writeDescriptorSets[writeCount++] = accelerationState.writes[i];
    }
    for (uint32_t i = 0; i < K_IMAGE_DESCRIPTOR_COUNT; i++) {
        writeDescriptorSets[writeCount++] = imageState.writes[i];
    }
    for (uint32_t i = 0; i < K_BUFFER_DESCRIPTOR_COUNT; i++) {
        writeDescriptorSets[writeCount++] = bufferState.writes[i];
    }
    writeDescriptorSets[writeCount++] = textureState.samplerWrite;
    writeDescriptorSets[writeCount++] = textureState.textureWrite;

    vkUpdateDescriptorSets(vkrt->core.device, writeCount, writeDescriptorSets, 0, VK_NULL_HANDLE);
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
        {.binding = 6,  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,              .descriptorCount = 1, .stageFlags = rgen},
        {.binding = 7,  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,              .descriptorCount = 1, .stageFlags = rgen},
        {.binding = 8,  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,              .descriptorCount = 1, .stageFlags = rgen},
        {.binding = 9,  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,              .descriptorCount = 1, .stageFlags = rgen},
        {.binding = 10, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             .descriptorCount = 1, .stageFlags = rhit},
        {.binding = 11, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             .descriptorCount = 1, .stageFlags = rhit},
        {.binding = 12, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,             .descriptorCount = 1, .stageFlags = rtAll | comp},
        {.binding = 13, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             .descriptorCount = 1, .stageFlags = rgen},
        {.binding = 14, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             .descriptorCount = 1, .stageFlags = rgen | rhit},
        {.binding = 15, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             .descriptorCount = 1, .stageFlags = rgen | rhit},
        {.binding = 16, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             .descriptorCount = 1, .stageFlags = rgen},
        {.binding = 17, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             .descriptorCount = 1, .stageFlags = rgen},
        {.binding = 18, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             .descriptorCount = 1, .stageFlags = rgen},
        {.binding = 19, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             .descriptorCount = 1, .stageFlags = rgen},
        {.binding = 20, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             .descriptorCount = 1, .stageFlags = rgen},
        {.binding = 21, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             .descriptorCount = 1, .stageFlags = rgen},
        {.binding = 22, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,                    .descriptorCount = VKRT_TEXTURE_SAMPLER_VARIANT_COUNT, .stageFlags = rtAll},
        {.binding = 23, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,              .descriptorCount = VKRT_MAX_BINDLESS_TEXTURES, .stageFlags = rtAll},
    };

    VkDescriptorSetLayoutCreateInfo createInfo = {0};
    createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    createInfo.bindingCount = (uint32_t)(sizeof(bindings) / sizeof(bindings[0]));
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
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 8u * VKRT_MAX_FRAMES_IN_FLIGHT},
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
    createInfo.poolSizeCount = (uint32_t)(sizeof(rendererPoolSizes) / sizeof(rendererPoolSizes[0]));
    createInfo.pPoolSizes = rendererPoolSizes;
    createInfo.maxSets = VKRT_MAX_FRAMES_IN_FLIGHT + 4u;
    createInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

    if (vkCreateDescriptorPool(vkrt->core.device, &createInfo, NULL, &vkrt->core.descriptorPool) != VK_SUCCESS) {
        LOG_ERROR("Failed to create descriptor pool");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    createInfo.poolSizeCount = (uint32_t)(sizeof(overlayPoolSizes) / sizeof(overlayPoolSizes[0]));
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
