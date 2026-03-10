#include "common.h"
#include "vkrt.h"

#include "IconsFontAwesome6.h"

#include <inttypes.h>
#include <stdio.h>

enum {
    kOverviewTimeTextCapacity = 32,
    kOverviewCountTextCapacity = 32,
    kOverviewDriverTextCapacity = 64,
};

static void drawStatusSummary(const VKRT_RenderStatusSnapshot* status, const VKRT_SceneSettingsSnapshot* settings) {
    if (!status || !settings) return;

    const char* mode = status->renderModeActive
        ? (status->renderModeFinished ? "Complete" : "Rendering")
        : "Preview";

    char renderTimeText[kOverviewTimeTextCapacity];
    char accumulationText[kOverviewTimeTextCapacity];
    char fpsText[kOverviewCountTextCapacity];
    char sppText[kOverviewCountTextCapacity];

    snprintf(renderTimeText, sizeof(renderTimeText), "%.1f ms", status->displayRenderTimeMs);
    snprintf(accumulationText, sizeof(accumulationText), "%" PRIu64 " samples", status->totalSamples);
    snprintf(fpsText, sizeof(fpsText), "%u", status->framesPerSecond);
    snprintf(sppText, sizeof(sppText), "%u", settings->samplesPerPixel);

    if (inspectorBeginKeyValueTable("##monitor_status")) {
        inspectorKeyValueRow("Mode", mode);
        inspectorKeyValueRow("FPS", fpsText);
        inspectorKeyValueRow("Render Time", renderTimeText);
        inspectorKeyValueRow("SPP", sppText);
        inspectorKeyValueRow("Accumulation", accumulationText);
        inspectorEndKeyValueTable();
    }
}

static void drawSystemSummary(const VKRT_RuntimeSnapshot* runtime, const VKRT_SystemInfo* system) {
    if (!runtime || !system) return;

    char driverText[kOverviewDriverTextCapacity];
    char viewportText[kOverviewTimeTextCapacity];
    formatDriverVersionText(system->vendorID, system->driverVersion, driverText, sizeof(driverText));
    snprintf(viewportText, sizeof(viewportText), "%ux%u", runtime->displayViewportRect[2], runtime->displayViewportRect[3]);

    if (inspectorBeginKeyValueTable("##monitor_system")) {
        inspectorKeyValueRow("GPU", system->deviceName);
        inspectorKeyValueRow("Driver", driverText);
        inspectorKeyValueRow("Viewport", viewportText);
        inspectorEndKeyValueTable();
    }
}

void inspectorDrawSceneOverviewSection(VKRT* vkrt) {
    if (!vkrt) return;

    VKRT_SceneSettingsSnapshot settings = {0};
    VKRT_RenderStatusSnapshot status = {0};
    VKRT_RuntimeSnapshot runtime = {0};
    VKRT_SystemInfo system = {0};
    if (VKRT_getSceneSettings(vkrt, &settings) != VKRT_SUCCESS ||
        VKRT_getRenderStatus(vkrt, &status) != VKRT_SUCCESS ||
        VKRT_getRuntimeSnapshot(vkrt, &runtime) != VKRT_SUCCESS ||
        VKRT_getSystemInfo(vkrt, &system) != VKRT_SUCCESS) {
        return;
    }

    ImGui_Dummy((ImVec2){0.0f, kInspectorControlSpacing});
    ImGui_SeparatorText(ICON_FA_GAUGE " Status");
    inspectorIndentSection();
    drawStatusSummary(&status, &settings);
    inspectorUnindentSection();

    ImGui_Dummy((ImVec2){0.0f, kInspectorControlSpacing});
    ImGui_SeparatorText(ICON_FA_MICROCHIP " System");
    inspectorIndentSection();
    drawSystemSummary(&runtime, &system);
    inspectorUnindentSection();
}
