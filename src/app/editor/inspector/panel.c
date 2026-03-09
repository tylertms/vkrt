#include "editor_internal.h"
#include "common.h"

#include "IconsFontAwesome6.h"
#include "dcimgui.h"
#include "dcimgui_internal.h"

enum {
    PROPERTIES_PANEL_OBJECT = 0,
    PROPERTIES_PANEL_CAMERA,
    PROPERTIES_PANEL_RENDER,
    PROPERTIES_PANEL_COUNT
};

static ImVec2 queryPropertiesTabPadding(void) {
    return (ImVec2){10.0f, 5.0f};
}

static bool drawPropertiesPanelButton(const char* label, bool selected, ImVec2 size) {
    if (!label) return false;

    const ImVec4* baseColor = ImGui_GetStyleColorVec4(selected ? ImGuiCol_TabActive : ImGuiCol_Tab);
    const ImVec4* hoveredColor = ImGui_GetStyleColorVec4(ImGuiCol_TabHovered);
    const ImVec4* activeColor = ImGui_GetStyleColorVec4(selected ? ImGuiCol_TabActive : ImGuiCol_TabHovered);

    ImGui_PushStyleColorImVec4(ImGuiCol_Button, *baseColor);
    ImGui_PushStyleColorImVec4(ImGuiCol_ButtonHovered, *hoveredColor);
    ImGui_PushStyleColorImVec4(ImGuiCol_ButtonActive, *activeColor);
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_FramePadding, queryPropertiesTabPadding());
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_ButtonTextAlign, (ImVec2){0.5f, 0.5f});
    bool pressed = ImGui_ButtonEx(label, size);
    ImGui_PopStyleVarEx(2);
    ImGui_PopStyleColorEx(3);
    return pressed;
}

static void drawPropertiesPanelSelector(Session* session) {
    if (!session) return;

    ImGuiStyle* style = ImGui_GetStyle();
    float spacing = style->ItemSpacing.x;
    float availableWidth = ImGui_GetContentRegionAvail().x;
    float buttonWidth = (availableWidth - spacing * (float)(PROPERTIES_PANEL_COUNT - 1)) / (float)PROPERTIES_PANEL_COUNT;
    if (buttonWidth < 1.0f) buttonWidth = 1.0f;

    const struct {
        const char* label;
        uint32_t index;
    } buttons[PROPERTIES_PANEL_COUNT] = {
        {ICON_FA_CUBE " Object", PROPERTIES_PANEL_OBJECT},
        {ICON_FA_VIDEO " Camera", PROPERTIES_PANEL_CAMERA},
        {ICON_FA_IMAGES " Render", PROPERTIES_PANEL_RENDER},
    };

    if (session->editor.propertiesPanelIndex >= PROPERTIES_PANEL_COUNT) {
        session->editor.propertiesPanelIndex = PROPERTIES_PANEL_OBJECT;
    }

    for (uint32_t i = 0; i < PROPERTIES_PANEL_COUNT; i++) {
        if (i > 0) ImGui_SameLine();
        if (drawPropertiesPanelButton(buttons[i].label,
                session->editor.propertiesPanelIndex == buttons[i].index,
                (ImVec2){buttonWidth, 0.0f})) {
            session->editor.propertiesPanelIndex = buttons[i].index;
        }
    }

    ImGui_Dummy((ImVec2){0.0f, kInspectorSpacerHairline});
}

static void drawSceneWindow(VKRT* vkrt, Session* session) {
    ImGuiWindowClass windowClass = {0};
    windowClass.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoCloseButton |
                                           ImGuiDockNodeFlags_NoWindowMenuButton |
                                           ImGuiDockNodeFlags_NoTabBar;
    ImGui_SetNextWindowClass(&windowClass);

    ImGui_PushStyleColorImVec4(ImGuiCol_WindowBg, kMenuBgColor);
    ImGui_Begin("Scene Browser", NULL, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
    ImGui_PopStyleColor();

    inspectorTightSeparatorText(ICON_FA_CUBES " Scene");
    inspectorDrawSceneBrowser(vkrt, session);

    ImGui_Dummy((ImVec2){0.0f, kInspectorSpacerMedium});
    inspectorDrawMonitoringPanel(vkrt);

    ImGui_End();
}

static void drawPropertiesWindow(VKRT* vkrt, Session* session) {
    ImGuiWindowClass windowClass = {0};
    windowClass.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoCloseButton |
                                           ImGuiDockNodeFlags_NoWindowMenuButton |
                                           ImGuiDockNodeFlags_NoTabBar;
    ImGui_SetNextWindowClass(&windowClass);

    ImGui_PushStyleColorImVec4(ImGuiCol_WindowBg, kMenuBgColor);
    ImGui_Begin("Properties", NULL, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
    ImGui_PopStyleColor();

    drawPropertiesPanelSelector(session);
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_WindowPadding, (ImVec2){0.0f, 0.0f});
    ImGui_BeginChild("##properties_content", (ImVec2){0.0f, 0.0f}, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);

    switch (session->editor.propertiesPanelIndex) {
        case PROPERTIES_PANEL_CAMERA:
            inspectorDrawCameraTab(vkrt);
            break;
        case PROPERTIES_PANEL_RENDER:
            inspectorDrawRenderTab(vkrt, session);
            break;
        case PROPERTIES_PANEL_OBJECT:
        default:
            inspectorDrawSelectionPanel(vkrt, session);
            break;
    }

    ImGui_EndChild();
    ImGui_PopStyleVar();

    ImGui_End();
}

void editorUIDrawWorkspacePanels(VKRT* vkrt, Session* session) {
    if (!vkrt || !session) return;
    inspectorPrepareRenderState(vkrt, session);
    drawSceneWindow(vkrt, session);
    drawPropertiesWindow(vkrt, session);
}
