#include "controller.h"

#include "debug.h"

#include <stdlib.h>

static void applyRenderCommand(VKRT* vkrt, SessionRenderCommand command, const SessionRenderSettings* settings) {
    if (!vkrt || command == SESSION_RENDER_COMMAND_NONE) return;

    if (command == SESSION_RENDER_COMMAND_START) {
        VKRT_Result result = VKRT_startRender(vkrt, settings->width, settings->height, settings->targetSamples);
        if (result != VKRT_SUCCESS) {
            LOG_ERROR("Starting render command failed (%d)", (int)result);
        }
        return;
    }

    if (command == SESSION_RENDER_COMMAND_STOP) {
        VKRT_RenderStatusSnapshot status = {0};
        if (VKRT_getRenderStatus(vkrt, &status) != VKRT_SUCCESS || !status.renderModeActive) {
            return;
        }

        VKRT_Result result = VKRT_stopRender(vkrt);
        if (result != VKRT_SUCCESS) {
            LOG_ERROR("Stopping render mode failed (%d)", (int)result);
        }
        return;
    }

    if (command == SESSION_RENDER_COMMAND_RESET_ACCUMULATION) {
        VKRT_Result result = VKRT_invalidateAccumulation(vkrt);
        if (result != VKRT_SUCCESS) {
            LOG_ERROR("Resetting accumulation failed (%d)", (int)result);
        }
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
    if (sessionTakeRenderSave(session, &savePath)) {
        if (VKRT_saveRenderImage(vkrt, savePath) != VKRT_SUCCESS) {
            LOG_ERROR("Saving render image failed. Path: %s", savePath);
        }
        free(savePath);
    }
}
