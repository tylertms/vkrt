#include "record.h"

#include "accel/accel.h"
#include "debug.h"
#include "scene.h"
#include "types.h"
#include "view.h"
#include "vkrt_engine_types.h"
#include "vkrt_internal.h"
#include "vkrt_types.h"
#include "vulkan/vulkan_core.h"

#include <stdint.h>
#include <stdlib.h>

static const VkClearColorValue kViewportClearColor = {.float32 = {0.02f, 0.02f, 0.025f, 1.0f}};

static void beginDebugLabel(
    VKRT* vkrt,
    VkCommandBuffer commandBuffer,
    const char* labelName,
    float r,
    float g,
    float b
) {
#if VKRT_PROFILING_ENABLED
    if (!vkrt || !labelName || !vkrt->core.procs.vkCmdBeginDebugUtilsLabelEXT) return;

    VkDebugUtilsLabelEXT label = {0};
    label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
    label.pLabelName = labelName;
    label.color[0] = r;
    label.color[1] = g;
    label.color[2] = b;
    label.color[3] = 1.0f;
    vkrt->core.procs.vkCmdBeginDebugUtilsLabelEXT(commandBuffer, &label);
#else
    (void)vkrt;
    (void)commandBuffer;
    (void)labelName;
    (void)r;
    (void)g;
    (void)b;
#endif
}

static void endDebugLabel(VKRT* vkrt, VkCommandBuffer commandBuffer) {
#if VKRT_PROFILING_ENABLED
    if (!vkrt || !vkrt->core.procs.vkCmdEndDebugUtilsLabelEXT) return;
    vkrt->core.procs.vkCmdEndDebugUtilsLabelEXT(commandBuffer);
#else
    (void)vkrt;
    (void)commandBuffer;
#endif
}

static void recordImageAccessBarrier(
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkAccessFlags2 srcAccessMask,
    VkAccessFlags2 dstAccessMask,
    VkPipelineStageFlags2 srcStageMask,
    VkPipelineStageFlags2 dstStageMask
) {
    VkImageMemoryBarrier2 barrier = {0};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = srcAccessMask;
    barrier.dstAccessMask = dstAccessMask;
    barrier.srcStageMask = srcStageMask;
    barrier.dstStageMask = dstStageMask;

    VkDependencyInfo dependencyInfo = {0};
    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependencyInfo.imageMemoryBarrierCount = 1;
    dependencyInfo.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
}

static void recordMemoryAccessBarrier(
    VkCommandBuffer commandBuffer,
    VkAccessFlags2 srcAccessMask,
    VkAccessFlags2 dstAccessMask,
    VkPipelineStageFlags2 srcStageMask,
    VkPipelineStageFlags2 dstStageMask
) {
    VkMemoryBarrier2 barrier = {0};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    barrier.srcAccessMask = srcAccessMask;
    barrier.dstAccessMask = dstAccessMask;
    barrier.srcStageMask = srcStageMask;
    barrier.dstStageMask = dstStageMask;

    VkDependencyInfo dependencyInfo = {0};
    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependencyInfo.memoryBarrierCount = 1;
    dependencyInfo.pMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
}

static void recordSceneUpdateCommands(VKRT* vkrt, VkCommandBuffer commandBuffer) {
    FrameSceneUpdate* update = vkrtCurrentFrameSceneUpdate(vkrt);
    VkBool32 hasTransferWrites = VK_FALSE;
    VkBool32 hasBLASBuilds = update->blasBuildCount > 0 ? VK_TRUE : VK_FALSE;
    VkBool32 hasTLASBuild = update->sceneTLASBuildPending || update->selectionTLASBuildPending;

    for (uint32_t i = 0; i < update->sceneTransferCount; i++) {
        PendingBufferCopy* transfer = &update->sceneTransfers[i];
        VkBufferCopy copyRegion = {
            .size = transfer->size,
        };
        vkCmdCopyBuffer(commandBuffer, transfer->stagingBuffer, transfer->dstBuffer, 1, &copyRegion);
        hasTransferWrites = VK_TRUE;
    }

    for (uint32_t i = 0; i < update->geometryUploadCount; i++) {
        PendingGeometryUpload* upload = &update->geometryUploads[i];
        Mesh* mesh = &vkrt->core.meshes[upload->meshIndex];
        VkBufferCopy copyRegions[2] = {
            {
                .srcOffset = 0,
                .dstOffset = (VkDeviceSize)mesh->info.vertexBase * sizeof(Vertex),
                .size = (VkDeviceSize)mesh->info.vertexCount * sizeof(Vertex),
            },
            {
                .srcOffset = upload->indexOffset,
                .dstOffset = (VkDeviceSize)mesh->info.indexBase * sizeof(uint32_t),
                .size = (VkDeviceSize)mesh->info.indexCount * sizeof(uint32_t),
            },
        };
        vkCmdCopyBuffer(commandBuffer, upload->stagingBuffer, vkrt->core.vertexData.buffer, 1, &copyRegions[0]);
        vkCmdCopyBuffer(commandBuffer, upload->stagingBuffer, vkrt->core.indexData.buffer, 1, &copyRegions[1]);
        hasTransferWrites = VK_TRUE;
    }

    if (hasTransferWrites) {
        recordMemoryAccessBarrier(
            commandBuffer,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_2_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR
        );
    }

    if (hasBLASBuilds) {
        recordBottomLevelAccelerationStructureBuilds(vkrt, commandBuffer);

        recordMemoryAccessBarrier(
            commandBuffer,
            VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
            VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR,
            VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR
        );
    }

    if (hasTLASBuild) {
        recordTopLevelAccelerationStructureBuilds(vkrt, commandBuffer);
    }

    if (hasTransferWrites || hasBLASBuilds || hasTLASBuild) {
        recordMemoryAccessBarrier(
            commandBuffer,
            VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
            VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR
        );
    }
}

typedef struct RecordCommandContext {
    VKRT* vkrt;
    VkCommandBuffer commandBuffer;
    uint32_t imageIndex;
    uint32_t qbase;
    VkBool32 presentToSwapchain;
    VkExtent2D swapchainExtent;
    VkExtent2D renderExtent;
    VkImage accumulationReadImage;
    VkImage accumulationWriteImage;
    VkImage albedoReadImage;
    VkImage albedoWriteImage;
    VkImage normalReadImage;
    VkImage normalWriteImage;
    VkImage outputImage;
    VkImage selectionMaskImage;
    VkImage destImage;
    VkImageSubresourceRange clearRange;
    VkBool32 renderModeActive;
    VkBool32 descriptorReady;
    VkBool32 shouldTrace;
    VkBool32 shouldSelectionTrace;
    VkBool32 shouldSelectionPost;
} RecordCommandContext;

typedef struct ViewportRect {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} ViewportRect;

static void initializeRecordCommandContext(
    RecordCommandContext* context,
    VKRT* vkrt,
    uint32_t imageIndex,
    VkBool32 presentToSwapchain
) {
    if (!context || !vkrt) return;

    *context = (RecordCommandContext){0};
    context->vkrt = vkrt;
    context->commandBuffer = vkrt->runtime.commandBuffers[vkrt->runtime.currentFrame];
    context->imageIndex = imageIndex;
    context->qbase = vkrt->runtime.currentFrame * 2u;
    context->presentToSwapchain = presentToSwapchain;
    context->swapchainExtent = vkrt->runtime.swapChainExtent;
    context->renderExtent = vkrt->runtime.renderExtent;
    if (context->renderExtent.width == 0 || context->renderExtent.height == 0) {
        context->renderExtent = context->swapchainExtent;
    }

    context->accumulationReadImage = vkrt->core.accumulationImages[vkrt->core.accumulationReadIndex];
    context->accumulationWriteImage = vkrt->core.accumulationImages[vkrt->core.accumulationWriteIndex];
    context->albedoReadImage = vkrt->core.albedoImages[vkrt->core.accumulationReadIndex];
    context->albedoWriteImage = vkrt->core.albedoImages[vkrt->core.accumulationWriteIndex];
    context->normalReadImage = vkrt->core.normalImages[vkrt->core.accumulationReadIndex];
    context->normalWriteImage = vkrt->core.normalImages[vkrt->core.accumulationWriteIndex];
    context->outputImage = vkrt->core.outputImage;
    context->selectionMaskImage = vkrt->core.selectionMaskImage;
    context->destImage = presentToSwapchain ? vkrt->runtime.swapChainImages[imageIndex] : VK_NULL_HANDLE;
    context->clearRange = (VkImageSubresourceRange){
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };

    context->renderModeActive = VKRT_renderPhaseIsActive(vkrt->renderStatus.renderPhase);
    context->descriptorReady = vkrt->core.descriptorSetReady[vkrt->runtime.currentFrame];
    context->shouldTrace =
        context->descriptorReady && !VKRT_renderPhaseSamplingFinished(vkrt->renderStatus.renderPhase);

    VkBool32 selectionOverlayEnabled = !context->renderModeActive && vkrt->sceneSettings.selectionEnabled != 0u;
    context->shouldSelectionTrace = context->shouldTrace && selectionOverlayEnabled &&
                                    vkrt->core.selectionTopLevelAccelerationStructure.structure != VK_NULL_HANDLE &&
                                    vkrt->core.selectionMaskDirty &&
                                    vkrt->core.selectionRayTracingPipeline != VK_NULL_HANDLE;
    context->shouldSelectionPost =
        context->shouldTrace && selectionOverlayEnabled && vkrt->core.computePipeline != VK_NULL_HANDLE;
}

static VKRT_Result beginRecordCommandContext(RecordCommandContext* context) {
    if (!context || !context->vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    VkCommandBufferBeginInfo commandBufferBeginInfo = {0};
    commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(context->commandBuffer, &commandBufferBeginInfo) != VK_SUCCESS) {
        LOG_ERROR("Failed to begin command buffer");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    vkCmdResetQueryPool(context->commandBuffer, context->vkrt->runtime.timestampPool, context->qbase, 2);
    vkCmdWriteTimestamp(
        context->commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        context->vkrt->runtime.timestampPool,
        context->qbase
    );

    beginDebugLabel(context->vkrt, context->commandBuffer, "Scene Update", 0.23f, 0.54f, 0.91f);
    recordSceneUpdateCommands(context->vkrt, context->commandBuffer);
    endDebugLabel(context->vkrt, context->commandBuffer);

    if (context->presentToSwapchain) {
        transitionImageLayout(
            context->commandBuffer,
            context->destImage,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
        );
    }

    context->vkrt->runtime.frameTraced = VK_FALSE;
    context->vkrt->runtime.frameSelectionTraced = VK_FALSE;
    return VKRT_SUCCESS;
}

static void resetAccumulationImages(const RecordCommandContext* context) {
    if (!context || !context->vkrt) return;

    VkClearColorValue clearZero = {.float32 = {0.0f, 0.0f, 0.0f, 0.0f}};
    transitionImageLayout(
        context->commandBuffer,
        context->accumulationReadImage,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
    );
    transitionImageLayout(
        context->commandBuffer,
        context->accumulationWriteImage,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
    );
    transitionImageLayout(
        context->commandBuffer,
        context->albedoReadImage,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
    );
    transitionImageLayout(
        context->commandBuffer,
        context->albedoWriteImage,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
    );
    transitionImageLayout(
        context->commandBuffer,
        context->normalReadImage,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
    );
    transitionImageLayout(
        context->commandBuffer,
        context->normalWriteImage,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
    );
    transitionImageLayout(
        context->commandBuffer,
        context->outputImage,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
    );

    vkCmdClearColorImage(
        context->commandBuffer,
        context->accumulationReadImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        &clearZero,
        1,
        &context->clearRange
    );
    vkCmdClearColorImage(
        context->commandBuffer,
        context->accumulationWriteImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        &clearZero,
        1,
        &context->clearRange
    );
    vkCmdClearColorImage(
        context->commandBuffer,
        context->albedoReadImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        &clearZero,
        1,
        &context->clearRange
    );
    vkCmdClearColorImage(
        context->commandBuffer,
        context->albedoWriteImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        &clearZero,
        1,
        &context->clearRange
    );
    vkCmdClearColorImage(
        context->commandBuffer,
        context->normalReadImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        &clearZero,
        1,
        &context->clearRange
    );
    vkCmdClearColorImage(
        context->commandBuffer,
        context->normalWriteImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        &clearZero,
        1,
        &context->clearRange
    );
    vkCmdClearColorImage(
        context->commandBuffer,
        context->outputImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        &clearZero,
        1,
        &context->clearRange
    );

    transitionImageLayout(
        context->commandBuffer,
        context->accumulationReadImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_GENERAL
    );
    transitionImageLayout(
        context->commandBuffer,
        context->accumulationWriteImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_GENERAL
    );
    transitionImageLayout(
        context->commandBuffer,
        context->albedoReadImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_GENERAL
    );
    transitionImageLayout(
        context->commandBuffer,
        context->albedoWriteImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_GENERAL
    );
    transitionImageLayout(
        context->commandBuffer,
        context->normalReadImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_GENERAL
    );
    transitionImageLayout(
        context->commandBuffer,
        context->normalWriteImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_GENERAL
    );
    transitionImageLayout(
        context->commandBuffer,
        context->outputImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_GENERAL
    );
}

static void recordMainTracePass(const RecordCommandContext* context) {
    if (!context || !context->shouldTrace) return;

    beginDebugLabel(context->vkrt, context->commandBuffer, "Main TraceRays", 0.91f, 0.47f, 0.20f);
    vkCmdBindPipeline(
        context->commandBuffer,
        VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
        context->vkrt->core.rayTracingPipeline
    );
    vkCmdBindDescriptorSets(
        context->commandBuffer,
        VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
        context->vkrt->core.pipelineLayout,
        0,
        1,
        &context->vkrt->core.descriptorSets[context->vkrt->runtime.currentFrame],
        0,
        NULL
    );
    context->vkrt->core.procs.vkCmdTraceRaysKHR(
        context->commandBuffer,
        &context->vkrt->core.shaderBindingTables[0],
        &context->vkrt->core.shaderBindingTables[1],
        &context->vkrt->core.shaderBindingTables[2],
        &context->vkrt->core.shaderBindingTables[3],
        context->renderExtent.width,
        context->renderExtent.height,
        1
    );
    endDebugLabel(context->vkrt, context->commandBuffer);
    context->vkrt->runtime.frameTraced = VK_TRUE;
}

static void recordSelectionTracePass(const RecordCommandContext* context) {
    if (!context || !context->shouldSelectionTrace) return;

    beginDebugLabel(context->vkrt, context->commandBuffer, "Selection TraceRays", 0.20f, 0.80f, 0.33f);
    vkCmdBindPipeline(
        context->commandBuffer,
        VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
        context->vkrt->core.selectionRayTracingPipeline
    );
    vkCmdBindDescriptorSets(
        context->commandBuffer,
        VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
        context->vkrt->core.pipelineLayout,
        0,
        1,
        &context->vkrt->core.descriptorSets[context->vkrt->runtime.currentFrame],
        0,
        NULL
    );
    context->vkrt->core.procs.vkCmdTraceRaysKHR(
        context->commandBuffer,
        &context->vkrt->core.selectionShaderBindingTables[0],
        &context->vkrt->core.selectionShaderBindingTables[1],
        &context->vkrt->core.selectionShaderBindingTables[2],
        &context->vkrt->core.selectionShaderBindingTables[3],
        context->renderExtent.width,
        context->renderExtent.height,
        1
    );
    endDebugLabel(context->vkrt, context->commandBuffer);
    context->vkrt->runtime.frameSelectionTraced = VK_TRUE;
}

static void recordSelectionPostPass(const RecordCommandContext* context) {
    if (!context || !context->shouldSelectionPost) return;

    if (context->shouldSelectionTrace) {
        recordImageAccessBarrier(
            context->commandBuffer,
            context->selectionMaskImage,
            VK_ACCESS_2_SHADER_WRITE_BIT,
            VK_ACCESS_2_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
        );
    }
    recordImageAccessBarrier(
        context->commandBuffer,
        context->outputImage,
        VK_ACCESS_2_SHADER_WRITE_BIT,
        VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
    );

    beginDebugLabel(context->vkrt, context->commandBuffer, "Selection Post", 0.56f, 0.30f, 0.84f);
    vkCmdBindPipeline(context->commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, context->vkrt->core.computePipeline);
    vkCmdBindDescriptorSets(
        context->commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        context->vkrt->core.pipelineLayout,
        0,
        1,
        &context->vkrt->core.descriptorSets[context->vkrt->runtime.currentFrame],
        0,
        NULL
    );

    const uint32_t localSizeX = 16u;
    const uint32_t localSizeY = 16u;
    uint32_t groupCountX = (context->renderExtent.width + localSizeX - 1u) / localSizeX;
    uint32_t groupCountY = (context->renderExtent.height + localSizeY - 1u) / localSizeY;
    vkCmdDispatch(context->commandBuffer, groupCountX, groupCountY, 1);

    recordImageAccessBarrier(
        context->commandBuffer,
        context->outputImage,
        VK_ACCESS_2_SHADER_WRITE_BIT,
        VK_ACCESS_2_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT
    );
    endDebugLabel(context->vkrt, context->commandBuffer);
}

static void recordTracingPasses(RecordCommandContext* context) {
    if (!context || !context->descriptorReady) return;

    if (context->vkrt->core.accumulationNeedsReset) {
        context->vkrt->renderStatus.accumulationFrame = 0;
        context->vkrt->renderStatus.totalSamples = 0;
        resetAccumulationImages(context);
        context->vkrt->core.accumulationNeedsReset = VK_FALSE;
    }

    recordMainTracePass(context);
    recordSelectionTracePass(context);
    recordSelectionPostPass(context);

    if (context->shouldTrace) {
        recordAutoExposureReadback(
            context->vkrt,
            context->commandBuffer,
            context->accumulationWriteImage,
            context->renderExtent
        );
    }
}

static void initializePresentBlit(const RecordCommandContext* context, VkImageBlit* blit) {
    if (!context || !blit) return;

    *blit = (VkImageBlit){0};
    blit->srcSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit->srcOffsets[0] = (VkOffset3D){0, 0, 0};
    blit->srcOffsets[1] = (VkOffset3D){(int32_t)context->renderExtent.width, (int32_t)context->renderExtent.height, 1};
    blit->dstSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
}

static void configureAspectFitBlit(
    const ViewportRect* viewport,
    uint32_t srcWidth,
    uint32_t srcHeight,
    VkImageBlit* blit
) {
    if (!viewport || !blit || srcHeight == 0u || viewport->height == 0u) return;

    float srcAspect = (float)srcWidth / (float)srcHeight;
    float dstAspect = (float)viewport->width / (float)viewport->height;
    uint32_t fitWidth = viewport->width;
    uint32_t fitHeight = viewport->height;
    if (dstAspect > srcAspect) {
        fitWidth = (uint32_t)(((float)viewport->height * srcAspect) + 0.5f);
        if (fitWidth == 0u) fitWidth = 1u;
    } else {
        fitHeight = (uint32_t)(((float)viewport->width / srcAspect) + 0.5f);
        if (fitHeight == 0u) fitHeight = 1u;
    }

    uint32_t dstX = viewport->x + ((viewport->width - fitWidth) / 2u);
    uint32_t dstY = viewport->y + ((viewport->height - fitHeight) / 2u);
    blit->dstOffsets[0] = (VkOffset3D){(int32_t)dstX, (int32_t)dstY, 0};
    blit->dstOffsets[1] = (VkOffset3D){(int32_t)(dstX + fitWidth), (int32_t)(dstY + fitHeight), 1};
}

static void configureRenderModeBlit(const RecordCommandContext* context, VkImageBlit* blit) {
    if (!context || !blit) return;

    ViewportRect viewport = {
        .x = context->vkrt->runtime.displayViewportRect[0],
        .y = context->vkrt->runtime.displayViewportRect[1],
        .width = context->vkrt->runtime.displayViewportRect[2],
        .height = context->vkrt->runtime.displayViewportRect[3],
    };
    uint32_t srcWidth = context->renderExtent.width;
    uint32_t srcHeight = context->renderExtent.height;
    VkBool32 fillViewport = VK_FALSE;
    int32_t srcX = 0;
    int32_t srcY = 0;

    vkrtClampViewportRect(context->swapchainExtent, &viewport.x, &viewport.y, &viewport.width, &viewport.height);
    vkCmdClearColorImage(
        context->commandBuffer,
        context->destImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        &kViewportClearColor,
        1,
        &context->clearRange
    );

    vkrtQueryRenderViewCropExtent(
        context->renderExtent,
        (VkExtent2D){viewport.width, viewport.height},
        context->vkrt->renderControl.view.zoom,
        &srcWidth,
        &srcHeight,
        &fillViewport
    );

    int32_t maxSrcX = (int32_t)context->renderExtent.width - (int32_t)srcWidth;
    int32_t maxSrcY = (int32_t)context->renderExtent.height - (int32_t)srcHeight;
    float panX = context->vkrt->renderControl.view.panX;
    float panY = context->vkrt->renderControl.view.panY;
    vkrtClampRenderViewPanOffset(
        context->renderExtent,
        (VkExtent2D){viewport.width, viewport.height},
        context->vkrt->renderControl.view.zoom,
        &panX,
        &panY
    );

    if (maxSrcX > 0) srcX = (int32_t)(((float)maxSrcX * 0.5f) + panX + 0.5f);
    if (maxSrcY > 0) srcY = (int32_t)(((float)maxSrcY * 0.5f) + panY + 0.5f);
    if (srcX < 0) srcX = 0;
    if (srcY < 0) srcY = 0;
    if (srcX > maxSrcX) srcX = maxSrcX;
    if (srcY > maxSrcY) srcY = maxSrcY;

    blit->srcOffsets[0] = (VkOffset3D){srcX, srcY, 0};
    blit->srcOffsets[1] = (VkOffset3D){srcX + (int32_t)srcWidth, srcY + (int32_t)srcHeight, 1};
    if (fillViewport) {
        blit->dstOffsets[0] = (VkOffset3D){(int32_t)viewport.x, (int32_t)viewport.y, 0};
        blit->dstOffsets[1] =
            (VkOffset3D){(int32_t)(viewport.x + viewport.width), (int32_t)(viewport.y + viewport.height), 1};
        return;
    }

    configureAspectFitBlit(&viewport, srcWidth, srcHeight, blit);
}

static void recordPresentBlitPass(const RecordCommandContext* context) {
    if (!context || !context->presentToSwapchain) return;

    beginDebugLabel(context->vkrt, context->commandBuffer, "Present Blit", 0.95f, 0.78f, 0.16f);
    transitionImageLayout(
        context->commandBuffer,
        context->outputImage,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
    );

    VkImageBlit blit;
    initializePresentBlit(context, &blit);
    if (context->renderModeActive) {
        configureRenderModeBlit(context, &blit);
    } else {
        blit.dstOffsets[0] = (VkOffset3D){0, 0, 0};
        blit.dstOffsets[1] =
            (VkOffset3D){(int32_t)context->swapchainExtent.width, (int32_t)context->swapchainExtent.height, 1};
    }

    vkCmdBlitImage(
        context->commandBuffer,
        context->outputImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        context->destImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &blit,
        VK_FILTER_LINEAR
    );
    endDebugLabel(context->vkrt, context->commandBuffer);
}

static void recordOverlayPass(const RecordCommandContext* context) {
    if (!context || !context->presentToSwapchain) return;

    beginDebugLabel(context->vkrt, context->commandBuffer, "Overlay Pass", 0.75f, 0.75f, 0.75f);
    if (context->vkrt->appHooks.drawOverlay) {
        transitionImageLayout(
            context->commandBuffer,
            context->destImage,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        );

        VkRenderingAttachmentInfo colorAttachment = {0};
        colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment.imageView = context->vkrt->runtime.swapChainImageViews[context->imageIndex];
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingInfo renderingInfo = {0};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.renderArea.offset = (VkOffset2D){0, 0};
        renderingInfo.renderArea.extent = context->swapchainExtent;
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &colorAttachment;

        vkCmdBeginRendering(context->commandBuffer, &renderingInfo);
        context->vkrt->appHooks.drawOverlay(context->vkrt, context->commandBuffer, context->vkrt->appHooks.userData);
        vkCmdEndRendering(context->commandBuffer);
        transitionImageLayout(
            context->commandBuffer,
            context->destImage,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
        );
    } else {
        transitionImageLayout(
            context->commandBuffer,
            context->destImage,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
        );
    }
    endDebugLabel(context->vkrt, context->commandBuffer);

    if (context->descriptorReady) {
        transitionImageLayout(
            context->commandBuffer,
            context->outputImage,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_LAYOUT_GENERAL
        );
    }
}

static VKRT_Result endRecordCommandContext(const RecordCommandContext* context) {
    if (!context) return VKRT_ERROR_INVALID_ARGUMENT;

    vkCmdWriteTimestamp(
        context->commandBuffer,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        context->vkrt->runtime.timestampPool,
        context->qbase + 1u
    );
    if (vkEndCommandBuffer(context->commandBuffer) != VK_SUCCESS) {
        LOG_ERROR("Failed to end command buffer");
        return VKRT_ERROR_OPERATION_FAILED;
    }
    return VKRT_SUCCESS;
}

VKRT_Result recordCommandBuffer(VKRT* vkrt, uint32_t imageIndex, VkBool32 presentToSwapchain) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    if (presentToSwapchain && imageIndex >= vkrt->runtime.swapChainImageCount) return VKRT_ERROR_INVALID_ARGUMENT;
    RecordCommandContext context;
    initializeRecordCommandContext(&context, vkrt, imageIndex, presentToSwapchain);

    VKRT_Result result = beginRecordCommandContext(&context);
    if (result != VKRT_SUCCESS) return result;

    recordTracingPasses(&context);
    if (context.descriptorReady) {
        recordPresentBlitPass(&context);
    } else if (context.presentToSwapchain) {
        vkCmdClearColorImage(
            context.commandBuffer,
            context.destImage,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            &kViewportClearColor,
            1,
            &context.clearRange
        );
    }

    recordOverlayPass(&context);
    return endRecordCommandContext(&context);
}
