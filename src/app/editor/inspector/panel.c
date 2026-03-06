#include "editor_internal.h"
#include "common.h"

#include "IconsFontAwesome6.h"
#include "dcimgui.h"
#include "dcimgui_internal.h"

void editorUIDrawSceneInspector(VKRT* vkrt, Session* session) {
    if (!vkrt || !session) return;
    const char* currentTabLabel = "Config";

    ImGuiWindowClass inspectorWindowClass = {0};
    inspectorWindowClass.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoTabBar |
        ImGuiDockNodeFlags_NoWindowMenuButton |
        ImGuiDockNodeFlags_NoCloseButton;
    ImGui_SetNextWindowClass(&inspectorWindowClass);

    ImGui_PushStyleColorImVec4(ImGuiCol_WindowBg, kMenuBgColor);
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_WindowPadding, (ImVec2){0.0f, 8.0f});
    ImGui_Begin("Scene Inspector", NULL, 0);
    ImGui_PopStyleVar();
    ImGui_PopStyleColor();

    static int currentTab = INSPECTOR_TAB_MAIN;
    const VerticalIconTab tabs[] = {
        {ICON_FA_GEAR, "Config"},
        {ICON_FA_CAMERA_RETRO, "Camera"},
        {ICON_FA_IMAGES, "Render"},
        {ICON_FA_FOLDER_PLUS, "Scene"},
    };

    ImVec2 defaultSpacing = ImGui_GetStyle()->ItemSpacing;
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_ItemSpacing, (ImVec2){0.0f, defaultSpacing.y});
    drawVerticalIconTabs(tabs, IM_ARRAYSIZE(tabs), &currentTab);
    syncInspectorDockWidthForTabState(currentTab);
    if (currentTab >= 0) {
        ImGui_SameLine();
        drawInspectorVerticalDivider();
        ImGui_SameLine();
        ImGui_Dummy((ImVec2){10.0f, 0.0f});
        ImGui_SameLine();
    }
    ImGui_PopStyleVar();

    if (currentTab >= 0) {
        float rightMargin = 10.0f;
        float pageWidth = ImGui_GetContentRegionAvail().x - rightMargin;
        if (pageWidth < 1.0f) pageWidth = 1.0f;
        float inspectorScrollbarSize = ImGui_GetStyle()->ScrollbarSize * 0.72f;
        if (inspectorScrollbarSize < 6.0f) inspectorScrollbarSize = 6.0f;

        ImGui_PushStyleVarImVec2(ImGuiStyleVar_WindowPadding, (ImVec2){7.0f, 5.0f});
        ImGui_PushStyleVar(ImGuiStyleVar_ScrollbarSize, inspectorScrollbarSize);
        ImGui_BeginChild("##inspector_page", (ImVec2){pageWidth, 0.0f},
            ImGuiChildFlags_None,
            ImGuiWindowFlags_NoBackground);

        switch ((InspectorTab)currentTab) {
        case INSPECTOR_TAB_MAIN:
            currentTabLabel = "Config";
            break;
        case INSPECTOR_TAB_CAMERA:
            currentTabLabel = "Camera";
            break;
        case INSPECTOR_TAB_RENDER:
            currentTabLabel = "Render";
            break;
        case INSPECTOR_TAB_SCENE:
            currentTabLabel = "Scene";
            break;
        default:
            currentTabLabel = "Config";
            break;
        }
        ImGui_SeparatorText(currentTabLabel);

        ImGui_PushStyleVar(ImGuiStyleVar_IndentSpacing, kInspectorSectionIndent);
        switch ((InspectorTab)currentTab) {
        case INSPECTOR_TAB_MAIN:
            inspectorDrawConfigTab(vkrt);
            break;
        case INSPECTOR_TAB_CAMERA:
            inspectorDrawCameraTab(vkrt);
            break;
        case INSPECTOR_TAB_RENDER:
            inspectorDrawRenderTab(vkrt, session);
            break;
        case INSPECTOR_TAB_SCENE:
            inspectorDrawSceneTab(vkrt, session);
            break;
        default:
            inspectorDrawConfigTab(vkrt);
            break;
        }
        ImGui_PopStyleVar();
        ImGui_EndChild();
        ImGui_PopStyleVarEx(2);
    }

    ImGui_End();
}
