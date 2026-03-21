#include "common.h"
#include "constants.h"
#include "debug.h"
#include "session.h"
#include "vkrt.h"

#include "IconsFontAwesome6.h"

#include <dcimgui.h>
#include <float.h>
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
    kSceneTextureNameTextCapacity = 192,
    kSceneMaterialComboMaxEntries = 128,
};

static const ImVec2 kTextureActionButtonPadding = {8.0f, 4.0f};
static const ImVec2 kCompactNameInputPadding = {8.0f, 4.0f};
static const float kSceneBrowserRowTopPadding = 2.0f;
static const float kSceneBrowserRowBottomPadding = 5.0f;
static const float kSceneBrowserRowLeftPadding = 8.0f;

static void formatPrefixedText(char* out, size_t outSize, const char* prefix, const char* text) {
    if (!out || outSize == 0 || !prefix || !text) return;

    size_t prefixLength = strlen(prefix);
    size_t available = outSize > prefixLength + 2u ? outSize - prefixLength - 2u : 0u;
    int textLimit = available > (size_t)INT_MAX ? INT_MAX : (int)available;
    snprintf(out, outSize, "%s %.*s", prefix, textLimit, text);
}

static bool drawCompactNameInput(const char* id, char* buffer, size_t bufferSize) {
    if (!id || !buffer || bufferSize == 0) return false;

    ImGui_PushStyleVarImVec2(ImGuiStyleVar_FramePadding, kCompactNameInputPadding);
    bool changed = ImGui_InputText(id, buffer, bufferSize, ImGuiInputTextFlags_None);
    ImGui_PopStyleVar();
    return changed;
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

static bool drawSceneBrowserEntry(uint32_t objectIndex, const char* label, int isGroup, bool isSelected, int depth) {
    if (!label) return false;

    char id[kSceneBrowserIdCapacity];
    char nameText[kSceneBrowserNameTextCapacity];
    const float rowHeight = ImGui_GetTextLineHeight() + kSceneBrowserRowTopPadding + kSceneBrowserRowBottomPadding;
    snprintf(id, sizeof(id), "##scene_%u", objectIndex);
    formatPrefixedText(nameText, sizeof(nameText), isGroup ? ICON_FA_FOLDER : ICON_FA_CUBE, label);

    const ImVec4 transparent = {0.0f, 0.0f, 0.0f, 0.0f};
    ImGui_PushStyleColorImVec4(ImGuiCol_Header, transparent);
    ImGui_PushStyleColorImVec4(ImGuiCol_HeaderHovered, transparent);
    ImGui_PushStyleColorImVec4(ImGuiCol_HeaderActive, transparent);
    bool clicked = ImGui_SelectableEx(id, isSelected, ImGuiSelectableFlags_None, (ImVec2){0.0f, rowHeight});
    ImGui_PopStyleColorEx(3);

    ImVec2 min = ImGui_GetItemRectMin();
    ImVec2 max = ImGui_GetItemRectMax();
    float textY = floorf(min.y + kSceneBrowserRowTopPadding);
    float textX = min.x + kSceneBrowserRowLeftPadding + (float)depth * 16.0f;
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

static uint32_t querySelectedSceneObjectIndex(Session* session) {
    return sessionGetSelectedSceneObject(session);
}

static void updateSelectedSceneObject(VKRT* vkrt, Session* session, uint32_t objectIndex) {
    if (!vkrt || !session) return;

    sessionSetSelectedSceneObject(session, objectIndex);
    const SessionSceneObject* object = sessionGetSceneObject(session, objectIndex);
    uint32_t meshIndex = object && object->meshIndex != VKRT_INVALID_INDEX ? object->meshIndex : VKRT_INVALID_INDEX;
    VKRT_Result result = VKRT_setSelectedMesh(vkrt, meshIndex);
    if (result != VKRT_SUCCESS) {
        LOG_ERROR("Updating selected scene object failed (%d)", (int)result);
    }
}

static void drawSceneObjectInfoHeader(Session* session, uint32_t objectIndex, const SessionSceneObject* object) {
    if (!session || !object) return;

    char name[VKRT_NAME_LEN];
    snprintf(name, sizeof(name), "%s", object->name[0] ? object->name : "(unnamed)");

    ImGui_SeparatorText(ICON_FA_CIRCLE_INFO " Object");
    inspectorIndentSection();
    if (inspectorBeginKeyValueTable("##object_stats")) {
        ImGui_TableNextRow();
        ImGui_TableSetColumnIndex(0);
        ImGui_AlignTextToFramePadding();
        ImGui_TextDisabled("Name");
        ImGui_TableSetColumnIndex(1);
        if (drawCompactNameInput("##object_name", name, sizeof(name))) {
            sessionSetSceneObjectName(session, objectIndex, name);
        }

        inspectorKeyValueRow("Type", object->meshIndex != VKRT_INVALID_INDEX ? "Mesh" : "Group");
        char childCount[32];
        snprintf(childCount, sizeof(childCount), "%u", sessionCountSceneObjectChildren(session, objectIndex));
        inspectorKeyValueRow("Children", childCount);
        inspectorEndKeyValueTable();
    }
    inspectorUnindentSection();
}

static void drawMeshInfoHeader(VKRT* vkrt, const VKRT_MeshSnapshot* mesh) {
    if (!vkrt || !mesh) return;

    uint32_t triangleCount = mesh->info.indexCount / 3;
    uint64_t vertexBytes = (uint64_t)mesh->info.vertexCount * sizeof(Vertex);
    uint64_t indexBytes = (uint64_t)mesh->info.indexCount * sizeof(uint32_t);

    char countText[kSceneStatTextCapacity];
    char sizeText[kSceneSizeTextCapacity];
    char geometryText[kSceneStatTextCapacity];
    char sourceText[kSceneSourceTextCapacity];
    VKRT_MaterialSnapshot material = {0};
    uint8_t hasEditableMaterial = mesh->hasMaterialAssignment &&
                                  mesh->materialIndex != 0u &&
                                  VKRT_getMaterialSnapshot(vkrt, mesh->materialIndex, &material) == VKRT_SUCCESS;
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
        ImGui_TextDisabled("Material");
        ImGui_TableSetColumnIndex(1);
        if (hasEditableMaterial) {
            char materialName[VKRT_NAME_LEN];
            snprintf(materialName, sizeof(materialName), "%s", material.name);
            if (drawCompactNameInput("##material_name", materialName, sizeof(materialName))) {
                VKRT_Result result = VKRT_setMaterialName(vkrt, mesh->materialIndex, materialName);
                if (result != VKRT_SUCCESS) {
                    LOG_ERROR("Updating material name failed (%d)", (int)result);
                }
            }
        } else {
            ImGui_AlignTextToFramePadding();
            ImGui_TextDisabled("None");
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

static void drawSceneObjectTransformEditor(VKRT* vkrt, Session* session, uint32_t objectIndex, const SessionSceneObject* object) {
    if (!vkrt || !session || !object) return;
    float position[3] = {object->localPosition[0], object->localPosition[1], object->localPosition[2]};
    float rotation[3] = {object->localRotation[0], object->localRotation[1], object->localRotation[2]};
    float scale[3] = {object->localScale[0], object->localScale[1], object->localScale[2]};

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
        if (!sessionSetSceneObjectLocalTransform(session, objectIndex, position, rotation, scale) ||
            !sessionSyncSceneObjectTransforms(vkrt, session)) {
            LOG_ERROR("Updating scene object transform failed");
        }
    }
}

static void formatMaterialLabel(
    char* out,
    size_t outSize,
    const VKRT_MaterialSnapshot* material,
    uint32_t visibleMaterialIndex
) {
    if (!out || outSize == 0 || !material) return;

    const char* materialName = material->name[0] ? material->name : "Material";
    int prefixLength = snprintf(out, outSize, "#%u - ", visibleMaterialIndex);
    if (prefixLength < 0 || (size_t)prefixLength >= outSize) return;

    size_t available = outSize - (size_t)prefixLength;
    int nameLimit = available > 0u
        ? (available - 1u > (size_t)INT_MAX ? INT_MAX : (int)(available - 1u))
        : 0;
    snprintf(out + prefixLength, available, "%.*s", nameLimit, materialName);
}

static void formatMaterialAssignmentPreviewLabel(
    VKRT* vkrt,
    const VKRT_MeshSnapshot* mesh,
    char* out,
    size_t outSize
) {
    if (!vkrt || !mesh || !out || outSize == 0) return;

    if (!mesh->hasMaterialAssignment || mesh->materialIndex == 0u) {
        snprintf(out, outSize, "None");
        return;
    }

    VKRT_MaterialSnapshot material = {0};
    if (VKRT_getMaterialSnapshot(vkrt, mesh->materialIndex, &material) != VKRT_SUCCESS) {
        snprintf(out, outSize, "Material #%u", mesh->materialIndex - 1u);
        return;
    }

    int visibleMaterialIndex = (int)mesh->materialIndex - 1;
    if (visibleMaterialIndex < 0) visibleMaterialIndex = 0;
    formatMaterialLabel(out, outSize, &material, (uint32_t)visibleMaterialIndex);
}

static float queryComboPopupMaxHeightFromItemCount(int itemCount) {
    if (itemCount <= 0) return FLT_MAX;

    const ImGuiStyle* style = ImGui_GetStyle();
    float itemHeight = ImGui_GetTextLineHeightWithSpacing();
    float spacing = style ? style->ItemSpacing.y : 0.0f;
    float padding = style ? style->WindowPadding.y * 2.0f : 0.0f;
    return itemHeight * (float)itemCount - spacing + padding;
}

static bool drawMaterialAssignmentCombo(
    VKRT* vkrt,
    const VKRT_MeshSnapshot* mesh,
    const char* const* labels,
    uint32_t labelCount,
    int* currentMaterialIndex
) {
    if (!vkrt || !mesh || !labels || !currentMaterialIndex) return false;

    char previewText[kSceneMaterialNameTextCapacity];
    formatMaterialAssignmentPreviewLabel(vkrt, mesh, previewText, sizeof(previewText));

    ImGui_SetNextWindowSizeConstraints(
        (ImVec2){0.0f, 0.0f},
        (ImVec2){FLT_MAX, queryComboPopupMaxHeightFromItemCount((int)labelCount)},
        NULL,
        NULL
    );
    if (!ImGui_BeginCombo("##material_binding", previewText, ImGuiComboFlags_None)) {
        return false;
    }

    bool valueChanged = false;
    for (uint32_t i = 0; i < labelCount; i++) {
        bool selected = *currentMaterialIndex == (int)i;
        if (ImGui_SelectableEx(labels[i], selected, ImGuiSelectableFlags_None, (ImVec2){0.0f, 0.0f})) {
            if (*currentMaterialIndex != (int)i) {
                *currentMaterialIndex = (int)i;
                valueChanged = true;
            }
        }
        if (selected) {
            ImGui_SetItemDefaultFocus();
        }
    }

    ImGui_EndCombo();
    return valueChanged;
}

static void drawMeshMaterialBindingEditor(VKRT* vkrt, uint32_t meshIndex, const VKRT_MeshSnapshot* mesh) {
    if (!vkrt || !mesh) return;

    uint32_t internalMaterialCount = 0;
    if (VKRT_getMaterialCount(vkrt, &internalMaterialCount) != VKRT_SUCCESS || internalMaterialCount == 0) {
        ImGui_TextDisabled("No materials available.");
        return;
    }

    uint32_t visibleMaterialCount = internalMaterialCount > 0u ? internalMaterialCount - 1u : 0u;
    uint32_t maxListedMaterials = kSceneMaterialComboMaxEntries > 0u
        ? (uint32_t)kSceneMaterialComboMaxEntries - 1u
        : 0u;
    if (visibleMaterialCount > maxListedMaterials) {
        visibleMaterialCount = maxListedMaterials;
    }

    int currentMaterialIndex = (mesh->hasMaterialAssignment && mesh->materialIndex > 0u)
        ? (int)mesh->materialIndex
        : 0;
    char storage[kSceneMaterialComboMaxEntries][kSceneMaterialNameTextCapacity];
    const char* labels[kSceneMaterialComboMaxEntries];
    snprintf(storage[0], sizeof(storage[0]), "None");
    labels[0] = storage[0];

    for (uint32_t visibleMaterialIndex = 0; visibleMaterialIndex < visibleMaterialCount; visibleMaterialIndex++) {
        uint32_t materialIndex = visibleMaterialIndex + 1u;
        VKRT_MaterialSnapshot material = {0};
        if (VKRT_getMaterialSnapshot(vkrt, materialIndex, &material) == VKRT_SUCCESS) {
            formatMaterialLabel(
                storage[visibleMaterialIndex + 1u],
                sizeof(storage[visibleMaterialIndex + 1u]),
                &material,
                visibleMaterialIndex
            );
        } else {
            snprintf(storage[visibleMaterialIndex + 1u], sizeof(storage[visibleMaterialIndex + 1u]), "Material %u", visibleMaterialIndex);
        }
        labels[visibleMaterialIndex + 1u] = storage[visibleMaterialIndex + 1u];
    }

    float frameHeight = ImGui_GetFrameHeight();
    const ImGuiStyle* style = ImGui_GetStyle();
    float buttonWidth = frameHeight;
    float spacing = style ? style->ItemInnerSpacing.x : 4.0f;
    float comboWidth = ImGui_CalcItemWidth() - buttonWidth * 2.0f - spacing * 2.0f;
    if (comboWidth < 96.0f) comboWidth = 96.0f;

    ImGui_SetNextItemWidth(comboWidth);
    if (currentMaterialIndex > (int)visibleMaterialCount) {
        currentMaterialIndex = 0;
    }
    if (drawMaterialAssignmentCombo(vkrt, mesh, labels, visibleMaterialCount + 1u, &currentMaterialIndex)) {
        VKRT_Result result = currentMaterialIndex == 0
            ? VKRT_clearMeshMaterialAssignment(vkrt, meshIndex)
            : VKRT_setMeshMaterialIndex(vkrt, meshIndex, (uint32_t)currentMaterialIndex);
        if (result != VKRT_SUCCESS) {
            LOG_ERROR("Updating mesh material assignment failed (%d)", (int)result);
        }
    }
    ImGui_SameLine();
    if (ImGui_ButtonEx(ICON_FA_PLUS, (ImVec2){buttonWidth, frameHeight})) {
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
    tooltipOnHover("Create a unique material from the current shading state and assign it.");

    uint8_t canDeleteMaterial = mesh->hasMaterialAssignment && mesh->materialIndex != 0u;
    uint32_t currentAssignedMaterialIndex = mesh->materialIndex;

    ImGui_SameLine();
    ImGui_BeginDisabled(!canDeleteMaterial);
    if (ImGui_ButtonEx(ICON_FA_TRASH, (ImVec2){buttonWidth, frameHeight})) {
        VKRT_Result result = VKRT_removeMaterial(vkrt, currentAssignedMaterialIndex);
        if (result != VKRT_SUCCESS) {
            LOG_ERROR("Deleting material failed (%d)", (int)result);
        }
    }
    if (canDeleteMaterial) {
        tooltipOnHover("Delete this material. Any meshes using it will revert to defaults.");
    } else if (mesh->hasMaterialAssignment && currentAssignedMaterialIndex == 0u) {
        tooltipOnHover("The reserved default material cannot be deleted.");
    } else {
        tooltipOnHover("No explicit material is assigned to this mesh.");
    }
    ImGui_EndDisabled();
}

static uint32_t queryMaterialTextureIndex(const Material* material, uint32_t textureSlot) {
    if (!material) return VKRT_INVALID_INDEX;

    switch (textureSlot) {
        case VKRT_MATERIAL_TEXTURE_SLOT_BASE_COLOR:
            return material->baseColorTextureIndex;
        case VKRT_MATERIAL_TEXTURE_SLOT_METALLIC_ROUGHNESS:
            return material->metallicRoughnessTextureIndex;
        case VKRT_MATERIAL_TEXTURE_SLOT_NORMAL:
            return material->normalTextureIndex;
        case VKRT_MATERIAL_TEXTURE_SLOT_EMISSIVE:
            return material->emissiveTextureIndex;
        default:
            return VKRT_INVALID_INDEX;
    }
}

static const char* queryTextureSlotLabel(uint32_t textureSlot) {
    switch (textureSlot) {
        case VKRT_MATERIAL_TEXTURE_SLOT_BASE_COLOR:
            return "Base Color";
        case VKRT_MATERIAL_TEXTURE_SLOT_METALLIC_ROUGHNESS:
            return "Metal/Rough";
        case VKRT_MATERIAL_TEXTURE_SLOT_NORMAL:
            return "Normal";
        case VKRT_MATERIAL_TEXTURE_SLOT_EMISSIVE:
            return "Emissive";
        default:
            return "Texture";
    }
}

static void formatTextureAssignmentLabel(
    VKRT* vkrt,
    uint32_t textureIndex,
    char* out,
    size_t outSize
) {
    if (!out || outSize == 0) return;

    if (textureIndex == VKRT_INVALID_INDEX) {
        snprintf(out, outSize, "N/A");
        return;
    }

    VKRT_TextureSnapshot texture = {0};
    if (VKRT_getTextureSnapshot(vkrt, textureIndex, &texture) == VKRT_SUCCESS) {
        int nameLimit = outSize > 1u
            ? (outSize - 1u > (size_t)INT_MAX ? INT_MAX : (int)(outSize - 1u))
            : 0;
        snprintf(out, outSize, "%.*s", nameLimit, texture.name[0] ? texture.name : "Texture");
        return;
    }

    snprintf(out, outSize, "Texture #%u", textureIndex);
}

static void drawMaterialTextureSlotEditor(
    VKRT* vkrt,
    Session* session,
    uint32_t materialIndex,
    uint32_t textureSlot,
    uint32_t textureIndex
) {
    if (!vkrt || !session) return;

    int hasTexture = textureIndex != VKRT_INVALID_INDEX;
    const char* actionLabel = hasTexture ? "Clear" : "Upload";
    if (!ImGui_BeginTableEx(
        "##texture_slot_value",
        2,
        ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings,
        (ImVec2){-1.0f, 0.0f},
        0.0f
    )) {
        return;
    }

    ImGui_PushStyleVarImVec2(ImGuiStyleVar_FramePadding, kTextureActionButtonPadding);
    float actionButtonWidth = ImGui_CalcTextSize(actionLabel).x + kTextureActionButtonPadding.x * 2.0f;
    ImGui_TableSetupColumnEx("Value", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoHide, 0.0f, 0);
    ImGui_TableSetupColumnEx("Action", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHide, actionButtonWidth, 0);

    ImGui_TableNextRow();
    ImGui_TableSetColumnIndex(0);
    ImGui_AlignTextToFramePadding();
    if (hasTexture) {
        char textureLabel[kSceneTextureNameTextCapacity];
        formatTextureAssignmentLabel(vkrt, textureIndex, textureLabel, sizeof(textureLabel));
        ImGui_Text("%s", textureLabel);
    } else {
        ImGui_TextDisabled("None");
    }

    ImGui_TableSetColumnIndex(1);
    bool actionPressed = ImGui_Button(actionLabel);
    ImGui_PopStyleVar();
    if (actionPressed) {
        if (hasTexture) {
            VKRT_Result result = VKRT_setMaterialTexture(vkrt, materialIndex, textureSlot, VKRT_INVALID_INDEX);
            if (result != VKRT_SUCCESS) {
                LOG_ERROR("Clearing material texture failed (%d)", (int)result);
            }
        } else {
            sessionRequestTextureImportDialog(session, materialIndex, textureSlot);
        }
    }
    ImGui_EndTable();
}

static void drawMaterialTexturesEditor(VKRT* vkrt, Session* session, uint32_t materialIndex, const VKRT_MaterialSnapshot* materialSnapshot) {
    if (!vkrt || !session || !materialSnapshot) return;

    if (!inspectorBeginCollapsingHeaderSection("Textures", ImGuiTreeNodeFlags_None)) {
        return;
    }

    inspectorIndentSection();
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_ItemSpacing, (ImVec2){6.0f, 2.0f});
    if (inspectorBeginKeyValueTable("##material_textures")) {
        const uint32_t textureSlots[] = {
            VKRT_MATERIAL_TEXTURE_SLOT_BASE_COLOR,
            VKRT_MATERIAL_TEXTURE_SLOT_METALLIC_ROUGHNESS,
            VKRT_MATERIAL_TEXTURE_SLOT_NORMAL,
            VKRT_MATERIAL_TEXTURE_SLOT_EMISSIVE,
        };

        for (uint32_t i = 0; i < VKRT_ARRAY_COUNT(textureSlots); i++) {
            uint32_t textureSlot = textureSlots[i];
            uint32_t textureIndex = queryMaterialTextureIndex(&materialSnapshot->material, textureSlot);

            ImGui_TableNextRow();
            ImGui_TableSetColumnIndex(0);
            ImGui_AlignTextToFramePadding();
            ImGui_TextDisabled("%s", queryTextureSlotLabel(textureSlot));

            ImGui_TableSetColumnIndex(1);
            char textureSlotId[32];
            snprintf(textureSlotId, sizeof(textureSlotId), "texture_slot_%u", textureSlot);
            ImGui_PushID(textureSlotId);
            drawMaterialTextureSlotEditor(vkrt, session, materialIndex, textureSlot, textureIndex);
            ImGui_PopID();
        }

        inspectorEndKeyValueTable();
    }
    ImGui_PopStyleVar();
    inspectorUnindentSection();
    inspectorEndCollapsingHeaderSection();
}

static void drawMaterialPropertiesEditor(
    VKRT* vkrt,
    Session* session,
    uint32_t materialIndex,
    const VKRT_MaterialSnapshot* materialSnapshot
) {
    if (!vkrt || !session || !materialSnapshot) return;

    Material material = materialSnapshot->material;
    bool materialChanged = false;

    ImGui_PushID("mat_surface");
    if (inspectorBeginCollapsingHeaderSection("Surface", ImGuiTreeNodeFlags_DefaultOpen)) {
        inspectorIndentSection();
        materialChanged |= ImGui_ColorEdit3("Base Color", material.baseColor, ImGuiColorEditFlags_Float);
        materialChanged |= ImGui_SliderFloatEx("Metallic", &material.metallic, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        materialChanged |= ImGui_SliderFloatEx("Roughness", &material.roughness, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        materialChanged |= ImGui_SliderFloatEx("Diffuse Roughness", &material.diffuseRoughness, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        materialChanged |= ImGui_SliderFloatEx("Subsurface", &material.subsurface, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);

        const char* alphaModes[] = {"Opaque", "Mask", "Blend"};
        int alphaMode = (int)material.alphaMode;
        if (alphaMode < 0 || alphaMode > 2) alphaMode = 0;
        if (ImGui_ComboCharEx("Alpha Mode", &alphaMode, alphaModes, 3, 3)) {
            material.alphaMode = (uint32_t)alphaMode;
            materialChanged = true;
        }
        if (material.alphaMode != VKRT_MATERIAL_ALPHA_MODE_OPAQUE) {
            materialChanged |= ImGui_SliderFloatEx("Opacity", &material.opacity, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        }
        if (material.alphaMode == VKRT_MATERIAL_ALPHA_MODE_MASK) {
            materialChanged |= ImGui_SliderFloatEx("Alpha Cutoff", &material.alphaCutoff, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        }
        inspectorUnindentSection();
        inspectorEndCollapsingHeaderSection();
    }
    ImGui_PopID();

    ImGui_PushID("mat_specular");
    if (inspectorBeginCollapsingHeaderSection("Specular", ImGuiTreeNodeFlags_DefaultOpen)) {
        inspectorIndentSection();
        materialChanged |= ImGui_DragFloatEx("IOR", &material.ior, 0.01f, 1.0f, 4.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        materialChanged |= ImGui_SliderFloatEx("Weight", &material.specular, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        materialChanged |= ImGui_SliderFloatEx("Tint", &material.specularTint, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        materialChanged |= ImGui_SliderFloatEx("Anisotropic", &material.anisotropic, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        inspectorUnindentSection();
        inspectorEndCollapsingHeaderSection();
    }
    ImGui_PopID();

    ImGui_PushID("mat_transmission");
    if (inspectorBeginCollapsingHeaderSection("Transmission", ImGuiTreeNodeFlags_DefaultOpen)) {
        inspectorIndentSection();
        materialChanged |= ImGui_SliderFloatEx("Weight", &material.transmission, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        materialChanged |= ImGui_ColorEdit3("Attenuation Color", material.attenuationColor, ImGuiColorEditFlags_Float);
        materialChanged |= ImGui_DragFloatEx("Absorption", &material.absorptionCoefficient, 0.01f, 0.0f, VKRT_MAX_ABSORPTION_COEFFICIENT, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        inspectorUnindentSection();
        inspectorEndCollapsingHeaderSection();
    }
    ImGui_PopID();

    ImGui_PushID("mat_coating");
    if (inspectorBeginCollapsingHeaderSection("Coating", ImGuiTreeNodeFlags_DefaultOpen)) {
        inspectorIndentSection();
        materialChanged |= ImGui_SliderFloatEx("Clearcoat", &material.clearcoat, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        materialChanged |= ImGui_SliderFloatEx("Clearcoat Gloss", &material.clearcoatGloss, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        materialChanged |= ImGui_SliderFloatEx("Sheen Weight", &material.sheenTintWeight[3], 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        materialChanged |= ImGui_ColorEdit3("Sheen Tint", material.sheenTintWeight, ImGuiColorEditFlags_Float);
        materialChanged |= ImGui_SliderFloatEx("Sheen Roughness", &material.sheenRoughness, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        inspectorUnindentSection();
        inspectorEndCollapsingHeaderSection();
    }
    ImGui_PopID();

    ImGui_PushID("mat_emission");
    if (inspectorBeginCollapsingHeaderSection("Emission", ImGuiTreeNodeFlags_DefaultOpen)) {
        inspectorIndentSection();
        materialChanged |= ImGui_DragFloatEx("Luminance", &material.emissionLuminance, 0.1f, 0.0f, 1000000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        materialChanged |= ImGui_ColorEdit3("Color", material.emissionColor, ImGuiColorEditFlags_Float);
        inspectorUnindentSection();
        inspectorEndCollapsingHeaderSection();
    }
    ImGui_PopID();

    ImGui_PushID("mat_textures");
    drawMaterialTexturesEditor(vkrt, session, materialIndex, materialSnapshot);
    ImGui_PopID();

    ImGui_PushID("mat_advanced");
    if (inspectorBeginCollapsingHeaderSection("Advanced", ImGuiTreeNodeFlags_None)) {
        inspectorIndentSection();
        materialChanged |= ImGui_DragFloat3Ex("Conductor Eta", material.eta, 0.01f, 0.0f, 10.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        materialChanged |= ImGui_DragFloat3Ex("Conductor K", material.k, 0.01f, 0.0f, 10.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        inspectorUnindentSection();
        inspectorEndCollapsingHeaderSection();
    }
    ImGui_PopID();

    if (!materialChanged) return;

    VKRT_Result result = VKRT_setMaterial(vkrt, materialIndex, &material);
    if (result != VKRT_SUCCESS) {
        LOG_ERROR("Updating material failed (%d)", (int)result);
    }
}

static void drawSelectedSceneObjectEditor(VKRT* vkrt, Session* session, uint32_t objectIndex, bool renderModeActive) {
    if (!vkrt || !session || objectIndex == VKRT_INVALID_INDEX) return;

    const SessionSceneObject* object = sessionGetSceneObject(session, objectIndex);
    if (!object) {
        ImGui_TextDisabled("Selected object is no longer available.");
        return;
    }

    drawSceneObjectInfoHeader(session, objectIndex, object);

    uint32_t meshIndex = object->meshIndex;
    VKRT_MeshSnapshot mesh = {0};
    int hasMesh = meshIndex != VKRT_INVALID_INDEX &&
        VKRT_getMeshSnapshot(vkrt, meshIndex, &mesh) == VKRT_SUCCESS;
    if (hasMesh) {
        drawMeshInfoHeader(vkrt, &mesh);
    }

    ImGui_BeginDisabled(renderModeActive);

    ImGui_Spacing();
    ImGui_SeparatorText(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT " Transform");
    inspectorIndentSection();
    drawSceneObjectTransformEditor(vkrt, session, objectIndex, object);
    inspectorUnindentSection();

    if (hasMesh) {
        ImGui_Spacing();
        ImGui_SeparatorText(ICON_FA_PALETTE " Material");
        inspectorIndentSection();
        drawMeshMaterialBindingEditor(vkrt, meshIndex, &mesh);
        inspectorUnindentSection();
    }

    VKRT_MeshSnapshot refreshedMesh = mesh;
    if (hasMesh && VKRT_getMeshSnapshot(vkrt, meshIndex, &refreshedMesh) != VKRT_SUCCESS) {
        refreshedMesh = mesh;
    }

    if (hasMesh) {
        inspectorIndentSection();

        if (!refreshedMesh.hasMaterialAssignment || refreshedMesh.materialIndex == 0u) {
            ImGui_Spacing();
            ImGui_TextDisabled("No material assigned. Click + to create one.");
        } else {
            VKRT_MaterialSnapshot material = {0};
            if (VKRT_getMaterialSnapshot(vkrt, refreshedMesh.materialIndex, &material) == VKRT_SUCCESS) {
                ImGui_Spacing();
                drawMaterialPropertiesEditor(vkrt, session, refreshedMesh.materialIndex, &material);
            } else {
                ImGui_TextDisabled("Assigned material is unavailable.");
            }
        }

        inspectorUnindentSection();
    }

    ImGui_Spacing();
    ImGui_Spacing();
    ImGui_Separator();
    ImGui_Spacing();
    inspectorIndentSection();
    if (inspectorPaddedButton(ICON_FA_TRASH " Remove Object")) {
        sessionQueueSceneObjectRemoval(session, objectIndex);
    }
    tooltipOnHover(hasMesh
        ? "Remove this object and any child objects from the scene."
        : "Remove this group and all child objects from the scene.");

    ImGui_EndDisabled();

    if (renderModeActive) {
        ImGui_Spacing();
        ImGui_TextDisabled("Scene editing is locked while rendering.");
    }
    inspectorUnindentSection();
}

static const int kMaxTreeDepth = 64;

static void drawSceneObjectBrowserTree(VKRT* vkrt, Session* session, uint32_t objectIndex, uint32_t selectedObjectIndex, int depth) {
    if (depth >= kMaxTreeDepth) return;
    const SessionSceneObject* object = sessionGetSceneObject(session, objectIndex);
    if (!object) return;

    const char* label = object->name[0] ? object->name : "(unnamed)";
    bool isSelected = selectedObjectIndex == objectIndex;
    if (drawSceneBrowserEntry(objectIndex, label, object->meshIndex == VKRT_INVALID_INDEX, isSelected, depth)) {
        updateSelectedSceneObject(vkrt, session, isSelected ? VKRT_INVALID_INDEX : objectIndex);
        selectedObjectIndex = isSelected ? VKRT_INVALID_INDEX : objectIndex;
    }

    uint32_t objectCount = sessionGetSceneObjectCount(session);
    for (uint32_t childIndex = 0; childIndex < objectCount; childIndex++) {
        const SessionSceneObject* child = sessionGetSceneObject(session, childIndex);
        if (!child || child->parentIndex != objectIndex) continue;
        drawSceneObjectBrowserTree(vkrt, session, childIndex, sessionGetSelectedSceneObject(session), depth + 1);
    }
}

void inspectorDrawSceneBrowserSection(VKRT* vkrt, Session* session) {
    if (!vkrt || !session) return;

    uint32_t objectCount = sessionGetSceneObjectCount(session);
    uint32_t selectedObjectIndex = querySelectedSceneObjectIndex(session);

    ImGui_PushStyleVarImVec2(ImGuiStyleVar_ItemSpacing, (ImVec2){6.0f, 2.0f});
    for (uint32_t objectIndex = 0; objectIndex < objectCount; objectIndex++) {
        const SessionSceneObject* object = sessionGetSceneObject(session, objectIndex);
        if (!object || object->parentIndex != VKRT_INVALID_INDEX) continue;
        drawSceneObjectBrowserTree(vkrt, session, objectIndex, selectedObjectIndex, 0);
    }
    ImGui_PopStyleVar();

    if (objectCount == 0) {
        ImGui_TextDisabled("No scene objects. Use File > Import Mesh.");
    }
}

void inspectorDrawSelectionTab(VKRT* vkrt, Session* session) {
    if (!vkrt || !session) return;

    VKRT_RenderStatusSnapshot status = {0};
    if (VKRT_getRenderStatus(vkrt, &status) != VKRT_SUCCESS) return;

    uint32_t selectedMeshIndex = VKRT_INVALID_INDEX;
    VKRT_getSelectedMesh(vkrt, &selectedMeshIndex);
    if (selectedMeshIndex != session->runtime.lastSyncedSelectedMeshIndex) {
        session->runtime.lastSyncedSelectedMeshIndex = selectedMeshIndex;
        if (selectedMeshIndex != VKRT_INVALID_INDEX) {
            sessionSelectSceneObjectForMesh(session, selectedMeshIndex);
        }
    }

    uint32_t selectedObjectIndex = querySelectedSceneObjectIndex(session);
    if (selectedObjectIndex == VKRT_INVALID_INDEX) {
        ImGui_TextWrapped("Click an object in the Scene list or left-click a mesh in the viewport.");
        return;
    }

    drawSelectedSceneObjectEditor(vkrt, session, selectedObjectIndex, status.renderModeActive != 0);
}
