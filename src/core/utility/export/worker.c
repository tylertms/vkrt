#include "debug.h"
#include "export.h"
#include "internal.h"
#include "platform.h"
#include "vkrt_engine_types.h"
#include "vkrt_types.h"

#include <stdint.h>
#include <stdlib.h>

static const uint32_t kMaxPendingImageExportJobs = 2u;

static RenderImageExportJob* waitForNextRenderImageExportJob(RenderImageExporter* exporter) {
    if (!exporter) return NULL;

    vkrtMutexLock(&exporter->workerLock);
    while (!exporter->stop && exporter->head == NULL) {
        vkrtCondWait(&exporter->workerCondition, &exporter->workerLock);
    }

    if (exporter->stop && exporter->head == NULL) {
        vkrtMutexUnlock(&exporter->workerLock);
        return NULL;
    }

    RenderImageExportJob* job = exporter->head;
    exporter->head = job->next;
    if (exporter->head == NULL) {
        exporter->tail = NULL;
    }
    vkrtMutexUnlock(&exporter->workerLock);
    return job;
}

static void publishCompletedViewportJob(
    RenderImageExporter* exporter,
    const RenderImageExportJob* job,
    int result,
    uint16_t* displayPixels,
    size_t displayByteCount
) {
    if (!exporter || !job) return;

    vkrtMutexLock(&exporter->stateLock);
    free(exporter->completedViewportPixels);
    exporter->completedViewportPixels = displayPixels;
    exporter->completedViewportByteCount = displayByteCount;
    exporter->completedViewportWidth = job->width;
    exporter->completedViewportHeight = job->height;
    exporter->completedViewportRenderSequence = job->renderSequence;
    exporter->completedViewportSucceeded = result == 0;
    exporter->completedViewportReady = 1;
    vkrtMutexUnlock(&exporter->stateLock);
}

static void processRenderImageWorkerJob(RenderImageExporter* exporter, RenderImageExportJob* job) {
    if (!exporter || !job) return;

    if (job->type == RENDER_IMAGE_JOB_TYPE_SAVE) {
        int result = processRenderImageExportJob(job);
        if (result == 0) {
            LOG_INFO("Saved render image: %s", job->path);
        }
        return;
    }

    uint16_t* displayPixels = NULL;
    size_t displayByteCount = 0u;
    int result = processViewportDenoiseJob(job, &displayPixels, &displayByteCount);
    publishCompletedViewportJob(exporter, job, result, displayPixels, displayByteCount);
}

static void finishRenderImageWorkerJob(RenderImageExporter* exporter) {
    if (!exporter) return;

    vkrtMutexLock(&exporter->workerLock);
    if (exporter->pendingJobCount > 0u) {
        exporter->pendingJobCount--;
    }
    vkrtCondBroadcast(&exporter->workerCondition);
    vkrtMutexUnlock(&exporter->workerLock);
}

static int renderImageWorkerMain(void* userData) {
    RenderImageExporter* exporter = (RenderImageExporter*)userData;

    for (;;) {
        RenderImageExportJob* job = waitForNextRenderImageExportJob(exporter);
        if (!job) break;

        processRenderImageWorkerJob(exporter, job);
        freeRenderImageExportJob(job);
        finishRenderImageWorkerJob(exporter);
    }

    return 0;
}

static int initializeRenderImageWorkerPrimitivesLocked(RenderImageExporter* exporter) {
    if (exporter->primitivesInitialized) return 0;

    if (vkrtMutexInit(&exporter->workerLock, VKRT_MUTEX_PLAIN) != VKRT_THREAD_SUCCESS) {
        return -1;
    }
    if (vkrtCondInit(&exporter->workerCondition) != VKRT_THREAD_SUCCESS) {
        vkrtMutexDestroy(&exporter->workerLock);
        return -1;
    }
    exporter->primitivesInitialized = 1;
    return 0;
}

static int startRenderImageWorkerLocked(RenderImageExporter* exporter) {
    if (exporter->threadRunning) return 0;
    if (initializeRenderImageWorkerPrimitivesLocked(exporter) != 0) return -1;

    exporter->stop = 0;
    exporter->head = NULL;
    exporter->tail = NULL;
    exporter->pendingJobCount = 0u;
    exporter->completedViewportReady = 0;
    exporter->completedViewportSucceeded = 0;
    exporter->completedViewportByteCount = 0u;
    exporter->completedViewportWidth = 0u;
    exporter->completedViewportHeight = 0u;
    exporter->completedViewportRenderSequence = 0u;
    free(exporter->completedViewportPixels);
    exporter->completedViewportPixels = NULL;

    if (vkrtThreadCreate(&exporter->workerThread, renderImageWorkerMain, exporter) != VKRT_THREAD_SUCCESS) {
        vkrtCondDestroy(&exporter->workerCondition);
        vkrtMutexDestroy(&exporter->workerLock);
        exporter->primitivesInitialized = 0;
        return -1;
    }
    exporter->threadRunning = 1;
    return 0;
}

static int ensureRenderImageWorkerStarted(RenderImageExporter* exporter) {
    if (!exporter->stateLockInitialized) {
        if (vkrtMutexInit(&exporter->stateLock, VKRT_MUTEX_PLAIN) != VKRT_THREAD_SUCCESS) {
            LOG_ERROR("Failed to initialize render image exporter state lock");
            return -1;
        }
        exporter->stateLockInitialized = 1;
    }

    vkrtMutexLock(&exporter->stateLock);
    int result = startRenderImageWorkerLocked(exporter);
    vkrtMutexUnlock(&exporter->stateLock);

    if (result != 0) {
        LOG_ERROR("Failed to initialize render image exporter worker");
        return -1;
    }
    return 0;
}

int queueRenderImageJob(VKRT* vkrt, RenderImageExportJob* job) {
    if (!vkrt || !job || !job->beauty.pixels || job->width == 0u || job->height == 0u) {
        freeRenderImageExportJob(job);
        return -1;
    }

    RenderImageExporter* exporter = &vkrt->renderImageExporter;

    if (job->type == RENDER_IMAGE_JOB_TYPE_SAVE &&
        (job->path == NULL || job->format < RENDER_IMAGE_FORMAT_PNG || job->format > RENDER_IMAGE_FORMAT_EXR)) {
        freeRenderImageExportJob(job);
        return -1;
    }

    if (ensureRenderImageWorkerStarted(exporter) != 0) {
        freeRenderImageExportJob(job);
        return -1;
    }

    job->next = NULL;

    vkrtMutexLock(&exporter->workerLock);
    if (exporter->stop) {
        vkrtMutexUnlock(&exporter->workerLock);
        freeRenderImageExportJob(job);
        LOG_ERROR("Render image exporter is shutting down");
        return -1;
    }

    while (!exporter->stop && exporter->pendingJobCount >= kMaxPendingImageExportJobs) {
        vkrtCondWait(&exporter->workerCondition, &exporter->workerLock);
    }
    if (exporter->stop) {
        vkrtMutexUnlock(&exporter->workerLock);
        freeRenderImageExportJob(job);
        LOG_ERROR("Render image exporter is shutting down");
        return -1;
    }

    if (exporter->tail) {
        exporter->tail->next = job;
    } else {
        exporter->head = job;
    }
    exporter->tail = job;
    exporter->pendingJobCount++;
    vkrtCondSignal(&exporter->workerCondition);
    vkrtMutexUnlock(&exporter->workerLock);

    return 0;
}

void shutdownRenderImageExporter(VKRT* vkrt) {
    if (!vkrt) return;
    RenderImageExporter* exporter = &vkrt->renderImageExporter;

    if (!exporter->stateLockInitialized) return;

    vkrtMutexLock(&exporter->stateLock);
    if (!exporter->threadRunning) {
        vkrtMutexUnlock(&exporter->stateLock);
        return;
    }

    vkrtMutexLock(&exporter->workerLock);
    exporter->stop = 1;
    vkrtCondBroadcast(&exporter->workerCondition);
    vkrtMutexUnlock(&exporter->workerLock);

    VKRT_Thread workerThread = exporter->workerThread;
    exporter->threadRunning = 0;
    vkrtMutexUnlock(&exporter->stateLock);

    vkrtThreadJoin(workerThread, NULL);

    vkrtMutexLock(&exporter->workerLock);
    RenderImageExportJob* job = exporter->head;
    exporter->head = NULL;
    exporter->tail = NULL;
    exporter->pendingJobCount = 0u;
    exporter->stop = 0;
    vkrtMutexUnlock(&exporter->workerLock);

    while (job) {
        RenderImageExportJob* next = job->next;
        freeRenderImageExportJob(job);
        job = next;
    }

    free(exporter->completedViewportPixels);
    exporter->completedViewportPixels = NULL;
    exporter->completedViewportByteCount = 0u;
    exporter->completedViewportWidth = 0u;
    exporter->completedViewportHeight = 0u;
    exporter->completedViewportRenderSequence = 0u;
    exporter->completedViewportReady = 0;
    exporter->completedViewportSucceeded = 0;

    if (exporter->primitivesInitialized) {
        vkrtCondDestroy(&exporter->workerCondition);
        vkrtMutexDestroy(&exporter->workerLock);
        exporter->primitivesInitialized = 0;
    }

    vkrtMutexDestroy(&exporter->stateLock);
    exporter->stateLockInitialized = 0;
}
