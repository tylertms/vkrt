#include "IconsFontAwesome6.h"
#include "common.h"
#include "constants.h"
#include "debug.h"
#include "sections.h"
#include "session.h"
#include "vkrt.h"
#include "vkrt_types.h"

#include <dcimgui.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <vec3.h>

static const float kCameraPoseDragSpeed = 0.01f;
static const float kCameraPoseDegenerateThreshold = 1e-8f;
static const float kCameraPoseNudgeDistance = 0.001f;
static const float kCameraFovMinDeg = 10.0f;
static const float kCameraFovMaxDeg = 140.0f;
static const float kExposureMin = 0.03125f;
static const float kExposureMax = 32.0f;
static const float kEnvironmentStrengthMax = 1000000.0f;
static const float kEnvironmentRotationMinDeg = -360.0f;
static const float kEnvironmentRotationMaxDeg = 360.0f;
static const int kPathDepthMin = 0;
static const int kPathDepthMax = 64;

static void formatEnvironmentTextureLabel(VKRT* vkrt, uint32_t textureIndex, char* out, size_t outSize) {
    if (!out || outSize == 0u) return;

    if (!vkrt || textureIndex == VKRT_INVALID_INDEX) {
        (void)snprintf(out, outSize, "None");
        return;
    }

    VKRT_TextureSnapshot texture = {0};
    if (VKRT_getTextureSnapshot(vkrt, textureIndex, &texture) == VKRT_SUCCESS) {
        (void)snprintf(out, outSize, "%s", texture.name[0] ? texture.name : "Texture");
        return;
    }

    (void)snprintf(out, outSize, "Texture #%u", textureIndex);
}

static void logCameraInspectorFailure(const char* message, VKRT_Result result) {
    if (result != VKRT_SUCCESS) {
        LOG_ERROR("%s (%d)", message, (int)result);
    }
}

static void drawCameraPoseSection(VKRT* vkrt, bool renderModeActive) {
    if (inspectorBeginCollapsingHeaderSection("Camera Pose", ImGuiTreeNodeFlags_DefaultOpen)) {
        inspectorIndentSection();
        ImGui_BeginDisabled(renderModeActive);

        float position[3] = {0.0f, 0.0f, 0.0f};
        float target[3] = {0.0f, 0.0f, 0.0f};
        float upVector[3] = {0.0f, 0.0f, 1.0f};
        float fov = 40.0f;
        VKRT_Result poseResult = VKRT_cameraGetPose(vkrt, position, target, upVector, &fov);
        if (poseResult != VKRT_SUCCESS) {
            logCameraInspectorFailure("Querying camera pose failed", poseResult);
        } else {
            bool changed = false;
            changed |= ImGui_DragFloat3Ex(
                "Position",
                position,
                kCameraPoseDragSpeed,
                0.0f,
                0.0f,
                "%.3f",
                ImGuiSliderFlags_None
            );
            changed |=
                ImGui_DragFloat3Ex("Target", target, kCameraPoseDragSpeed, 0.0f, 0.0f, "%.3f", ImGuiSliderFlags_None);
            bool fovChanged = ImGui_SliderFloatEx(
                "FOV",
                &fov,
                kCameraFovMinDeg,
                kCameraFovMaxDeg,
                "%.1f deg",
                ImGuiSliderFlags_AlwaysClamp
            );

            if (changed || fovChanged) {
                float viewDir[3];
                glm_vec3_sub(target, position, viewDir);
                if (glm_vec3_norm2(viewDir) < kCameraPoseDegenerateThreshold) {
                    target[0] = position[0] + kCameraPoseNudgeDistance;
                    target[1] = position[1];
                    target[2] = position[2];
                }
                logCameraInspectorFailure(
                    "Updating camera pose failed",
                    VKRT_cameraSetPose(vkrt, position, target, upVector, fov)
                );
            }
        }

        ImGui_EndDisabled();
        inspectorUnindentSection();
        inspectorEndCollapsingHeaderSection();
    }
}

static void drawCameraShadingSection(VKRT* vkrt, VKRT_SceneSettingsSnapshot* settings, bool renderModeActive) {
    if (inspectorBeginCollapsingHeaderSection("Shading", ImGuiTreeNodeFlags_DefaultOpen)) {
        inspectorIndentSection();
        ImGui_BeginDisabled(renderModeActive);

        const char* toneMappingLabels[] = {"None", "ACES"};
        int toneMappingMode = (int)settings->toneMappingMode;
        if (ImGui_ComboCharEx(
                "Tone Mapping",
                &toneMappingMode,
                toneMappingLabels,
                VKRT_TONE_MAPPING_MODE_COUNT,
                VKRT_TONE_MAPPING_MODE_COUNT
            )) {
            logCameraInspectorFailure(
                "Updating tone mapping mode failed",
                VKRT_setToneMappingMode(vkrt, (VKRT_ToneMappingMode)toneMappingMode)
            );
        }

        const char* renderModeLabels[] = {"RGB", "Spectral"};
        int renderMode = (int)settings->renderMode;
        if (ImGui_ComboCharEx(
                "Color Mode",
                &renderMode,
                renderModeLabels,
                VKRT_RENDER_MODE_COUNT,
                VKRT_RENDER_MODE_COUNT
            )) {
            VKRT_Result result = VKRT_setRenderMode(vkrt, (VKRT_RenderMode)renderMode);
            logCameraInspectorFailure("Updating color mode failed", result);
            if (result == VKRT_SUCCESS) {
                settings->renderMode = (VKRT_RenderMode)renderMode;
            }
        }

        bool autoExposureEnabled = settings->autoExposureEnabled != 0;
        if (ImGui_Checkbox("Auto Exposure", &autoExposureEnabled)) {
            uint8_t autoExposureFlag = (uint8_t)autoExposureEnabled;
            logCameraInspectorFailure(
                "Updating auto exposure failed",
                VKRT_setAutoExposureEnabled(vkrt, autoExposureFlag)
            );
        }
        if (!autoExposureEnabled) {
            float exposure = settings->exposure;
            if (ImGui_DragFloatEx(
                    "Exposure",
                    &exposure,
                    0.01f,
                    kExposureMin,
                    kExposureMax,
                    "%.3f",
                    ImGuiSliderFlags_AlwaysClamp
                )) {
                VKRT_Result result = VKRT_setExposure(vkrt, exposure);
                logCameraInspectorFailure("Updating exposure failed", result);
                if (result == VKRT_SUCCESS) {
                    settings->exposure = exposure;
                }
            }
        }

        ImGui_EndDisabled();

        inspectorUnindentSection();
        inspectorEndCollapsingHeaderSection();
    }
}

static void drawEnvironmentTextureSummary(VKRT* vkrt, const VKRT_SceneSettingsSnapshot* settings) {
    if (!inspectorBeginKeyValueTable("##environment_texture")) return;

    char environmentTextureLabel[VKRT_NAME_LEN];
    formatEnvironmentTextureLabel(
        vkrt,
        settings->environmentTextureIndex,
        environmentTextureLabel,
        sizeof(environmentTextureLabel)
    );

    ImGui_TableNextRow();
    ImGui_TableSetColumnIndex(0);
    ImGui_AlignTextToFramePadding();
    ImGui_TextDisabled("Texture");

    ImGui_TableSetColumnIndex(1);
    ImGui_AlignTextToFramePadding();
    ImGui_TextUnformatted(environmentTextureLabel);
    inspectorEndKeyValueTable();
}

static void drawEnvironmentTextureActions(Session* session, uint32_t environmentTextureIndex) {
    if (inspectorPaddedButton(ICON_FA_FOLDER_OPEN " Load")) {
        sessionRequestEnvironmentImportDialog(session);
    }
    tooltipOnHover("Load an environment texture for image-based lighting.");

    ImGui_SameLine();
    ImGui_BeginDisabled(environmentTextureIndex == VKRT_INVALID_INDEX);
    if (inspectorPaddedButton(ICON_FA_XMARK " Clear")) {
        sessionQueueEnvironmentClear(session);
    }
    if (environmentTextureIndex != VKRT_INVALID_INDEX) {
        tooltipOnHover("Remove the environment texture and use the solid sky color.");
    } else {
        tooltipOnHover("No texture loaded.");
    }
    ImGui_EndDisabled();
}

static void drawCameraEnvironmentSection(
    VKRT* vkrt,
    Session* session,
    VKRT_SceneSettingsSnapshot* settings,
    bool renderModeActive
) {
    if (inspectorBeginCollapsingHeaderSection("Environment", ImGuiTreeNodeFlags_DefaultOpen)) {
        inspectorIndentSection();
        ImGui_BeginDisabled(renderModeActive);

        float environmentStrength = settings->environmentStrength;
        if (ImGui_DragFloatEx(
                "Strength",
                &environmentStrength,
                0.01f,
                0.0f,
                kEnvironmentStrengthMax,
                "%.3f",
                ImGuiSliderFlags_AlwaysClamp
            )) {
            VKRT_Result result = VKRT_setEnvironmentLight(vkrt, settings->environmentColor, environmentStrength);
            logCameraInspectorFailure("Updating environment strength failed", result);
            if (result == VKRT_SUCCESS) {
                settings->environmentStrength = environmentStrength;
            }
        }

        float environmentColor[3] = {
            settings->environmentColor[0],
            settings->environmentColor[1],
            settings->environmentColor[2],
        };
        if (ImGui_ColorEdit3("Color", environmentColor, ImGuiColorEditFlags_Float)) {
            logCameraInspectorFailure(
                "Updating environment color failed",
                VKRT_setEnvironmentLight(vkrt, environmentColor, settings->environmentStrength)
            );
        }

        float environmentRotation = settings->environmentRotation;
        if (ImGui_DragFloatEx(
                "Rotation",
                &environmentRotation,
                0.25f,
                kEnvironmentRotationMinDeg,
                kEnvironmentRotationMaxDeg,
                "%.2f deg",
                ImGuiSliderFlags_AlwaysClamp
            )) {
            environmentRotation = fmodf(environmentRotation, 360.0f);
            if (environmentRotation < -180.0f) environmentRotation += 360.0f;
            if (environmentRotation >= 180.0f) environmentRotation -= 360.0f;

            VKRT_Result result = VKRT_setEnvironmentRotation(vkrt, environmentRotation);
            logCameraInspectorFailure("Updating environment rotation failed", result);
            if (result == VKRT_SUCCESS) {
                settings->environmentRotation = environmentRotation;
            }
        }

        drawEnvironmentTextureSummary(vkrt, settings);
        drawEnvironmentTextureActions(session, settings->environmentTextureIndex);

        ImGui_EndDisabled();
        inspectorUnindentSection();
        inspectorEndCollapsingHeaderSection();
    }
}

static void drawCameraDebugSection(VKRT* vkrt, const VKRT_SceneSettingsSnapshot* settings, bool renderModeActive) {
    if (inspectorBeginCollapsingHeaderSection("Debug", ImGuiTreeNodeFlags_DefaultOpen)) {
        inspectorIndentSection();
        ImGui_BeginDisabled(renderModeActive);

        const char* debugModeLabels[] =
            {"None",
             "Normals",
             "Depth",
             "Bounce Count",
             "NEE Only",
             "BSDF Only",
             "Selection Mask",
             "Base Color Map",
             "Metallic Map",
             "Roughness Map",
             "Normal Map",
             "Emissive Map",
             "Denoiser Albedo",
             "Denoiser Normal",
             "Denoiser Feature Validity",
             "Denoiser Feature Depth",
             "Denoiser Follow Specular"};
        int debugModeValue = (int)settings->debugMode;
        if (debugModeValue < 0 || debugModeValue >= (int)VKRT_DEBUG_MODE_COUNT) debugModeValue = 0;
        if (ImGui_ComboCharEx(
                "View Mode",
                &debugModeValue,
                debugModeLabels,
                VKRT_DEBUG_MODE_COUNT,
                VKRT_DEBUG_MODE_COUNT
            )) {
            logCameraInspectorFailure("Updating debug mode failed", VKRT_setDebugMode(vkrt, (uint32_t)debugModeValue));
        }

        bool neeEnabled = settings->misNeeEnabled != 0;
        if (ImGui_Checkbox("NEE (Direct Light)", &neeEnabled)) {
            uint32_t neeEnabledFlag = (uint32_t)neeEnabled;
            logCameraInspectorFailure("Updating NEE toggle failed", VKRT_setMISNEEEnabled(vkrt, neeEnabledFlag));
        }
        tooltipOnHover("Samples lights directly to reduce noise from emissive lighting.");

        ImGui_EndDisabled();
        inspectorUnindentSection();
        inspectorEndCollapsingHeaderSection();
    }
}

static void drawAutoSPPControls(VKRT* vkrt, bool autoSPP, const VKRT_SceneSettingsSnapshot* settings) {
    if (autoSPP) {
        int targetFPS = (int)settings->autoSPPTargetFPS;
        if (ImGui_DragIntEx("Target FPS", &targetFPS, 0.5f, 30, 360, "%d", ImGuiSliderFlags_AlwaysClamp)) {
            logCameraInspectorFailure(
                "Updating auto SPP target FPS failed",
                VKRT_setAutoSPPTargetFPS(vkrt, (uint32_t)targetFPS)
            );
        }
        return;
    }

    int spp = (int)settings->samplesPerPixel;
    if (ImGui_SliderIntEx("SPP", &spp, 1, 2048, "%d", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic)) {
        logCameraInspectorFailure("Updating samples per pixel failed", VKRT_setSamplesPerPixel(vkrt, (uint32_t)spp));
    }
}

static void drawPathDepthControls(VKRT* vkrt, VKRT_SceneSettingsSnapshot* settings) {
    int minBounces = (int)settings->rrMinDepth;
    int maxBounces = (int)settings->rrMaxDepth;
    if (ImGui_DragIntEx(
            "Min Bounces",
            &minBounces,
            0.25f,
            kPathDepthMin,
            kPathDepthMax,
            "%d",
            ImGuiSliderFlags_AlwaysClamp
        )) {
        VKRT_Result result = VKRT_setPathDepth(vkrt, (uint32_t)minBounces, settings->rrMaxDepth);
        logCameraInspectorFailure("Updating minimum path depth failed", result);
        if (result == VKRT_SUCCESS) {
            settings->rrMinDepth = (uint32_t)minBounces;
            if (settings->rrMinDepth > settings->rrMaxDepth) settings->rrMaxDepth = settings->rrMinDepth;
        }
    }

    maxBounces = (int)settings->rrMaxDepth;
    if (ImGui_DragIntEx(
            "Max Bounces",
            &maxBounces,
            0.25f,
            kPathDepthMin + 1,
            kPathDepthMax,
            "%d",
            ImGuiSliderFlags_AlwaysClamp
        )) {
        VKRT_Result result = VKRT_setPathDepth(vkrt, settings->rrMinDepth, (uint32_t)maxBounces);
        logCameraInspectorFailure("Updating maximum path depth failed", result);
        if (result == VKRT_SUCCESS) {
            settings->rrMaxDepth = (uint32_t)maxBounces;
            if (settings->rrMinDepth > settings->rrMaxDepth) settings->rrMinDepth = settings->rrMaxDepth;
        }
    }
}

static void drawCameraPerformanceSection(VKRT* vkrt, VKRT_SceneSettingsSnapshot* settings, bool renderModeActive) {
    if (inspectorBeginCollapsingHeaderSection("Performance", ImGuiTreeNodeFlags_DefaultOpen)) {
        inspectorIndentSection();
        ImGui_BeginDisabled(renderModeActive);

        bool autoSPP = settings->autoSPPEnabled != 0;
        if (ImGui_Checkbox("Auto SPP", &autoSPP)) {
            logCameraInspectorFailure("Updating auto SPP toggle failed", VKRT_setAutoSPPEnabled(vkrt, (uint8_t)autoSPP));
        }

        drawAutoSPPControls(vkrt, autoSPP, settings);
        drawPathDepthControls(vkrt, settings);

        ImGui_EndDisabled();
        inspectorUnindentSection();
        inspectorEndCollapsingHeaderSection();
    }
}

void inspectorDrawCameraTab(VKRT* vkrt, Session* session) {
    if (!vkrt || !session) return;

    VKRT_SceneSettingsSnapshot settings = {0};
    VKRT_RenderStatusSnapshot status = {0};
    if (VKRT_getSceneSettings(vkrt, &settings) != VKRT_SUCCESS || VKRT_getRenderStatus(vkrt, &status) != VKRT_SUCCESS) {
        return;
    }

    bool renderModeActive = VKRT_renderStatusIsActive(&status) != 0;
    drawCameraPoseSection(vkrt, renderModeActive);
    drawCameraShadingSection(vkrt, &settings, renderModeActive);
    drawCameraEnvironmentSection(vkrt, session, &settings, renderModeActive);
    drawCameraDebugSection(vkrt, &settings, renderModeActive);
    drawCameraPerformanceSection(vkrt, &settings, renderModeActive);

    if (renderModeActive) {
        ImGui_Spacing();
        ImGui_TextDisabled("Camera is locked while rendering.");
    }
}
