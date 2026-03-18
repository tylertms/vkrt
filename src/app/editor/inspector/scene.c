#include "common.h"
#include "constants.h"
#include "debug.h"
#include "session.h"
#include "vkrt.h"

#include "IconsFontAwesome6.h"

#include <dcimgui.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

enum {
    kSceneSuffixTextCapacity = 32,
    kSceneStatTextCapacity = 48,
    kSceneSizeTextCapacity = 32,
    kSceneBrowserNameTextCapacity = 192,
    kSceneBrowserIdCapacity = 32,
    kSceneSourceTextCapacity = 192,
    kSceneMaterialNameTextCapacity = 192,
    kSceneMaterialComboMaxEntries = 64,
};

static void formatPrefixedText(char* out, size_t outSize, const char* prefix, const char* text) {
    if (!out || outSize == 0 || !prefix || !text) return;

    size_t prefixLength = strlen(prefix);
    size_t available = outSize > prefixLength + 2u ? outSize - prefixLength - 2u : 0u;
    int textLimit = available > (size_t)INT_MAX ? INT_MAX : (int)available;
    snprintf(out, outSize, "%s %.*s", prefix, textLimit, text);
}

static void formatMeshSourceText(char* out, size_t outSize, const char* sourceName, uint32_t sourceIndex) {
    if (!out || outSize == 0 || !sourceName) return;

    char suffix[kSceneSuffixTextCapacity];
    int suffixLength = snprintf(suffix, sizeof(suffix), " (#%u)", sourceIndex);
    if (suffixLength < 0) return;

    size_t suffixSize = (size_t)suffixLength;
    size_t available = outSize > suffixSize + 1u ? outSize - suffixSize - 1u : 0u;
    int nameLimit = available > (size_t)INT_MAX ? INT_MAX : (int)available;
    snprintf(out, outSize, "%.*s%s", nameLimit, sourceName, suffix);
}

static void formatMeshGeometryText(const VKRT_MeshSnapshot* mesh, char* out, size_t outSize) {
    if (!mesh || !out || outSize == 0) return;
    snprintf(out, outSize, mesh->ownsGeometry ? "Unique" : "Instanced");
}

static bool drawSceneBrowserEntry(uint32_t meshIndex, const VKRT_MeshSnapshot* mesh, bool isSelected) {
    if (!mesh) return false;

    char id[kSceneBrowserIdCapacity];
    char nameText[kSceneBrowserNameTextCapacity];
    const float rowHeight = ImGui_GetTextLineHeight() + 2.0f;
    snprintf(id, sizeof(id), "##mesh_%u", meshIndex);

    const char* meshName = mesh->name[0] ? mesh->name : "(unknown)";
    formatPrefixedText(
        nameText,
        sizeof(nameText),
        mesh->ownsGeometry ? ICON_FA_CUBE : ICON_FA_CLONE,
        meshName
    );

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

    ImDrawList_AddTextEx(
        ImGui_GetWindowDrawList(),
        (ImVec2){textX, textY},
        ImGui_GetColorU32(ImGuiCol_Text),
        nameText,
        NULL
    );
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

static void drawMeshInfoHeader(VKRT* vkrt, uint32_t meshIndex, const VKRT_MeshSnapshot* mesh) {
    if (!vkrt || !mesh) return;

    uint32_t triangleCount = mesh->info.indexCount / 3;
    uint64_t vertexBytes = (uint64_t)mesh->info.vertexCount * sizeof(Vertex);
    uint64_t indexBytes = (uint64_t)mesh->info.indexCount * sizeof(uint32_t);

    char countText[kSceneStatTextCapacity];
    char sizeText[kSceneSizeTextCapacity];
    char geometryText[kSceneStatTextCapacity];
    char sourceText[kSceneSourceTextCapacity];
    snprintf(countText, sizeof(countText), "%u verts / %u tris", mesh->info.vertexCount, triangleCount);
    formatByteSize(vertexBytes + indexBytes, sizeText, sizeof(sizeText));
    formatMeshGeometryText(mesh, geometryText, sizeof(geometryText));
    if (!mesh->ownsGeometry) {
        VKRT_MeshSnapshot sourceMesh = {0};
        const char* sourceName = "(unknown)";
        if (VKRT_getMeshSnapshot(vkrt, mesh->geometrySource, &sourceMesh) == VKRT_SUCCESS && sourceMesh.name[0]) {
            sourceName = sourceMesh.name;
        }
        formatMeshSourceText(sourceText, sizeof(sourceText), sourceName, mesh->geometrySource);
    }

    ImGui_SeparatorText(ICON_FA_CIRCLE_INFO " Stats");
    inspectorIndentSection();
    if (inspectorBeginKeyValueTable("##mesh_stats")) {
        ImGui_TableNextRow();
        ImGui_TableSetColumnIndex(0);
        ImGui_AlignTextToFramePadding();
        ImGui_TextDisabled("Name");
        ImGui_TableSetColumnIndex(1);
        char meshName[VKRT_NAME_LEN];
        snprintf(meshName, sizeof(meshName), "%s", mesh->name[0] ? mesh->name : "(unknown)");
        if (ImGui_InputText("##mesh_name", meshName, sizeof(meshName), ImGuiInputTextFlags_None)) {
            VKRT_Result result = VKRT_setMeshName(vkrt, meshIndex, meshName);
            if (result != VKRT_SUCCESS) {
                LOG_ERROR("Updating mesh name failed (%d)", (int)result);
            }
        }

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
}

static void formatMaterialLabel(char* out, size_t outSize, const VKRT_MaterialSnapshot* material, uint32_t materialIndex) {
    if (!out || outSize == 0 || !material) return;

    char suffix[kSceneSuffixTextCapacity];
    int suffixLength = snprintf(suffix, sizeof(suffix), " (#%u)", materialIndex);
    if (suffixLength < 0) return;

    size_t suffixSize = (size_t)suffixLength;
    size_t available = outSize > suffixSize + 1u ? outSize - suffixSize - 1u : 0u;
    int nameLimit = available > (size_t)INT_MAX ? INT_MAX : (int)available;
    const char* materialName = material->name[0] ? material->name : "Material";
    snprintf(out, outSize, "%.*s%s", nameLimit, materialName, suffix);
}

static void drawMeshMaterialBindingEditor(VKRT* vkrt, uint32_t meshIndex, const VKRT_MeshSnapshot* mesh) {
    if (!vkrt || !mesh) return;

    uint32_t materialCount = 0;
    if (VKRT_getMaterialCount(vkrt, &materialCount) != VKRT_SUCCESS || materialCount == 0) {
        ImGui_TextDisabled("No materials available.");
        return;
    }

    if (materialCount > kSceneMaterialComboMaxEntries) {
        materialCount = kSceneMaterialComboMaxEntries;
    }

    int currentMaterialIndex = (int)mesh->materialIndex;
    char storage[kSceneMaterialComboMaxEntries][kSceneMaterialNameTextCapacity];
    const char* labels[kSceneMaterialComboMaxEntries];

    for (uint32_t materialIndex = 0; materialIndex < materialCount; materialIndex++) {
        VKRT_MaterialSnapshot material = {0};
        if (VKRT_getMaterialSnapshot(vkrt, materialIndex, &material) == VKRT_SUCCESS) {
            formatMaterialLabel(storage[materialIndex], sizeof(storage[materialIndex]), &material, materialIndex);
        } else {
            snprintf(storage[materialIndex], sizeof(storage[materialIndex]), "Material %u", materialIndex);
        }
        labels[materialIndex] = storage[materialIndex];
    }

    float frameHeight = ImGui_GetFrameHeight();
    const ImGuiStyle* style = ImGui_GetStyle();
    float buttonWidth = frameHeight;
    float spacing = style ? style->ItemInnerSpacing.x : 4.0f;
    float comboWidth = ImGui_CalcItemWidth() - buttonWidth - spacing;

    ImGui_SetNextItemWidth(comboWidth);
    if (ImGui_ComboCharEx("##material_binding", &currentMaterialIndex, labels, (int)materialCount, (int)materialCount)) {
        VKRT_Result result = VKRT_setMeshMaterialIndex(vkrt, meshIndex, (uint32_t)currentMaterialIndex);
        if (result != VKRT_SUCCESS) {
            LOG_ERROR("Updating mesh material assignment failed (%d)", (int)result);
        }
    }
    ImGui_SameLine();
    if (ImGui_ButtonEx("+", (ImVec2){buttonWidth, frameHeight})) {
        char materialName[VKRT_NAME_LEN];
        const char* baseName = mesh->name[0] ? mesh->name : "Material";
        snprintf(materialName, sizeof(materialName), "%s", baseName);

        uint32_t materialIndex = VKRT_INVALID_INDEX;
        VKRT_Result result = VKRT_addMaterial(vkrt, &mesh->material, materialName, &materialIndex);
        if (result == VKRT_SUCCESS) {
            result = VKRT_setMeshMaterialIndex(vkrt, meshIndex, materialIndex);
            if (result != VKRT_SUCCESS) {
                (void)VKRT_removeMaterial(vkrt, materialIndex);
            }
        }
        if (result != VKRT_SUCCESS) {
            LOG_ERROR("Creating unique material failed (%d)", (int)result);
        }
    }
}

static void drawMaterialPropertiesEditor(VKRT* vkrt, uint32_t materialIndex, const VKRT_MaterialSnapshot* materialSnapshot) {
    if (!vkrt || !materialSnapshot) return;

    Material material = materialSnapshot->material;
    bool materialChanged = false;

    ImGui_PushID("mat_surface");
    if (ImGui_CollapsingHeader("Surface", ImGuiTreeNodeFlags_DefaultOpen)) {
        inspectorIndentSection();
        materialChanged |= ImGui_ColorEdit3("Base Color", material.baseColor, ImGuiColorEditFlags_Float);
        materialChanged |= ImGui_SliderFloatEx("Metallic", &material.metallic, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        materialChanged |= ImGui_SliderFloatEx("Roughness", &material.roughness, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        materialChanged |= ImGui_SliderFloatEx("Diffuse Roughness", &material.diffuseRoughness, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        materialChanged |= ImGui_SliderFloatEx("Subsurface", &material.subsurface, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        inspectorUnindentSection();
    }
    ImGui_PopID();

    ImGui_PushID("mat_specular");
    if (ImGui_CollapsingHeader("Specular", ImGuiTreeNodeFlags_DefaultOpen)) {
        inspectorIndentSection();
        materialChanged |= ImGui_DragFloatEx("IOR", &material.ior, 0.01f, 1.0f, 4.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        materialChanged |= ImGui_SliderFloatEx("Weight", &material.specular, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        materialChanged |= ImGui_SliderFloatEx("Tint", &material.specularTint, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        materialChanged |= ImGui_SliderFloatEx("Anisotropic", &material.anisotropic, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        inspectorUnindentSection();
    }
    ImGui_PopID();

    ImGui_PushID("mat_transmission");
    if (ImGui_CollapsingHeader("Transmission", ImGuiTreeNodeFlags_DefaultOpen)) {
        inspectorIndentSection();
        materialChanged |= ImGui_SliderFloatEx("Weight", &material.transmission, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        materialChanged |= ImGui_ColorEdit3("Attenuation Color", material.attenuationColor, ImGuiColorEditFlags_Float);
        materialChanged |= ImGui_DragFloatEx("Absorption", &material.absorptionCoefficient, 0.01f, 0.0f, VKRT_MAX_ABSORPTION_COEFFICIENT, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        inspectorUnindentSection();
    }
    ImGui_PopID();

    ImGui_PushID("mat_coating");
    if (ImGui_CollapsingHeader("Coating", ImGuiTreeNodeFlags_DefaultOpen)) {
        inspectorIndentSection();
        materialChanged |= ImGui_SliderFloatEx("Clearcoat", &material.clearcoat, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        materialChanged |= ImGui_SliderFloatEx("Clearcoat Gloss", &material.clearcoatGloss, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        materialChanged |= ImGui_SliderFloatEx("Sheen Weight", &material.sheenTintWeight[3], 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        materialChanged |= ImGui_ColorEdit3("Sheen Tint", material.sheenTintWeight, ImGuiColorEditFlags_Float);
        materialChanged |= ImGui_SliderFloatEx("Sheen Roughness", &material.sheenRoughness, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        inspectorUnindentSection();
    }
    ImGui_PopID();

    ImGui_PushID("mat_emission");
    if (ImGui_CollapsingHeader("Emission", ImGuiTreeNodeFlags_DefaultOpen)) {
        inspectorIndentSection();
        materialChanged |= ImGui_DragFloatEx("Luminance", &material.emissionLuminance, 0.1f, 0.0f, 1000000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        materialChanged |= ImGui_ColorEdit3("Color", material.emissionColor, ImGuiColorEditFlags_Float);
        inspectorUnindentSection();
    }
    ImGui_PopID();

    ImGui_PushID("mat_advanced");
    if (ImGui_CollapsingHeader("Advanced", ImGuiTreeNodeFlags_None)) {
        inspectorIndentSection();
        materialChanged |= ImGui_DragFloat3Ex("Conductor Eta", material.eta, 0.01f, 0.0f, 10.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        materialChanged |= ImGui_DragFloat3Ex("Conductor K", material.k, 0.01f, 0.0f, 10.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        inspectorUnindentSection();
    }
    ImGui_PopID();

    if (!materialChanged) return;

    VKRT_Result result = VKRT_setMaterial(vkrt, materialIndex, &material);
    if (result != VKRT_SUCCESS) {
        LOG_ERROR("Updating material failed (%d)", (int)result);
    }
}

static void drawSelectedMeshEditor(VKRT* vkrt, Session* session, uint32_t meshIndex, bool renderModeActive) {
    if (!vkrt || !session || meshIndex == VKRT_INVALID_INDEX) return;

    VKRT_MeshSnapshot mesh = {0};
    if (VKRT_getMeshSnapshot(vkrt, meshIndex, &mesh) != VKRT_SUCCESS) {
        ImGui_TextDisabled("Selected mesh is no longer available.");
        return;
    }

    drawMeshInfoHeader(vkrt, meshIndex, &mesh);

    ImGui_BeginDisabled(renderModeActive);

    ImGui_Spacing();
    ImGui_SeparatorText(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT " Transform");
    inspectorIndentSection();
    drawMeshTransformEditor(vkrt, meshIndex, &mesh);
    inspectorUnindentSection();

    ImGui_Spacing();
    ImGui_SeparatorText(ICON_FA_PALETTE " Material");
    inspectorIndentSection();
    drawMeshMaterialBindingEditor(vkrt, meshIndex, &mesh);
    inspectorUnindentSection();

    VKRT_MeshSnapshot refreshedMesh = mesh;
    if (VKRT_getMeshSnapshot(vkrt, meshIndex, &refreshedMesh) != VKRT_SUCCESS) {
        refreshedMesh = mesh;
    }

    VKRT_MaterialSnapshot material = {0};
    if (VKRT_getMaterialSnapshot(vkrt, refreshedMesh.materialIndex, &material) == VKRT_SUCCESS) {
        ImGui_Spacing();
        ImGui_SeparatorText(ICON_FA_SLIDERS " Properties");
        inspectorIndentSection();
        if (inspectorBeginKeyValueTable("##material_stats")) {
            ImGui_TableNextRow();
            ImGui_TableSetColumnIndex(0);
            ImGui_AlignTextToFramePadding();
            ImGui_TextDisabled("Name");
            ImGui_TableSetColumnIndex(1);
            char materialName[VKRT_NAME_LEN];
            snprintf(materialName, sizeof(materialName), "%s", material.name);
            if (ImGui_InputText("##material_name", materialName, sizeof(materialName), ImGuiInputTextFlags_None)) {
                VKRT_Result result = VKRT_setMaterialName(vkrt, refreshedMesh.materialIndex, materialName);
                if (result != VKRT_SUCCESS) {
                    LOG_ERROR("Updating material name failed (%d)", (int)result);
                }
            }
            inspectorEndKeyValueTable();
        }
        ImGui_Spacing();
        drawMaterialPropertiesEditor(vkrt, refreshedMesh.materialIndex, &material);
        inspectorUnindentSection();
    } else {
        ImGui_TextDisabled("Assigned material is unavailable.");
    }

    ImGui_Spacing();
    ImGui_Spacing();
    ImGui_Separator();
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

void inspectorDrawSceneBrowserSection(VKRT* vkrt, Session* session) {
    if (!vkrt || !session) return;

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
        if (drawSceneBrowserEntry(meshIndex, &mesh, isSelected)) {
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
}

void inspectorDrawSelectionTab(VKRT* vkrt, Session* session) {
    if (!vkrt || !session) return;

    VKRT_RenderStatusSnapshot status = {0};
    if (VKRT_getRenderStatus(vkrt, &status) != VKRT_SUCCESS) return;

    uint32_t selectedMeshIndex = querySelectedMeshIndex(vkrt);
    if (selectedMeshIndex == VKRT_INVALID_INDEX) {
        ImGui_TextWrapped("Click a mesh in the Scene list or left-click it in the viewport.");
        return;
    }

    drawSelectedMeshEditor(vkrt, session, selectedMeshIndex, status.renderModeActive != 0);
}
