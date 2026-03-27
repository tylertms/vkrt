#include "IconsFontAwesome6.h"
#include "common.h"
#include "sections.h"
#include "vkrt.h"
#include "vkrt_types.h"

#include <dcimgui.h>
#include <stdio.h>

enum {
    K_OVERVIEW_TIME_TEXT_CAPACITY = 32,
    K_OVERVIEW_COUNT_TEXT_CAPACITY = 32,
    K_OVERVIEW_DRIVER_TEXT_CAPACITY = 64,
};

static const ImVec2 kOverviewTableCellPadding = {4.0f, 2.0f};
static const float kOverviewFramePaddingY = 2.0f;

static bool beginCompactTable(const char* tableId) {
    if (!tableId) return false;

    ImVec2 framePadding = ImGui_GetStyle()->FramePadding;
    framePadding.y = kOverviewFramePaddingY;
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_FramePadding, framePadding);
    if (!inspectorBeginKeyValueTableWithCellPadding(tableId, kOverviewTableCellPadding)) {
        ImGui_PopStyleVar();
        return false;
    }
    return true;
}

static void endCompactTable(void) {
    inspectorEndKeyValueTable();
    ImGui_PopStyleVar();
}

static void drawStatusSummary(const VKRT_RenderStatusSnapshot* status, const VKRT_SceneSettingsSnapshot* settings) {
    if (!status || !settings) return;

    const char* mode = "Preview";
    if (VKRT_renderStatusIsActive(status)) {
        if (VKRT_renderStatusIsDenoising(status)) {
            mode = "Denoising";
        } else if (VKRT_renderStatusIsComplete(status)) {
            mode = "Complete";
        } else {
            mode = "Rendering";
        }
    }

    char renderTimeText[K_OVERVIEW_TIME_TEXT_CAPACITY];
    char accumulationText[K_OVERVIEW_TIME_TEXT_CAPACITY];
    char fpsText[K_OVERVIEW_COUNT_TEXT_CAPACITY];
    char sppText[K_OVERVIEW_COUNT_TEXT_CAPACITY];

    (void)snprintf(renderTimeText, sizeof(renderTimeText), "%.1f ms", status->displayRenderTimeMs);
    (void)
        snprintf(accumulationText, sizeof(accumulationText), "%llu samples", (unsigned long long)status->totalSamples);
    (void)snprintf(fpsText, sizeof(fpsText), "%u", status->framesPerSecond);
    (void)snprintf(sppText, sizeof(sppText), "%u", settings->samplesPerPixel);

    if (beginCompactTable("##monitor_status")) {
        inspectorKeyValueRow("Mode", mode);
        inspectorKeyValueRow("FPS", fpsText);
        inspectorKeyValueRow("Render Time", renderTimeText);
        inspectorKeyValueRow("SPP", sppText);
        inspectorKeyValueRow("Accumulation", accumulationText);
        endCompactTable();
    }
}

static void drawSystemSummary(const VKRT_RuntimeSnapshot* runtime, const VKRT_SystemInfo* system) {
    if (!runtime || !system) return;

    char driverText[K_OVERVIEW_DRIVER_TEXT_CAPACITY];
    char viewportText[K_OVERVIEW_TIME_TEXT_CAPACITY];
    formatDriverVersionText(system->vendorID, system->driverVersion, driverText, sizeof(driverText));
    (void)snprintf(
        viewportText,
        sizeof(viewportText),
        "%ux%u",
        runtime->displayViewportRect[2],
        runtime->displayViewportRect[3]
    );

    if (beginCompactTable("##monitor_system")) {
        inspectorKeyValueRow("GPU", system->deviceName);
        inspectorKeyValueRow("Driver", driverText);
        inspectorKeyValueRow("Viewport", viewportText);
        endCompactTable();
    }
}

void inspectorDrawSceneOverviewSection(VKRT* vkrt) {
    if (!vkrt) return;

    VKRT_SceneSettingsSnapshot settings = {0};
    VKRT_RenderStatusSnapshot status = {0};
    VKRT_RuntimeSnapshot runtime = {0};
    VKRT_SystemInfo system = {0};
    if (VKRT_getSceneSettings(vkrt, &settings) != VKRT_SUCCESS || VKRT_getRenderStatus(vkrt, &status) != VKRT_SUCCESS ||
        VKRT_getRuntimeSnapshot(vkrt, &runtime) != VKRT_SUCCESS || VKRT_getSystemInfo(vkrt, &system) != VKRT_SUCCESS) {
        return;
    }

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
