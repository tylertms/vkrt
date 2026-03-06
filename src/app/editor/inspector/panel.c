#include "editor_internal.h"
#include "common.h"

#include "IconsFontAwesome6.h"
#include "dcimgui.h"
#include "dcimgui_internal.h"

static void drawSceneWindow(VKRT* vkrt, Session* session) {
    ImGuiWindowClass windowClass = {0};
    windowClass.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoCloseButton | ImGuiDockNodeFlags_NoWindowMenuButton;
    ImGui_SetNextWindowClass(&windowClass);

    ImGui_PushStyleColorImVec4(ImGuiCol_WindowBg, kMenuBgColor);
    ImGui_Begin("Scene Browser", NULL, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
    ImGui_PopStyleColor();

    inspectorTightSeparatorText(ICON_FA_CUBES " Scene");
    inspectorDrawSceneBrowser(vkrt, session);

    ImGui_Dummy((ImVec2){0.0f, 6.0f});
    inspectorDrawMonitoringPanel(vkrt);

    ImGui_End();
}

static void drawPropertiesWindow(VKRT* vkrt, Session* session) {
    ImGuiWindowClass windowClass = {0};
    windowClass.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoCloseButton | ImGuiDockNodeFlags_NoWindowMenuButton;
    ImGui_SetNextWindowClass(&windowClass);

    ImGui_PushStyleColorImVec4(ImGuiCol_WindowBg, kMenuBgColor);
    ImGui_Begin("Properties", NULL, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
    ImGui_PopStyleColor();

    if (ImGui_BeginTabBar("##properties_tabs", ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_NoTooltip)) {
        ImGui_PushStyleVarX(ImGuiStyleVar_FramePadding, 5.0f);
        if (ImGui_BeginTabItem(ICON_FA_CUBE " Object", NULL, 0)) {
            ImGui_PopStyleVar();
            inspectorDrawSelectionPanel(vkrt, session);
            ImGui_EndTabItem();
        } else {
            ImGui_PopStyleVar();
        }
        ImGui_PushStyleVarX(ImGuiStyleVar_FramePadding, 5.0f);
        if (ImGui_BeginTabItem(ICON_FA_VIDEO " Camera", NULL, 0)) {
            ImGui_PopStyleVar();
            inspectorDrawCameraTab(vkrt);
            ImGui_EndTabItem();
        } else {
            ImGui_PopStyleVar();
        }
        ImGui_PushStyleVarX(ImGuiStyleVar_FramePadding, 5.0f);
        if (ImGui_BeginTabItem(ICON_FA_IMAGES " Render", NULL, 0)) {
            ImGui_PopStyleVar();
            inspectorDrawRenderTab(vkrt, session);
            ImGui_EndTabItem();
        } else {
            ImGui_PopStyleVar();
        }
        ImGui_EndTabBar();
    }

    ImGui_End();
}

void editorUIDrawWorkspacePanels(VKRT* vkrt, Session* session) {
    if (!vkrt || !session) return;
    inspectorPrepareRenderState(vkrt, session);
    drawSceneWindow(vkrt, session);
    drawPropertiesWindow(vkrt, session);
}
