#pragma once

#include "session.h"
#include "vkrt.h"
#include "dcimgui.h"

#include <stdbool.h>

typedef struct VerticalIconTab {
    const char* icon;
    const char* tooltip;
} VerticalIconTab;

typedef enum InspectorTab {
    INSPECTOR_TAB_MAIN = 0,
    INSPECTOR_TAB_CAMERA,
    INSPECTOR_TAB_RENDER,
    INSPECTOR_TAB_SCENE,
    INSPECTOR_TAB_COUNT
} InspectorTab;

typedef struct EditorFrameData {
    VKRT_PublicState state;
    VKRT_RuntimeSnapshot runtime;
    VKRT_SystemInfo system;
} EditorFrameData;

extern const float kInspectorControlSpacing;
extern const float kInspectorSectionIndent;
extern const ImVec4 kMenuBgColor;
extern const ImVec4 kProgressBgColor;
extern const ImVec4 kProgressFillColor;
extern const ImVec4 kProgressTextColor;

float queryInspectorInputWidth(float preferredWidth, float labelReserve);
void formatDriverVersionText(uint32_t vendorID, uint32_t driverVersion, char* out, size_t outSize);
void drawVerticalIconTabs(const VerticalIconTab* tabs, int tabCount, int* currentTab);
void drawPaddedTooltip(const char* text);
void tooltipOnHover(const char* text);
void drawInspectorVerticalDivider(void);
void syncInspectorDockWidthForTabState(int currentTab);
uint32_t clampRenderDimension(int value);
void formatTime(float seconds, char* out, size_t outSize);

void inspectorDrawConfigTab(VKRT* vkrt, const EditorFrameData* frame, bool renderModeActive);
void inspectorDrawCameraTab(VKRT* vkrt, const VKRT_PublicState* state, bool renderModeActive);
void inspectorDrawRenderTab(VKRT* vkrt, Session* session, const VKRT_PublicState* state, const VKRT_RuntimeSnapshot* runtime);
void inspectorDrawSceneTab(VKRT* vkrt, Session* session, bool renderModeActive);
