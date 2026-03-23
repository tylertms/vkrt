#include "common.h"
#include "debug.h"
#include "session.h"
#include "vkrt.h"

#include "IconsFontAwesome6.h"

#include <math.h>

static const float kCameraPoseDragSpeed = 0.01f;
static const float kCameraPoseDegenerateThreshold = 1e-8f;
static const float kCameraPoseNudgeDistance = 0.001f;
static const float kCameraFovMinDeg = 10.0f;
static const float kCameraFovMaxDeg = 140.0f;
static const float kExposureMin = 0.03125f;
static const float kExposureMax = 32.0f;
static const float kEnvironmentStrengthMax = 1000000.0f;
static const int kPathDepthMin = 0;
static const int kPathDepthMax = 64;

static void formatEnvironmentTextureLabel(VKRT* vkrt, uint32_t textureIndex, char* out, size_t outSize) {
    if (!out || outSize == 0u) return;

    if (!vkrt || textureIndex == VKRT_INVALID_INDEX) {
        snprintf(out, outSize, "None");
        return;
    }

    VKRT_TextureSnapshot texture = {0};
    if (VKRT_getTextureSnapshot(vkrt, textureIndex, &texture) == VKRT_SUCCESS) {
        snprintf(out, outSize, "%s", texture.name[0] ? texture.name : "Texture");
        return;
    }

    snprintf(out, outSize, "Texture #%u", textureIndex);
}

void inspectorDrawCameraTab(VKRT* vkrt, Session* session) {
    if (!vkrt || !session) return;

    VKRT_SceneSettingsSnapshot settings = {0};
    VKRT_RenderStatusSnapshot status = {0};
    if (VKRT_getSceneSettings(vkrt, &settings) != VKRT_SUCCESS ||
        VKRT_getRenderStatus(vkrt, &status) != VKRT_SUCCESS) {
        return;
    }

    bool renderModeActive = status.renderModeActive != 0;

    if (inspectorBeginCollapsingHeaderSection("Camera Pose", ImGuiTreeNodeFlags_DefaultOpen)) {
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
        inspectorEndCollapsingHeaderSection();
    }

    if (inspectorBeginCollapsingHeaderSection("Shading", ImGuiTreeNodeFlags_DefaultOpen)) {
        inspectorIndentSection();
        ImGui_BeginDisabled(renderModeActive);

        const char* toneMappingLabels[] = {"None", "ACES"};
        int toneMappingMode = (int)settings.toneMappingMode;
        if (ImGui_ComboCharEx("Tone Mapping", &toneMappingMode, toneMappingLabels, VKRT_TONE_MAPPING_MODE_COUNT, VKRT_TONE_MAPPING_MODE_COUNT)) {
            VKRT_Result result = VKRT_setToneMappingMode(vkrt, (VKRT_ToneMappingMode)toneMappingMode);
            if (result != VKRT_SUCCESS) {
                LOG_ERROR("Updating tone mapping mode failed (%d)", (int)result);
            }
        }

        bool autoExposureEnabled = settings.autoExposureEnabled != 0;
        if (ImGui_Checkbox("Auto Exposure", &autoExposureEnabled)) {
            VKRT_Result result = VKRT_setAutoExposureEnabled(vkrt, autoExposureEnabled ? 1u : 0u);
            if (result != VKRT_SUCCESS) {
                LOG_ERROR("Updating auto exposure failed (%d)", (int)result);
            }
        }
        if (!autoExposureEnabled) {
            float exposure = settings.exposure;
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
                if (result != VKRT_SUCCESS) {
                    LOG_ERROR("Updating exposure failed (%d)", (int)result);
                } else {
                    settings.exposure = exposure;
                }
            }
        }

        ImGui_EndDisabled();

        inspectorUnindentSection();
        inspectorEndCollapsingHeaderSection();
    }

    if (inspectorBeginCollapsingHeaderSection("Environment", ImGuiTreeNodeFlags_DefaultOpen)) {
        inspectorIndentSection();
        ImGui_BeginDisabled(renderModeActive);

        float environmentStrength = settings.environmentStrength;
        if (ImGui_DragFloatEx("Strength", &environmentStrength, 0.01f, 0.0f, kEnvironmentStrengthMax, "%.3f", ImGuiSliderFlags_AlwaysClamp)) {
            VKRT_Result result = VKRT_setEnvironmentLight(vkrt, settings.environmentColor, environmentStrength);
            if (result != VKRT_SUCCESS) {
                LOG_ERROR("Updating environment strength failed (%d)", (int)result);
            } else {
                settings.environmentStrength = environmentStrength;
            }
        }

        vec3 environmentColor = {settings.environmentColor[0], settings.environmentColor[1], settings.environmentColor[2]};
        if (ImGui_ColorEdit3("Color", environmentColor, ImGuiColorEditFlags_Float)) {
            VKRT_Result result = VKRT_setEnvironmentLight(vkrt, environmentColor, settings.environmentStrength);
            if (result != VKRT_SUCCESS) {
                LOG_ERROR("Updating environment color failed (%d)", (int)result);
            }
        }

        if (inspectorBeginKeyValueTable("##environment_texture")) {
            char environmentTextureLabel[VKRT_NAME_LEN];
            formatEnvironmentTextureLabel(
                vkrt,
                settings.environmentTextureIndex,
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

        if (inspectorPaddedButton(ICON_FA_FOLDER_OPEN " Load")) {
            sessionRequestEnvironmentImportDialog(session);
        }
        tooltipOnHover("Load environment texture.");

        ImGui_SameLine();
        ImGui_BeginDisabled(settings.environmentTextureIndex == VKRT_INVALID_INDEX);
        if (inspectorPaddedButton(ICON_FA_XMARK " Clear")) {
            VKRT_Result result = VKRT_clearEnvironmentTexture(vkrt);
            if (result != VKRT_SUCCESS) {
                LOG_ERROR("Clearing environment texture failed (%d)", (int)result);
            }
        }
        if (settings.environmentTextureIndex != VKRT_INVALID_INDEX) {
            tooltipOnHover("Clear environment texture.");
        } else {
            tooltipOnHover("No environment texture is loaded.");
        }
        ImGui_EndDisabled();

        ImGui_EndDisabled();
        inspectorUnindentSection();
        inspectorEndCollapsingHeaderSection();
    }

    if (inspectorBeginCollapsingHeaderSection("Debug", ImGuiTreeNodeFlags_DefaultOpen)) {
        inspectorIndentSection();
        ImGui_BeginDisabled(renderModeActive);

        const char* debugModeLabels[] = {
            "None", "Normals", "Depth", "Bounce Count",
            "NEE Only", "BSDF Only", "Selection Mask",
            "Base Color Map", "Metallic Map", "Roughness Map",
            "Normal Map", "Emissive Map"
        };
        int debugModeValue = (int)settings.debugMode;
        if (debugModeValue < 0 || debugModeValue >= (int)VKRT_DEBUG_MODE_COUNT) debugModeValue = 0;
        if (ImGui_ComboCharEx("View Mode", &debugModeValue, debugModeLabels, VKRT_DEBUG_MODE_COUNT, VKRT_DEBUG_MODE_COUNT)) {
            VKRT_Result result = VKRT_setDebugMode(vkrt, (uint32_t)debugModeValue);
            if (result != VKRT_SUCCESS) {
                LOG_ERROR("Updating debug mode failed (%d)", (int)result);
            }
        }

        bool neeEnabled = settings.misNeeEnabled != 0;
        if (ImGui_Checkbox("NEE (Direct Light)", &neeEnabled)) {
            VKRT_Result result = VKRT_setMISNEEEnabled(vkrt, neeEnabled ? 1u : 0u);
            if (result != VKRT_SUCCESS) {
                LOG_ERROR("Updating NEE toggle failed (%d)", (int)result);
            }
        }
        tooltipOnHover("Next Event Estimation with MIS. Samples emissive geometry directly for lower variance.");

        ImGui_EndDisabled();
        inspectorUnindentSection();
        inspectorEndCollapsingHeaderSection();
    }

    if (inspectorBeginCollapsingHeaderSection("Performance", ImGuiTreeNodeFlags_DefaultOpen)) {
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

        int minBounces = (int)settings.rrMinDepth;
        int maxBounces = (int)settings.rrMaxDepth;
        if (ImGui_DragIntEx("Min Bounces", &minBounces, 0.25f, kPathDepthMin, kPathDepthMax, "%d", ImGuiSliderFlags_AlwaysClamp)) {
            VKRT_Result result = VKRT_setPathDepth(vkrt, (uint32_t)minBounces, settings.rrMaxDepth);
            if (result != VKRT_SUCCESS) {
                LOG_ERROR("Updating minimum path depth failed (%d)", (int)result);
            } else {
                settings.rrMinDepth = (uint32_t)minBounces;
                if (settings.rrMinDepth > settings.rrMaxDepth) settings.rrMaxDepth = settings.rrMinDepth;
            }
        }

        maxBounces = (int)settings.rrMaxDepth;
        if (ImGui_DragIntEx("Max Bounces", &maxBounces, 0.25f, kPathDepthMin + 1, kPathDepthMax, "%d", ImGuiSliderFlags_AlwaysClamp)) {
            VKRT_Result result = VKRT_setPathDepth(vkrt, settings.rrMinDepth, (uint32_t)maxBounces);
            if (result != VKRT_SUCCESS) {
                LOG_ERROR("Updating maximum path depth failed (%d)", (int)result);
            } else {
                settings.rrMaxDepth = (uint32_t)maxBounces;
                if (settings.rrMinDepth > settings.rrMaxDepth) settings.rrMinDepth = settings.rrMaxDepth;
            }
        }

        ImGui_EndDisabled();
        inspectorUnindentSection();
        inspectorEndCollapsingHeaderSection();
    }

    if (renderModeActive) {
        ImGui_Spacing();
        ImGui_TextDisabled("Camera is locked while rendering.");
    }
}
