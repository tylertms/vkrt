#include "common.h"
#include "debug.h"
#include "vkrt.h"

#include "IconsFontAwesome6.h"

#include <math.h>

static const float kCameraPoseDragSpeed = 0.01f;
static const float kCameraPoseDegenerateThreshold = 1e-8f;
static const float kCameraPoseNudgeDistance = 0.001f;
static const float kCameraFovMinDeg = 10.0f;
static const float kCameraFovMaxDeg = 140.0f;
static const float kFogDensityDragSpeed = 0.0005f;
static const float kFogDensityMax = 4.0f;

void inspectorDrawCameraTab(VKRT* vkrt) {
    if (!vkrt) return;

    VKRT_SceneSettingsSnapshot settings = {0};
    VKRT_RenderStatusSnapshot status = {0};
    VKRT_RuntimeSnapshot runtime = {0};
    if (VKRT_getSceneSettings(vkrt, &settings) != VKRT_SUCCESS ||
        VKRT_getRenderStatus(vkrt, &status) != VKRT_SUCCESS ||
        VKRT_getRuntimeSnapshot(vkrt, &runtime) != VKRT_SUCCESS) {
        return;
    }

    bool renderModeActive = status.renderModeActive != 0;

    if (ImGui_CollapsingHeader("Camera Pose", ImGuiTreeNodeFlags_DefaultOpen)) {
        inspectorIndentSection();
        ImGui_BeginDisabled(renderModeActive);

        vec3 position = {0.0f, 0.0f, 0.0f};
        vec3 target = {0.0f, 0.0f, 0.0f};
        vec3 up = {0.0f, 0.0f, 1.0f};
        float fov = 40.0f;
        VKRT_Result poseResult = VKRT_cameraGetPose(vkrt, position, target, up, &fov);
        if (poseResult != VKRT_SUCCESS) {
            LOG_ERROR("Querying camera pose failed (%d)", (int)poseResult);
        } else {
            bool changed = false;
            changed |= ImGui_DragFloat3Ex("Position", position, kCameraPoseDragSpeed, 0.0f, 0.0f, "%.3f", ImGuiSliderFlags_None);
            changed |= ImGui_DragFloat3Ex("Target", target, kCameraPoseDragSpeed, 0.0f, 0.0f, "%.3f", ImGuiSliderFlags_None);
            bool fovChanged = ImGui_SliderFloatEx("FOV", &fov, kCameraFovMinDeg, kCameraFovMaxDeg, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);

            if (changed || fovChanged) {
                vec3 viewDir;
                glm_vec3_sub(target, position, viewDir);
                if (glm_vec3_norm2(viewDir) < kCameraPoseDegenerateThreshold) {
                    target[0] = position[0] + kCameraPoseNudgeDistance;
                    target[1] = position[1];
                    target[2] = position[2];
                }
                poseResult = VKRT_cameraSetPose(vkrt, position, target, up, fov);
                if (poseResult != VKRT_SUCCESS) {
                    LOG_ERROR("Updating camera pose failed (%d)", (int)poseResult);
                }
            }
        }

        ImGui_EndDisabled();
        inspectorUnindentSection();
    }

    if (ImGui_CollapsingHeader("Shading", ImGuiTreeNodeFlags_DefaultOpen)) {
        inspectorIndentSection();
        ImGui_BeginDisabled(renderModeActive);

        const char* toneMappingLabels[] = {"None", "ACES"};
        int toneMappingMode = (int)settings.toneMappingMode;
        if (ImGui_ComboCharEx("Tone Mapping", &toneMappingMode, toneMappingLabels, 2, 2)) {
            VKRT_Result result = VKRT_setToneMappingMode(vkrt, (VKRT_ToneMappingMode)toneMappingMode);
            if (result != VKRT_SUCCESS) {
                LOG_ERROR("Updating tone mapping mode failed (%d)", (int)result);
            }
        }

        float fogDensity = settings.fogDensity;
        if (ImGui_DragFloatEx("Fog Density", &fogDensity, kFogDensityDragSpeed, 0.0f, kFogDensityMax, "%.4f", ImGuiSliderFlags_AlwaysClamp)) {
            VKRT_Result result = VKRT_setFogDensity(vkrt, fogDensity);
            if (result != VKRT_SUCCESS) {
                LOG_ERROR("Updating fog density failed (%d)", (int)result);
            }
        }
        tooltipOnHover("Global homogeneous fog density.");

        ImGui_EndDisabled();

        ImGui_Spacing();
        if (inspectorPaddedButton(ICON_FA_ARROWS_ROTATE " Reset Accumulation")) {
            VKRT_Result result = VKRT_invalidateAccumulation(vkrt);
            if (result != VKRT_SUCCESS) {
                LOG_ERROR("Resetting accumulation failed (%d)", (int)result);
            }
        }

        inspectorUnindentSection();
    }

    if (ImGui_CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen)) {
        inspectorIndentSection();
        ImGui_BeginDisabled(renderModeActive);

        bool autoSPP = settings.autoSPPEnabled != 0;
        if (ImGui_Checkbox("Auto SPP", &autoSPP)) {
            VKRT_Result result = VKRT_setAutoSPPEnabled(vkrt, autoSPP);
            if (result != VKRT_SUCCESS) {
                LOG_ERROR("Updating auto SPP toggle failed (%d)", (int)result);
            }
        }

        if (autoSPP) {
            int targetFPS = (int)settings.autoSPPTargetFPS;
            if (ImGui_DragIntEx("Target FPS", &targetFPS, 0.5, 30, 360, "%d", ImGuiSliderFlags_AlwaysClamp)) {
                VKRT_Result result = VKRT_setAutoSPPTargetFPS(vkrt, (uint32_t)targetFPS);
                if (result != VKRT_SUCCESS) {
                    LOG_ERROR("Updating auto SPP target FPS failed (%d)", (int)result);
                }
            }
        } else {
            int spp = (int)settings.samplesPerPixel;
            if (ImGui_SliderIntEx("SPP", &spp, 1, 2048, "%d", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic)) {
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
    }

    if (renderModeActive) {
        ImGui_Spacing();
        ImGui_TextDisabled("Camera is locked while rendering.");
    }
}
