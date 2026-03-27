#include "panel.h"

#include "IconsFontAwesome6.h"
#include "common.h"
#include "sections.h"
#include "session.h"
#include "vkrt_types.h"

#include <dcimgui.h>
#include <stdint.h>
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 5287)
#endif
#include "dcimgui_internal.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

enum {
    INSPECTOR_PROPERTIES_TAB_OBJECT = 0,
    INSPECTOR_PROPERTIES_TAB_CAMERA,
    INSPECTOR_PROPERTIES_TAB_RENDER,
};

typedef void (*InspectorTabDrawFn)(VKRT* vkrt, Session* session);

typedef struct InspectorPropertiesTab {
    const char* label;
    uint32_t index;
    InspectorTabDrawFn draw;
} InspectorPropertiesTab;

static ImVec2 queryInspectorTabPadding(void) {
    return (ImVec2){10.0f, 5.0f};
}

static void drawObjectTab(VKRT* vkrt, Session* session) {
    inspectorDrawSelectionTab(vkrt, session);
}

static void drawCameraTabContent(VKRT* vkrt, Session* session) {
    inspectorDrawCameraTab(vkrt, session);
}

static const InspectorPropertiesTab kInspectorPropertiesTabs[] = {
    {ICON_FA_CUBE " Object", INSPECTOR_PROPERTIES_TAB_OBJECT, drawObjectTab},
    {ICON_FA_VIDEO " Camera", INSPECTOR_PROPERTIES_TAB_CAMERA, drawCameraTabContent},
    {ICON_FA_IMAGES " Render", INSPECTOR_PROPERTIES_TAB_RENDER, inspectorDrawRenderTab},
};

static uint32_t queryInspectorPropertiesTabCount(void) {
    return (uint32_t)(sizeof(kInspectorPropertiesTabs) / sizeof(kInspectorPropertiesTabs[0]));
}

static void applyInspectorDockWindowClass(void) {
    ImGuiWindowClass windowClass = {0};
    windowClass.DockNodeFlagsOverrideSet =
        ImGuiDockNodeFlags_NoCloseButton | ImGuiDockNodeFlags_NoWindowMenuButton | ImGuiDockNodeFlags_NoTabBar;
    ImGui_SetNextWindowClass(&windowClass);
}

static bool beginInspectorDockWindow(const char* title) {
    if (!title) return false;
    applyInspectorDockWindowClass();
    return ImGui_Begin(title, NULL, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
}

static bool drawInspectorTabButton(const char* label, bool selected, ImVec2 size) {
    if (!label) return false;

    const ImVec4* baseColor = ImGui_GetStyleColorVec4((int)selected ? ImGuiCol_TabActive : ImGuiCol_Tab);
    const ImVec4* hoveredColor = ImGui_GetStyleColorVec4(ImGuiCol_TabHovered);
    const ImVec4* activeColor = ImGui_GetStyleColorVec4((int)selected ? ImGuiCol_TabActive : ImGuiCol_TabHovered);

    ImGui_PushStyleColorImVec4(ImGuiCol_Button, *baseColor);
    ImGui_PushStyleColorImVec4(ImGuiCol_ButtonHovered, *hoveredColor);
    ImGui_PushStyleColorImVec4(ImGuiCol_ButtonActive, *activeColor);
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_FramePadding, queryInspectorTabPadding());
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_ButtonTextAlign, (ImVec2){0.5f, 0.5f});
    bool pressed = ImGui_ButtonEx(label, size);
    ImGui_PopStyleVarEx(2);
    ImGui_PopStyleColorEx(3);
    return pressed;
}

static const InspectorPropertiesTab* queryActiveInspectorPropertiesTab(Session* session) {
    if (!session) return NULL;

    uint32_t tabCount = queryInspectorPropertiesTabCount();
    if (session->editor.propertiesPanelIndex >= tabCount) {
        session->editor.propertiesPanelIndex = INSPECTOR_PROPERTIES_TAB_OBJECT;
    }

    return &kInspectorPropertiesTabs[session->editor.propertiesPanelIndex];
}

static void drawPropertiesPanelSelector(Session* session) {
    const InspectorPropertiesTab* activeTab = queryActiveInspectorPropertiesTab(session);
    if (!activeTab) return;

    ImGuiStyle* style = ImGui_GetStyle();
    float spacing = style->ItemSpacing.x;
    float availableWidth = ImGui_GetContentRegionAvail().x;
    uint32_t tabCount = queryInspectorPropertiesTabCount();
    float buttonWidth = (availableWidth - (spacing * (float)(tabCount - 1u))) / (float)tabCount;
    if (buttonWidth < 1.0f) buttonWidth = 1.0f;

    for (uint32_t i = 0; i < tabCount; i++) {
        const InspectorPropertiesTab* tab = &kInspectorPropertiesTabs[i];
        if (i > 0) ImGui_SameLine();
        if (drawInspectorTabButton(tab->label, activeTab->index == tab->index, (ImVec2){buttonWidth, 0.0f})) {
            session->editor.propertiesPanelIndex = tab->index;
            activeTab = tab;
        }
    }

    ImGui_Dummy((ImVec2){0.0f, kInspectorSpacerHairline});
}

static void drawSceneWindowContent(VKRT* vkrt, Session* session) {
    inspectorDrawSceneOverviewSection(vkrt);
    ImGui_Dummy((ImVec2){0.0f, kInspectorControlSpacing});
    ImGui_SeparatorText(ICON_FA_CUBES " Scene");
    const char* currentScenePath = sessionGetCurrentScenePath(session);
    bool hasCurrentScenePath = currentScenePath && currentScenePath[0];
    ImGui_BeginDisabled(!hasCurrentScenePath);
    if (inspectorPaddedButton("Reset Scene")) {
        sessionQueueSceneOpen(session, currentScenePath);
    }
    ImGui_EndDisabled();
    ImGui_Dummy((ImVec2){0.0f, kInspectorControlSpacing});
    if (ImGui_BeginChild(
            "##scene_browser_list",
            (ImVec2){0.0f, 0.0f},
            ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
            ImGuiWindowFlags_None
        )) {
        inspectorDrawSceneBrowserSection(vkrt, session);
    }
    ImGui_EndChild();
}

static void drawPropertiesWindowContent(VKRT* vkrt, Session* session) {
    const InspectorPropertiesTab* activeTab = queryActiveInspectorPropertiesTab(session);
    if (!activeTab || !activeTab->draw) return;
    activeTab->draw(vkrt, session);
}

static void drawSceneWindow(VKRT* vkrt, Session* session) {
    if (!beginInspectorDockWindow("Scene Browser")) {
        ImGui_End();
        return;
    }

    drawSceneWindowContent(vkrt, session);
    ImGui_End();
}

static void drawPropertiesWindow(VKRT* vkrt, Session* session) {
    if (!beginInspectorDockWindow("Properties")) {
        ImGui_End();
        return;
    }

    drawPropertiesPanelSelector(session);
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_WindowPadding, (ImVec2){0.0f, 0.0f});
    ImGui_BeginChild("##properties_content", (ImVec2){0.0f, 0.0f}, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    drawPropertiesWindowContent(vkrt, session);
    ImGui_EndChild();
    ImGui_PopStyleVar();

    ImGui_End();
}

void inspectorDrawWorkspacePanels(VKRT* vkrt, Session* session) {
    if (!vkrt || !session) return;

    inspectorPrepareRenderState(vkrt, session);
    drawSceneWindow(vkrt, session);
    drawPropertiesWindow(vkrt, session);
}
