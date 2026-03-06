#include "common.h"
#include "debug.h"

#include "IconsFontAwesome6.h"

#include <math.h>

static void drawCameraEffectsSection(VKRT* vkrt, const VKRT_PublicState* state, bool controlsDisabled) {
    if (!vkrt || !state) return;
    if (!ImGui_CollapsingHeader("Shading & Effects", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui_Indent();
    if (controlsDisabled) ImGui_BeginDisabled(true);

    float fogDensity = state->fogDensity;
    if (ImGui_DragFloatEx("Fog Density", &fogDensity, 0.0005f, 0.0f, 4.0f, "%.4f", ImGuiSliderFlags_AlwaysClamp)) {
        VKRT_Result result = VKRT_setFogDensity(vkrt, fogDensity);
        if (result != VKRT_SUCCESS) {
            LOG_ERROR("Updating fog density failed (%d)", (int)result);
        }
    }
    tooltipOnHover("Global homogeneous fog density.");

    const char* toneMappingLabels[] = {"None", "ACES"};
    int toneMappingMode = (int)state->toneMappingMode;
    if (ImGui_ComboCharEx("Tone Mapping", &toneMappingMode, toneMappingLabels, 2, 2)) {
        VKRT_Result result = VKRT_setToneMappingMode(vkrt, (VKRT_ToneMappingMode)toneMappingMode);
        if (result != VKRT_SUCCESS) {
            LOG_ERROR("Updating tone mapping mode failed (%d)", (int)result);
        }
    }

    if (ImGui_Button(ICON_FA_ARROWS_ROTATE " Reset accumulation")) {
        VKRT_Result result = VKRT_invalidateAccumulation(vkrt);
        if (result != VKRT_SUCCESS) {
            LOG_ERROR("Resetting accumulation failed (%d)", (int)result);
        }
    }

    if (controlsDisabled) ImGui_EndDisabled();
    ImGui_Dummy((ImVec2){0.0f, kInspectorControlSpacing});
    ImGui_Unindent();
}

static void drawCameraSection(VKRT* vkrt, bool renderModeActive) {
    if (!ImGui_CollapsingHeader("Camera Pose", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui_Indent();
    ImGui_BeginDisabled(renderModeActive);

    vec3 position = {0.0f, 0.0f, 0.0f};
    vec3 target = {0.0f, 0.0f, 0.0f};
    vec3 up = {0.0f, 0.0f, 1.0f};
    float fov = 40.0f;
    VKRT_Result poseResult = VKRT_cameraGetPose(vkrt, position, target, up, &fov);
    if (poseResult != VKRT_SUCCESS) {
        LOG_ERROR("Querying camera pose failed (%d)", (int)poseResult);
        ImGui_EndDisabled();
        ImGui_Dummy((ImVec2){0.0f, kInspectorControlSpacing});
        ImGui_Unindent();
        return;
    }

    bool changed = false;
    changed |= ImGui_DragFloat3Ex("Position", position, 0.01f, 0.0f, 0.0f, "%.3f", ImGuiSliderFlags_None);
    changed |= ImGui_DragFloat3Ex("Target", target, 0.01f, 0.0f, 0.0f, "%.3f", ImGuiSliderFlags_None);
    bool fovChanged = ImGui_SliderFloatEx("FOV", &fov, 10.0f, 140.0f, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);

    if (!changed && !fovChanged) {
        ImGui_EndDisabled();
        ImGui_Dummy((ImVec2){0.0f, kInspectorControlSpacing});
        ImGui_Unindent();
        return;
    }

    vec3 viewDir;
    glm_vec3_sub(target, position, viewDir);
    if (glm_vec3_norm2(viewDir) < 1e-8f) {
        target[0] = position[0] + 0.001f;
        target[1] = position[1];
        target[2] = position[2];
    }
    poseResult = VKRT_cameraSetPose(vkrt, position, target, up, fov);
    if (poseResult != VKRT_SUCCESS) {
        LOG_ERROR("Updating camera pose failed (%d)", (int)poseResult);
    }
    ImGui_EndDisabled();
    ImGui_Dummy((ImVec2){0.0f, kInspectorControlSpacing});
    ImGui_Unindent();
}

void inspectorDrawCameraTab(VKRT* vkrt) {
    if (!vkrt) return;

    const VKRT_PublicState* state = VKRT_getPublicState(vkrt);
    if (!state) return;

    bool renderModeActive = state->renderModeActive != 0;
    drawCameraSection(vkrt, renderModeActive);
    drawCameraEffectsSection(vkrt, state, renderModeActive);
}
