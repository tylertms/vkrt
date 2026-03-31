#include "constants.h"
#include "debug.h"
#include "export.h"
#include "internal.h"
#include "platform.h"
#include "state.h"
#include "vkrt.h"
#include "vkrt_engine_types.h"
#include "vkrt_types.h"
#include "vulkan/vulkan_core.h"

#include <stdint.h>
#include <stdlib.h>

static void readbackCurrentRenderFeatureBuffers(VKRT* vkrt, RenderImageExportJob* job, const char* label) {
    if (!vkrt || !job || !label || !label[0]) return;

    uint32_t accumulationIndex = vkrt->core.accumulationReadIndex;
    VkImage albedoImage = vkrt->core.albedoImages[accumulationIndex];
    VkImage normalImage = vkrt->core.normalImages[accumulationIndex];

    if (albedoImage != VK_NULL_HANDLE &&
        readbackImagePixels(vkrt, albedoImage, job->width, job->height, job->albedo.format, &job->albedo.pixels) != 0) {
        LOG_INFO(
            "Failed to read back albedo feature buffer for '%s'; denoising will fall back to "
            "beauty-only",
            label
        );
    }
    if (normalImage != VK_NULL_HANDLE &&
        readbackImagePixels(vkrt, normalImage, job->width, job->height, job->normal.format, &job->normal.pixels) != 0) {
        LOG_INFO(
            "Failed to read back normal feature buffer for '%s'; denoising will fall back to "
            "beauty-only",
            label
        );
    }
}

int denoiseCurrentRenderToViewport(VKRT* vkrt) {
    if (!vkrt) return -1;
    if (!VKRT_renderPhaseIsActive(vkrt->renderStatus.renderPhase) ||
        !VKRT_renderPhaseIsDenoising(vkrt->renderStatus.renderPhase)) {
        LOG_ERROR("Viewport denoise requires a completed render");
        return -1;
    }
    if (vkrt->sceneSettings.debugMode != VKRT_DEBUG_MODE_NONE) {
        LOG_INFO("Viewport denoise is unavailable while a debug view is active");
        return -1;
    }

    uint32_t width = vkrt->runtime.renderExtent.width;
    uint32_t height = vkrt->runtime.renderExtent.height;
    if (width == 0u || height == 0u) {
        LOG_ERROR("Viewport denoise requires a valid render extent");
        return -1;
    }
    if (vkrt->core.outputImage == VK_NULL_HANDLE) {
        LOG_ERROR("Viewport denoise requires an initialized output image");
        return -1;
    }

    VkImage beautyImage = vkrt->core.accumulationImages[vkrt->core.accumulationReadIndex];
    if (beautyImage == VK_NULL_HANDLE) {
        LOG_ERROR("Viewport denoise requires an initialized accumulation image");
        return -1;
    }

    if (vkrtWaitForAllInFlightFrames(vkrt) != VKRT_SUCCESS) {
        LOG_ERROR("Viewport denoise failed while waiting for in-flight frames");
        return -1;
    }

    VKRT_RenderExportSettings denoiseSettings = {
        .denoiseEnabled = 1u,
    };

    RenderImageExportJob* job =
        createRenderImageJob(vkrt, RENDER_IMAGE_JOB_TYPE_VIEWPORT_DENOISE, width, height, &denoiseSettings);
    if (!job) {
        LOG_ERROR("Failed to allocate viewport denoise job");
        return -1;
    }

    job->renderSequence = vkrt->renderControl.renderSequence;

    if (readbackImagePixels(vkrt, beautyImage, width, height, job->beauty.format, &job->beauty.pixels) != 0) {
        LOG_ERROR("Failed to read back beauty buffer for viewport denoise");
        freeRenderImageExportJob(job);
        return -1;
    }

    readbackCurrentRenderFeatureBuffers(vkrt, job, "viewport denoise");

    if (queueRenderImageJob(vkrt, job) != 0) {
        LOG_ERROR("Failed to queue viewport denoise");
        return -1;
    }

    return 0;
}

void processPendingViewportDenoise(VKRT* vkrt) {
    if (!vkrt || !vkrt->renderControl.viewportDenoisePending) {
        return;
    }

    vkrt->renderControl.viewportDenoisePending = 0u;
    if (denoiseCurrentRenderToViewport(vkrt) != 0) {
        vkrt->renderStatus.renderPhase = VKRT_RENDER_PHASE_COMPLETE_RAW;
    }
}

void syncCompletedViewportDenoise(VKRT* vkrt) {
    if (!vkrt) return;

    RenderImageExporter* exporter = &vkrt->renderImageExporter;
    if (!exporter->stateLockInitialized) return;

    uint16_t* displayPixels = NULL;
    size_t displayByteCount = 0u;
    uint32_t width = 0u;
    uint32_t height = 0u;
    uint64_t renderSequence = 0u;
    int ready = 0;
    int succeeded = 0;

    vkrtMutexLock(&exporter->stateLock);
    if (exporter->completedViewportReady) {
        displayPixels = (uint16_t*)exporter->completedViewportPixels;
        displayByteCount = exporter->completedViewportByteCount;
        width = exporter->completedViewportWidth;
        height = exporter->completedViewportHeight;
        renderSequence = exporter->completedViewportRenderSequence;
        ready = 1;
        succeeded = exporter->completedViewportSucceeded;

        exporter->completedViewportPixels = NULL;
        exporter->completedViewportByteCount = 0u;
        exporter->completedViewportWidth = 0u;
        exporter->completedViewportHeight = 0u;
        exporter->completedViewportRenderSequence = 0u;
        exporter->completedViewportReady = 0;
        exporter->completedViewportSucceeded = 0;
    }
    vkrtMutexUnlock(&exporter->stateLock);

    if (!ready) return;

    if (!VKRT_renderPhaseIsActive(vkrt->renderStatus.renderPhase) ||
        renderSequence != vkrt->renderControl.renderSequence) {
        free(displayPixels);
        return;
    }

    if (succeeded && displayPixels && vkrtWaitForAllInFlightFrames(vkrt) == VKRT_SUCCESS &&
        uploadImagePixels(vkrt, vkrt->core.outputImage, width, height, displayPixels, displayByteCount) == 0) {
        vkrt->renderStatus.renderPhase = VKRT_RENDER_PHASE_COMPLETE_DENOISED;
        LOG_INFO("Finished denoising render");
    } else if (succeeded) {
        LOG_ERROR("Failed to upload denoised viewport image");
        vkrt->renderStatus.renderPhase = VKRT_RENDER_PHASE_COMPLETE_RAW;
    } else {
        LOG_INFO("Viewport denoise failed; keeping the raw render result");
        vkrt->renderStatus.renderPhase = VKRT_RENDER_PHASE_COMPLETE_RAW;
    }
    free(displayPixels);
}

int saveCurrentRenderImageEx(VKRT* vkrt, const char* path, const VKRT_RenderExportSettings* settings) {
    if (!vkrt || !path || !path[0]) return -1;

    VKRT_RenderExportSettings exportSettings = {0};
    VKRT_defaultRenderExportSettings(&exportSettings);
    if (settings) exportSettings = *settings;
    exportSettings.denoiseEnabled = exportSettings.denoiseEnabled ? 1u : 0u;

    char* resolvedPath = NULL;
    RenderImageFormat requestedFormat = RENDER_IMAGE_FORMAT_PNG;
    if (!resolveRenderImagePath(path, &resolvedPath, &requestedFormat)) {
        return -1;
    }

    VkImage beautyImage = vkrt->core.accumulationImages[vkrt->core.accumulationReadIndex];
    if (beautyImage == VK_NULL_HANDLE) {
        free(resolvedPath);
        LOG_ERROR("Cannot save render image before the accumulation image is initialized");
        return -1;
    }

    uint32_t width = vkrt->runtime.renderExtent.width;
    uint32_t height = vkrt->runtime.renderExtent.height;
    if (width == 0 || height == 0) {
        width = vkrt->runtime.swapChainExtent.width;
        height = vkrt->runtime.swapChainExtent.height;
    }
    if (width == 0 || height == 0) {
        free(resolvedPath);
        LOG_ERROR("Cannot save render image with invalid render size %ux%u", width, height);
        return -1;
    }

    RenderImageExportJob* job = createRenderImageJob(vkrt, RENDER_IMAGE_JOB_TYPE_SAVE, width, height, &exportSettings);
    if (!job) {
        free(resolvedPath);
        LOG_ERROR("Failed to allocate render image export job");
        return -1;
    }

    job->path = resolvedPath;
    job->format = requestedFormat;

    int canUseGpuDisplayExport =
        requestedFormat != RENDER_IMAGE_FORMAT_EXR && exportSettings.denoiseEnabled == 0u &&
        !VKRT_renderPhaseIsDenoised(vkrt->renderStatus.renderPhase) && vkrt->core.outputImage != VK_NULL_HANDLE;

    if (canUseGpuDisplayExport) {
        job->beauty.format = RENDER_IMAGE_BUFFER_FORMAT_RGBA16_UNORM;
        if (readbackImagePixels(vkrt, vkrt->core.outputImage, width, height, job->beauty.format, &job->beauty.pixels) != 0) {
            LOG_ERROR("Failed to read back display buffer for '%s'", resolvedPath);
            freeRenderImageExportJob(job);
            return -1;
        }
    } else if (readbackImagePixels(vkrt, beautyImage, width, height, job->beauty.format, &job->beauty.pixels) != 0) {
        LOG_ERROR("Failed to read back beauty buffer for '%s'", resolvedPath);
        freeRenderImageExportJob(job);
        return -1;
    }

    if (!canUseGpuDisplayExport && exportSettings.denoiseEnabled != 0u) {
        readbackCurrentRenderFeatureBuffers(vkrt, job, resolvedPath);
    }

    if (queueRenderImageJob(vkrt, job) != 0) {
        LOG_ERROR("Failed to queue render image export: %s", resolvedPath);
        return -1;
    }

    LOG_TRACE("Queued render image save: %s", resolvedPath);
    return 0;
}

int saveCurrentRenderImage(VKRT* vkrt, const char* path) {
    return saveCurrentRenderImageEx(vkrt, path, NULL);
}
