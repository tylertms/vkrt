#include "controller.h"

#include "debug.h"
#include "session.h"
#include "vkrt.h"
#include "vkrt_types.h"

#include <stdlib.h>

static int applyRenderDenoiseSetting(VKRT* vkrt, const SessionRenderSettings* settings) {
    if (!vkrt || !settings) return 0;

    VKRT_Result result = VKRT_setRenderDenoiseEnabled(vkrt, settings->denoiseEnabled);
    if (result != VKRT_SUCCESS) {
        LOG_ERROR("Updating render denoise setting failed (%d)", (int)result);
        return 0;
    }
    return 1;
}

static void startRenderCommand(VKRT* vkrt, const SessionRenderSettings* settings) {
    if (!applyRenderDenoiseSetting(vkrt, settings)) return;

    VKRT_Result result = VKRT_startRender(vkrt, settings->width, settings->height, settings->targetSamples);
    if (result != VKRT_SUCCESS) {
        LOG_ERROR("Starting render command failed (%d)", (int)result);
    }
}

static void stopRenderSamplingCommand(VKRT* vkrt, const SessionRenderSettings* settings) {
    if (!applyRenderDenoiseSetting(vkrt, settings)) return;

    VKRT_Result result = VKRT_stopRenderSampling(vkrt);
    if (result != VKRT_SUCCESS) {
        LOG_ERROR("Stopping render sampling failed (%d)", (int)result);
    }
}

static void denoiseRenderCommand(VKRT* vkrt, const SessionRenderSettings* settings) {
    if (!applyRenderDenoiseSetting(vkrt, settings)) return;

    VKRT_Result result = VKRT_denoiseRenderToViewport(vkrt);
    if (result != VKRT_SUCCESS) {
        LOG_ERROR("Starting viewport denoise failed (%d)", (int)result);
    }
}

static void stopRenderCommand(VKRT* vkrt) {
    if (!vkrt) return;

    VKRT_RenderStatusSnapshot status = {0};
    if (VKRT_getRenderStatus(vkrt, &status) != VKRT_SUCCESS || !VKRT_renderStatusIsActive(&status)) {
        return;
    }
    if (!VKRT_renderStatusIsComplete(&status)) {
        return;
    }

    VKRT_Result result = VKRT_stopRender(vkrt);
    if (result != VKRT_SUCCESS) {
        LOG_ERROR("Stopping render mode failed (%d)", (int)result);
    }
}

static void resetRenderAccumulation(VKRT* vkrt) {
    if (!vkrt) return;

    VKRT_Result result = VKRT_invalidateAccumulation(vkrt);
    if (result != VKRT_SUCCESS) {
        LOG_ERROR("Resetting accumulation failed (%d)", (int)result);
    }
}

static void applyRenderCommand(VKRT* vkrt, SessionRenderCommand command, const SessionRenderSettings* settings) {
    if (!vkrt || command == SESSION_RENDER_COMMAND_NONE) return;

    if (command == SESSION_RENDER_COMMAND_START) {
        startRenderCommand(vkrt, settings);
        return;
    }

    if (command == SESSION_RENDER_COMMAND_SET_DENOISE) {
        (void)applyRenderDenoiseSetting(vkrt, settings);
        return;
    }

    if (command == SESSION_RENDER_COMMAND_STOP_SAMPLING) {
        stopRenderSamplingCommand(vkrt, settings);
        return;
    }

    if (command == SESSION_RENDER_COMMAND_DENOISE) {
        denoiseRenderCommand(vkrt, settings);
        return;
    }

    if (command == SESSION_RENDER_COMMAND_STOP) {
        stopRenderCommand(vkrt);
        return;
    }

    if (command == SESSION_RENDER_COMMAND_RESET_ACCUMULATION) {
        resetRenderAccumulation(vkrt);
    }
}

void renderControllerApplySessionActions(VKRT* vkrt, Session* session) {
    if (!vkrt || !session) return;

    SessionRenderCommand command = SESSION_RENDER_COMMAND_NONE;
    SessionRenderSettings settings = {0};
    if (sessionTakeRenderCommand(session, &command, &settings)) {
        applyRenderCommand(vkrt, command, &settings);
    }

    char* savePath = NULL;
    VKRT_RenderExportSettings exportSettings = {0};
    if (sessionTakeRenderSave(session, &savePath, &exportSettings)) {
        if (VKRT_saveRenderImageEx(vkrt, savePath, &exportSettings) != VKRT_SUCCESS) {
            LOG_ERROR("Saving render image failed. Path: %s", savePath);
        }
        free(savePath);
    }
}
