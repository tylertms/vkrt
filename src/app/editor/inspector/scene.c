#include "common.h"
#include "constants.h"
#include "debug.h"

#include "IconsFontAwesome6.h"
#include "session.h"

#include <dcimgui.h>
#include <math.h>
#include <stdio.h>

static void formatMeshGeometryText(const VKRT_MeshSnapshot* mesh, char* out, size_t outSize) {
    if (!mesh || !out || outSize == 0) return;

    if (mesh->ownsGeometry) {
        snprintf(out, outSize, "Unique");
        return;
    }

    snprintf(out, outSize, "Instanced");
}

static bool drawSceneBrowserEntry(Session* session, uint32_t meshIndex, const VKRT_MeshSnapshot* mesh, bool isSelected) {
    if (!session || !mesh) return false;

    char id[32] = {0};
    char nameText[192] = {0};
    const float rowHeight = ImGui_GetTextLineHeight() + 2.0f;
    snprintf(id, sizeof(id), "##mesh_%u", meshIndex);

    const char* meshName = sessionGetMeshName(session, meshIndex);
    snprintf(nameText, sizeof(nameText), "%s %s",
        mesh->ownsGeometry ? ICON_FA_CUBE : ICON_FA_CLONE,
        meshName);

    const ImVec4 transparent = {0.0f, 0.0f, 0.0f, 0.0f};
    ImGui_PushStyleColorImVec4(ImGuiCol_Header, transparent);
    ImGui_PushStyleColorImVec4(ImGuiCol_HeaderHovered, transparent);
    ImGui_PushStyleColorImVec4(ImGuiCol_HeaderActive, transparent);
    bool clicked = ImGui_SelectableEx(id, isSelected, ImGuiSelectableFlags_None, (ImVec2){0.0f, rowHeight});
    ImGui_PopStyleColorEx(3);

    ImVec2 min = ImGui_GetItemRectMin();
    ImVec2 max = ImGui_GetItemRectMax();
    ImVec2 nameSize = ImGui_CalcTextSize(nameText);
    const ImGuiStyle* style = ImGui_GetStyle();
    float textY = floorf(min.y + (max.y - min.y - nameSize.y) * 0.5f);
    float textX = min.x + style->FramePadding.x;
    bool hovered = ImGui_IsItemHovered(ImGuiHoveredFlags_None);
    bool held = ImGui_IsItemActive();

    ImU32 bgColor = 0;
    if (held) bgColor = ImGui_GetColorU32(ImGuiCol_HeaderActive);
    else if (hovered) bgColor = ImGui_GetColorU32(ImGuiCol_HeaderHovered);
    else if (isSelected) bgColor = ImGui_GetColorU32(ImGuiCol_Header);

    if (bgColor != 0) {
        ImDrawList_AddRectFilledEx(ImGui_GetWindowDrawList(), min, max, bgColor, 4.0f, 0);
    }

    ImDrawList_AddTextEx(ImGui_GetWindowDrawList(),
        (ImVec2){textX, textY},
        ImGui_GetColorU32(ImGuiCol_Text),
        nameText,
        NULL);
    return clicked;
}

static uint32_t querySelectedMeshIndex(VKRT* vkrt) {
    uint32_t selectedMeshIndex = VKRT_INVALID_INDEX;
    if (VKRT_getSelectedMesh(vkrt, &selectedMeshIndex) != VKRT_SUCCESS) {
        return VKRT_INVALID_INDEX;
    }
    return selectedMeshIndex;
}

static void updateSelectedMesh(VKRT* vkrt, uint32_t meshIndex) {
    if (!vkrt) return;
    VKRT_Result result = VKRT_setSelectedMesh(vkrt, meshIndex);
    if (result != VKRT_SUCCESS) {
        LOG_ERROR("Updating selected mesh failed (%d)", (int)result);
    }
}

static void drawMeshInfoHeader(Session* session, uint32_t meshIndex, const VKRT_MeshSnapshot* mesh) {
    if (!session || !mesh) return;

    uint32_t triangleCount = mesh->info.indexCount / 3;
    uint64_t vertexBytes = (uint64_t)mesh->info.vertexCount * 32;
    uint64_t indexBytes = (uint64_t)mesh->info.indexCount * 4;

    char countText[48] = {0};
    char sizeText[32] = {0};
    char geometryText[48] = {0};
    char sourceText[192] = {0};
    snprintf(countText, sizeof(countText), "%u verts / %u tris", mesh->info.vertexCount, triangleCount);
    formatByteSize(vertexBytes + indexBytes, sizeText, sizeof(sizeText));
    formatMeshGeometryText(mesh, geometryText, sizeof(geometryText));
    if (!mesh->ownsGeometry) {
        snprintf(sourceText, sizeof(sourceText), "%s (#%u)",
            sessionGetMeshName(session, mesh->geometrySource),
            mesh->geometrySource);
    }

    inspectorTightSeparatorText(ICON_FA_CIRCLE_INFO " Stats");
    inspectorIndentSection();
    if (inspectorBeginKeyValueTable("##mesh_stats")) {
        ImGui_TableNextRow();
        ImGui_TableSetColumnIndex(0);
        ImGui_TextDisabled("Name");
        ImGui_TableSetColumnIndex(1);
        char* meshName = sessionGetMeshName(session, meshIndex);
        ImGui_InputText("##mesh_name", meshName, VKRT_NAME_LEN, ImGuiInputTextFlags_None);

        inspectorKeyValueRow("Geometry", geometryText);
        if (!mesh->ownsGeometry) {
            inspectorKeyValueRow("Source", sourceText);
        }
        inspectorKeyValueRow("Counts", countText);
        inspectorKeyValueRow("Size", sizeText);
        inspectorEndKeyValueTable();
    }
    inspectorUnindentSection();
}

static void drawMeshTransformEditor(VKRT* vkrt, uint32_t meshIndex, const VKRT_MeshSnapshot* mesh) {
    if (!vkrt || !mesh) return;
    float position[3] = {mesh->info.position[0], mesh->info.position[1], mesh->info.position[2]};
    float rotation[3] = {mesh->info.rotation[0], mesh->info.rotation[1], mesh->info.rotation[2]};
    float scale[3] = {mesh->info.scale[0], mesh->info.scale[1], mesh->info.scale[2]};

    bool transformChanged = false;
    inspectorPushWidgetSpacing();
    transformChanged |= ImGui_DragFloat3Ex("Translate", position, 0.001f, 0.0f, 0.0f, "%.3f", ImGuiSliderFlags_None);
    transformChanged |= ImGui_DragFloat3Ex("Rotate", rotation, 0.05f, 0.0f, 0.0f, "%.2f", ImGuiSliderFlags_None);
    transformChanged |= ImGui_DragFloat3Ex("Scale", scale, 0.001f, 0.001f, 1000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    inspectorPopWidgetSpacing();

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
}

static void drawMeshMaterialEditor(VKRT* vkrt, uint32_t meshIndex, const VKRT_MeshSnapshot* mesh) {
    if (!vkrt || !mesh) return;

    Material material = mesh->material;
    bool materialChanged = false;

    inspectorPushWidgetSpacing();
    materialChanged |= ImGui_ColorEdit3("Base Color", material.baseColor, ImGuiColorEditFlags_Float);
    materialChanged |= ImGui_SliderFloatEx("Metallic", &material.metallic, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    materialChanged |= ImGui_SliderFloatEx("Roughness", &material.roughness, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    materialChanged |= ImGui_SliderFloatEx("Specular", &material.specular, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    materialChanged |= ImGui_SliderFloatEx("Specular Tint", &material.specularTint, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    materialChanged |= ImGui_SliderFloatEx("Anisotropic", &material.anisotropic, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    materialChanged |= ImGui_SliderFloatEx("Sheen", &material.sheen, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    materialChanged |= ImGui_SliderFloatEx("Sheen Tint", &material.sheenTint, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    materialChanged |= ImGui_SliderFloatEx("Clearcoat", &material.clearcoat, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    materialChanged |= ImGui_SliderFloatEx("Clearcoat Gloss", &material.clearcoatGloss, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    materialChanged |= ImGui_DragFloatEx("Emission", &material.emissionLuminance, 0.1f, 0.0f, 1000000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    materialChanged |= ImGui_ColorEdit3("Emission Color", material.emissionColor, ImGuiColorEditFlags_Float);
    inspectorPopWidgetSpacing();

    if (!materialChanged) return;

    VKRT_Result result = VKRT_setMeshMaterial(vkrt, meshIndex, &material);
    if (result != VKRT_SUCCESS) {
        LOG_ERROR("Updating mesh material failed (%d)", (int)result);
    }
}

static void drawSelectedMeshEditor(VKRT* vkrt, Session* session, uint32_t meshIndex, bool renderModeActive) {
    if (!vkrt || !session || meshIndex == VKRT_INVALID_INDEX) return;

    VKRT_MeshSnapshot mesh = {0};
    if (VKRT_getMeshSnapshot(vkrt, meshIndex, &mesh) != VKRT_SUCCESS) {
        ImGui_TextDisabled("Selected mesh is no longer available.");
        return;
    }

    drawMeshInfoHeader(session, meshIndex, &mesh);

    ImGui_BeginDisabled(renderModeActive);

    inspectorTightSeparatorText(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT " Transform");
    inspectorIndentSection();
    drawMeshTransformEditor(vkrt, meshIndex, &mesh);
    inspectorUnindentSection();

    inspectorTightSeparatorText(ICON_FA_PALETTE " Material");
    inspectorIndentSection();
    drawMeshMaterialEditor(vkrt, meshIndex, &mesh);
    inspectorUnindentSection();

    ImGui_Spacing();
    if (inspectorPaddedButton(ICON_FA_TRASH " Remove Mesh")) {
        sessionQueueMeshRemoval(session, meshIndex);
    }
    tooltipOnHover("Remove this mesh from the scene.");

    ImGui_EndDisabled();

    if (renderModeActive) {
        ImGui_Spacing();
        ImGui_TextDisabled("Mesh editing is locked while rendering.");
    }
}

void inspectorDrawSceneBrowser(VKRT* vkrt, Session* session) {
    if (!vkrt || !session) return;

    const VKRT_PublicState* state = VKRT_getPublicState(vkrt);
    if (!state) return;

    uint32_t meshCount = 0;
    if (VKRT_getMeshCount(vkrt, &meshCount) != VKRT_SUCCESS) {
        ImGui_TextDisabled("Mesh list unavailable.");
        return;
    }

    uint32_t selectedMeshIndex = querySelectedMeshIndex(vkrt);

    inspectorIndentSection();
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_ItemSpacing, (ImVec2){6.0f, 2.0f});
    for (uint32_t meshIndex = 0; meshIndex < meshCount; meshIndex++) {
        VKRT_MeshSnapshot mesh = {0};
        if (VKRT_getMeshSnapshot(vkrt, meshIndex, &mesh) != VKRT_SUCCESS) continue;

        bool isSelected = selectedMeshIndex == meshIndex;
        if (drawSceneBrowserEntry(session, meshIndex, &mesh, isSelected)) {
            updateSelectedMesh(vkrt, isSelected ? VKRT_INVALID_INDEX : meshIndex);
            selectedMeshIndex = isSelected ? VKRT_INVALID_INDEX : meshIndex;
        }
    }
    ImGui_PopStyleVar();
    inspectorUnindentSection();

    if (meshCount == 0) {
        inspectorIndentSection();
        ImGui_TextDisabled("No meshes. Use File > Import Mesh.");
        inspectorUnindentSection();
    }

    (void)state;
}

void inspectorDrawSelectionPanel(VKRT* vkrt, Session* session) {
    if (!vkrt || !session) return;

    const VKRT_PublicState* state = VKRT_getPublicState(vkrt);
    if (!state) return;

    uint32_t selectedMeshIndex = querySelectedMeshIndex(vkrt);
    if (selectedMeshIndex == VKRT_INVALID_INDEX) {
        ImGui_TextWrapped("Click a mesh in the Scene list or left-click it in the viewport.");
        return;
    }

    drawSelectedMeshEditor(vkrt, session, selectedMeshIndex, state->renderModeActive != 0);
}
