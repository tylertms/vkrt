#include "common.h"
#include "debug.h"

#include "IconsFontAwesome6.h"

#include <stdio.h>

static void drawStatusSummary(const VKRT_PublicState* state) {
    if (!state) return;

    const char* mode = (state->renderModeActive != 0)
        ? (state->renderModeFinished ? "Complete" : "Rendering")
        : "Preview";

    char renderTimeText[32] = {0};
    char accumulationText[32] = {0};
    char sppText[32] = {0};

    snprintf(renderTimeText, sizeof(renderTimeText), "%.1f ms", state->displayRenderTimeMs);
    snprintf(accumulationText, sizeof(accumulationText), "%llu samples", (unsigned long long)state->totalSamples);
    snprintf(sppText, sizeof(sppText), "%u", state->samplesPerPixel);

    if (inspectorBeginKeyValueTable("##monitor_status")) {
        inspectorKeyValueRow("Mode", mode);
        inspectorKeyValueRow("Render Time", renderTimeText);
        inspectorKeyValueRow("Accumulation", accumulationText);
        inspectorKeyValueRow("SPP", sppText);
        inspectorEndKeyValueTable();
    }
}

static void drawSystemSummary(const VKRT_RuntimeSnapshot* runtime, const VKRT_SystemInfo* system) {
    if (!runtime || !system) return;

    char driverText[64] = {0};
    char viewportText[32] = {0};
    formatDriverVersionText(system->vendorID, system->driverVersion, driverText, sizeof(driverText));
    snprintf(viewportText, sizeof(viewportText), "%ux%u", runtime->displayViewportRect[2], runtime->displayViewportRect[3]);

    if (inspectorBeginKeyValueTable("##monitor_system")) {
        inspectorKeyValueRow("GPU", system->deviceName);
        inspectorKeyValueRow("Driver", driverText);
        inspectorKeyValueRow("Viewport", viewportText);
        inspectorEndKeyValueTable();
    }
}

void inspectorDrawMonitoringPanel(VKRT* vkrt) {
    if (!vkrt) return;

    const VKRT_PublicState* state = VKRT_getPublicState(vkrt);
    VKRT_RuntimeSnapshot runtime = {0};
    VKRT_SystemInfo system = {0};
    if (!state ||
        VKRT_getRuntimeSnapshot(vkrt, &runtime) != VKRT_SUCCESS ||
        VKRT_getSystemInfo(vkrt, &system) != VKRT_SUCCESS) {
        return;
    }

    bool renderModeActive = state->renderModeActive != 0;

    inspectorTightSeparatorText(ICON_FA_GAUGE " Status");
    inspectorIndentSection();
    drawStatusSummary(state);

    ImGui_BeginDisabled(renderModeActive);
    bool autoSPP = state->autoSPPEnabled != 0;
    if (ImGui_Checkbox("Auto SPP", &autoSPP)) {
        VKRT_Result result = VKRT_setAutoSPPEnabled(vkrt, autoSPP ? 1 : 0);
        if (result != VKRT_SUCCESS) {
            LOG_ERROR("Updating auto SPP toggle failed (%d)", (int)result);
        }
    }
    if (autoSPP) {
        int targetFPS = (int)state->autoSPPTargetFPS;
        if (ImGui_SliderIntEx("Target FPS", &targetFPS, 30, 360, "%d",
                ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic)) {
            VKRT_Result result = VKRT_setAutoSPPTargetFPS(vkrt, (uint32_t)targetFPS);
            if (result != VKRT_SUCCESS) {
                LOG_ERROR("Updating auto SPP target FPS failed (%d)", (int)result);
            }
        }
    } else {
        int spp = (int)state->samplesPerPixel;
        if (ImGui_SliderIntEx("SPP", &spp, 1, 2048, "%d",
                ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic)) {
            VKRT_Result result = VKRT_setSamplesPerPixel(vkrt, (uint32_t)spp);
            if (result != VKRT_SUCCESS) {
                LOG_ERROR("Updating samples per pixel failed (%d)", (int)result);
            }
        }
    }

    bool vsync = (renderModeActive ? runtime.savedVsync : runtime.vsync) != 0;
    if (ImGui_Checkbox("V-Sync", &vsync)) {
        VKRT_Result result = VKRT_setVSyncEnabled(vkrt, vsync ? 1u : 0u);
        if (result != VKRT_SUCCESS) {
            LOG_ERROR("Updating V-Sync failed (%d)", (int)result);
        }
    }
    ImGui_EndDisabled();
    inspectorUnindentSection();

    ImGui_Dummy((ImVec2){0.0f, 4.0f});
    inspectorTightSeparatorText(ICON_FA_MICROCHIP " System");
    inspectorIndentSection();
    drawSystemSummary(&runtime, &system);
    inspectorUnindentSection();
}
