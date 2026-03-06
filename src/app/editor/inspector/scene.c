#include "common.h"
#include "debug.h"

#include "IconsFontAwesome6.h"

#include <dcimgui.h>
#include <math.h>
#include <stdio.h>

static void drawMeshInspector(VKRT* vkrt, Session* session) {
    if (!vkrt || !session) return;

    uint32_t meshCount = 0;
    if (VKRT_getMeshCount(vkrt, &meshCount) != VKRT_SUCCESS || meshCount == 0) {
        ImGui_TextDisabled("No meshes loaded.");
        return;
    }

    for (uint32_t meshIndex = 0; meshIndex < meshCount; meshIndex++) {
        VKRT_MeshSnapshot mesh = {0};
        if (VKRT_getMeshSnapshot(vkrt, meshIndex, &mesh) != VKRT_SUCCESS) continue;

        char header[160] = {0};
        snprintf(header, sizeof(header), "Mesh %u (%s)", meshIndex, sessionGetMeshName(session, meshIndex));

        ImGui_PushIDInt((int)meshIndex);
        bool visible = true;
        bool open = ImGui_CollapsingHeaderBoolPtr(header, &visible, ImGuiTreeNodeFlags_None);
        if (!visible) {
            sessionQueueMeshRemoval(session, meshIndex);
            ImGui_PopID();
            continue;
        }

        if (!mesh.ownsGeometry && mesh.geometrySource < meshCount) {
            ImGui_SameLine();
            ImGui_TextDisabled("-> %u", mesh.geometrySource);
            tooltipOnHover("This mesh instance reuses geometry from the shown source mesh index.");
        }

        if (!open) {
            ImGui_PopID();
            continue;
        }

        ImGui_Indent();
        if (ImGui_CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui_Indent();
            float position[3] = {mesh.info.position[0], mesh.info.position[1], mesh.info.position[2]};
            float rotation[3] = {mesh.info.rotation[0], mesh.info.rotation[1], mesh.info.rotation[2]};
            float scale[3] = {mesh.info.scale[0], mesh.info.scale[1], mesh.info.scale[2]};

            bool transformChanged = false;
            transformChanged |= ImGui_DragFloat3Ex("Translate", position, 0.001f, 0.0f, 0.0f, "%.3f", ImGuiSliderFlags_None);
            transformChanged |= ImGui_DragFloat3Ex("Rotate", rotation, 0.05f, 0.0f, 0.0f, "%.2f", ImGuiSliderFlags_None);
            transformChanged |= ImGui_DragFloat3Ex("Scale", scale, 0.001f, 0.001f, 1000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);

            if (transformChanged) {
                for (int axis = 0; axis < 3; axis++) {
                    rotation[axis] = fmodf(rotation[axis], 360.0f);
                    if (rotation[axis] < -180.0f) rotation[axis] += 360.0f;
                    if (rotation[axis] >= 180.0f) rotation[axis] -= 360.0f;
                }
                VKRT_Result result = VKRT_setMeshTransform(vkrt, meshIndex, position, rotation, scale);
                if (result != VKRT_SUCCESS) {
                    LOG_ERROR("Updating mesh transform failed (%d)", (int)result);
                }
            }

            ImGui_Unindent();
        }

        Material material = mesh.material;
        bool materialChanged = false;

        if (ImGui_CollapsingHeader("Base", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui_Indent();
            materialChanged |= ImGui_ColorEdit3("Color##baseColor", material.baseColor, ImGuiColorEditFlags_Float);
            materialChanged |= ImGui_SliderFloatEx("Metallic##metallic", &material.metallic, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            materialChanged |= ImGui_SliderFloatEx("Roughness##roughness", &material.roughness, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            materialChanged |= ImGui_SliderFloatEx("Subsurface##subsurface", &material.subsurface, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            materialChanged |= ImGui_SliderFloatEx("Transmission##transmission", &material.transmission, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            ImGui_Unindent();
        }

        if (ImGui_CollapsingHeader("Specular", ImGuiTreeNodeFlags_None)) {
            ImGui_Indent();
            materialChanged |= ImGui_SliderFloatEx("Specular##specular", &material.specular, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            materialChanged |= ImGui_SliderFloatEx("Specular Tint##specularTint", &material.specularTint, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            materialChanged |= ImGui_SliderFloatEx("Anisotropic##anisotropic", &material.anisotropic, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            materialChanged |= ImGui_DragFloatEx("IOR##ior", &material.ior, 0.01f, 1.0f, 3.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            ImGui_Unindent();
        }

        if (ImGui_CollapsingHeader("Sheen & Coat", ImGuiTreeNodeFlags_None)) {
            ImGui_Indent();
            materialChanged |= ImGui_SliderFloatEx("Sheen##sheen", &material.sheen, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            materialChanged |= ImGui_SliderFloatEx("Sheen Tint##sheenTint", &material.sheenTint, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            materialChanged |= ImGui_SliderFloatEx("Clearcoat##clearcoat", &material.clearcoat, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            materialChanged |= ImGui_SliderFloatEx("Clearcoat Gloss##clearcoatGloss", &material.clearcoatGloss, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            ImGui_Unindent();
        }

        if (ImGui_CollapsingHeader("Emission", ImGuiTreeNodeFlags_None)) {
            ImGui_Indent();
            materialChanged |= ImGui_DragFloatEx("Luminance##emissionLuminance", &material.emissionLuminance, 0.1f, 0.0f, 1000000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            materialChanged |= ImGui_ColorEdit3("Color##emissionColor", material.emissionColor, ImGuiColorEditFlags_Float);
            ImGui_Unindent();
        }

        if (materialChanged) {
            VKRT_Result result = VKRT_setMeshMaterial(vkrt, meshIndex, &material);
            if (result != VKRT_SUCCESS) {
                LOG_ERROR("Updating mesh material failed (%d)", (int)result);
            }
        }

        ImGui_Unindent();
        ImGui_PopID();
    }
}

void inspectorDrawSceneTab(VKRT* vkrt, Session* session, bool renderModeActive) {
    if (!vkrt || !session) return;

    if (ImGui_CollapsingHeader("Import", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui_Indent();
        ImGui_BeginDisabled(renderModeActive);
        if (ImGui_Button(ICON_FA_FOLDER_PLUS " Import mesh")) {
            sessionRequestMeshImportDialog(session);
        }
        ImGui_EndDisabled();
        ImGui_Dummy((ImVec2){0.0f, kInspectorControlSpacing});
        ImGui_Unindent();
    }

    if (!ImGui_CollapsingHeader("Meshes", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui_Indent();
    ImGui_BeginDisabled(renderModeActive);
    drawMeshInspector(vkrt, session);
    ImGui_EndDisabled();
    ImGui_Dummy((ImVec2){0.0f, kInspectorControlSpacing});
    ImGui_Unindent();
}
