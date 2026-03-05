#include "common.h"
#include "debug.h"

static void drawPerformanceSection(VKRT* vkrt, const VKRT_PublicState* state, bool controlsDisabled) {
    if (!vkrt || !state) return;

    if (ImGui_CollapsingHeader("Performance Stats", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui_Indent();
        ImGui_Text("FPS:          %8u", state->framesPerSecond);
        ImGui_Text("Frames:       %8u", state->accumulationFrame);
        ImGui_Text("Samples:  %12llu", (unsigned long long)state->totalSamples);
        ImGui_Text("Samples / px: %8u", state->samplesPerPixel);
        ImGui_Text("Frame (ms):   %8.3f ms", state->displayFrameTimeMs);
        ImGui_Text("Render (ms):  %8.3f ms", state->displayRenderTimeMs);
        ImGui_Dummy((ImVec2){0.0f, kInspectorControlSpacing});
        ImGui_Unindent();
    }

    if (!ImGui_CollapsingHeader("Sampling", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui_Indent();
    if (controlsDisabled) ImGui_BeginDisabled(true);

    bool autoSPP = state->autoSPPEnabled != 0;
    if (ImGui_Checkbox("Auto SPP", &autoSPP)) {
        VKRT_Result result = VKRT_setAutoSPPEnabled(vkrt, autoSPP ? 1 : 0);
        if (result != VKRT_SUCCESS) {
            LOG_ERROR("Updating auto SPP toggle failed (%d)", (int)result);
        }
    }

    if (autoSPP) {
        int targetFPS = (int)state->autoSPPTargetFPS;
        if (ImGui_SliderIntEx("Target FPS", &targetFPS, 30, 360, "%d", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic)) {
            VKRT_Result result = VKRT_setAutoSPPTargetFPS(vkrt, (uint32_t)targetFPS);
            if (result != VKRT_SUCCESS) {
                LOG_ERROR("Updating auto SPP target FPS failed (%d)", (int)result);
            }
        }
    } else {
        int spp = (int)state->samplesPerPixel;
        if (ImGui_SliderIntEx("SPP", &spp, 1, 2048, "%d", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic)) {
            VKRT_Result result = VKRT_setSamplesPerPixel(vkrt, (uint32_t)spp);
            if (result != VKRT_SUCCESS) {
                LOG_ERROR("Updating samples per pixel failed (%d)", (int)result);
            }
        }
    }

    if (controlsDisabled) ImGui_EndDisabled();
    ImGui_Dummy((ImVec2){0.0f, kInspectorControlSpacing});
    ImGui_Unindent();
}

static void drawDebugSection(VKRT* vkrt, const VKRT_PublicState* state, bool controlsDisabled) {
    if (!vkrt || !state) return;
    if (!ImGui_CollapsingHeader("Debug", ImGuiTreeNodeFlags_None)) return;

    ImGui_Indent();
    if (controlsDisabled) ImGui_BeginDisabled(true);

    const char* debugModeLabels[] = {"None", "Normals", "Depth", "Bounce Count", "NEE Only", "BSDF Only"};
    int debugMode = (int)state->debugMode;
    if (ImGui_ComboCharEx("Debug Mode", &debugMode, debugModeLabels, 6, 6)) {
        VKRT_Result result = VKRT_setDebugMode(vkrt, (uint32_t)debugMode);
        if (result != VKRT_SUCCESS) {
            LOG_ERROR("Updating debug mode failed (%d)", (int)result);
        }
    }

    int rrMinDepth = (int)state->rrMinDepth;
    int rrMaxDepth = (int)state->rrMaxDepth;
    bool depthChanged = false;
    depthChanged |= ImGui_SliderIntEx("RR Min Depth", &rrMinDepth, 0, 64, "%d", ImGuiSliderFlags_AlwaysClamp);
    depthChanged |= ImGui_SliderIntEx("RR Max Depth", &rrMaxDepth, 1, 64, "%d", ImGuiSliderFlags_AlwaysClamp);
    if (depthChanged) {
        VKRT_Result result = VKRT_setPathDepth(vkrt, (uint32_t)rrMinDepth, (uint32_t)rrMaxDepth);
        if (result != VKRT_SUCCESS) {
            LOG_ERROR("Updating path depth failed (%d)", (int)result);
        }
    }
    tooltipOnHover("Russian roulette starts at RR Min Depth. Set RR Min Depth == RR Max Depth to disable RR.");

    bool misNeeEnabled = state->misNeeEnabled != 0u;
    if (ImGui_Checkbox("MIS + NEE", &misNeeEnabled)) {
        VKRT_Result result = VKRT_setMISNEEEnabled(vkrt, misNeeEnabled ? 1u : 0u);
        if (result != VKRT_SUCCESS) {
            LOG_ERROR("Updating MIS/NEE toggle failed (%d)", (int)result);
        }
    }
    tooltipOnHover("Enable Multiple Importance Sampling and Next Event Estimation.");

    if (controlsDisabled) ImGui_EndDisabled();
    ImGui_Dummy((ImVec2){0.0f, kInspectorControlSpacing});
    ImGui_Unindent();
}

static void drawSystemSection(const EditorFrameData* frame) {
    if (!frame) return;

    if (ImGui_CollapsingHeader("System", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui_Indent();
        ImGui_TextWrapped("Device: %s", frame->system.deviceName);
        char driverText[64] = {0};
        formatDriverVersionText(frame->system.vendorID, frame->system.driverVersion, driverText, sizeof(driverText));
        ImGui_Text("Driver: %s", driverText);
        ImGui_Text("Viewport: %ux%u", frame->runtime.displayViewportRect[2], frame->runtime.displayViewportRect[3]);
        ImGui_Dummy((ImVec2){0.0f, kInspectorControlSpacing});
        ImGui_Unindent();
    }
}

static void drawDisplaySection(VKRT* vkrt, const EditorFrameData* frame, bool renderModeActive) {
    if (!vkrt || !frame) return;

    if (!ImGui_CollapsingHeader("Display", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui_Indent();
    ImGui_BeginDisabled(renderModeActive);
    bool vsync = (renderModeActive ? frame->runtime.savedVsync : frame->runtime.vsync) != 0;
    if (ImGui_Checkbox("V-Sync", &vsync)) {
        VKRT_Result result = VKRT_setVSyncEnabled(vkrt, vsync ? 1u : 0u);
        if (result != VKRT_SUCCESS) {
            LOG_ERROR("Updating V-Sync failed (%d)", (int)result);
        }
    }
    ImGui_EndDisabled();
    ImGui_Dummy((ImVec2){0.0f, kInspectorControlSpacing});
    ImGui_Unindent();
}

void inspectorDrawConfigTab(VKRT* vkrt, const EditorFrameData* frame, bool renderModeActive) {
    if (!vkrt || !frame) return;

    drawSystemSection(frame);
    drawDisplaySection(vkrt, frame, renderModeActive);
    drawDebugSection(vkrt, &frame->state, renderModeActive);
    drawPerformanceSection(vkrt, &frame->state, renderModeActive);
}
