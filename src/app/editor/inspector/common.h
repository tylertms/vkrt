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

void inspectorDrawConfigTab(VKRT* vkrt);
void inspectorDrawCameraTab(VKRT* vkrt);
void inspectorDrawRenderTab(VKRT* vkrt, Session* session);
void inspectorDrawSceneTab(VKRT* vkrt, Session* session);
