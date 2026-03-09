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
extern const float kInspectorSpacerHairline;
extern const float kInspectorSpacerMedium;
extern const ImVec4 kMenuBgColor;
extern const ImVec4 kProgressBgColor;
extern const ImVec4 kProgressFillColor;
extern const ImVec4 kProgressTextColor;

void formatDriverVersionText(uint32_t vendorID, uint32_t driverVersion, char* out, size_t outSize);
void formatByteSize(uint64_t bytes, char* out, size_t outSize);
void inspectorIndentSection(void);
void inspectorUnindentSection(void);
void inspectorPushWidgetSpacing(void);
void inspectorPopWidgetSpacing(void);
void inspectorTightSeparatorText(const char* label);
bool inspectorBeginKeyValueTable(const char* id);
void inspectorKeyValueRow(const char* label, const char* value);
void inspectorEndKeyValueTable(void);
bool inspectorPaddedButton(const char* label);
void drawVerticalIconTabs(const VerticalIconTab* tabs, int tabCount, int* currentTab);
void drawPaddedTooltip(const char* text);
void tooltipOnHover(const char* text);
void drawInspectorVerticalDivider(void);
void syncInspectorDockWidthForTabState(int* currentTab);
uint32_t clampRenderDimension(int value);
void formatTime(float seconds, char* out, size_t outSize);

void inspectorDrawMonitoringPanel(VKRT* vkrt);
void inspectorDrawCameraTab(VKRT* vkrt);
void inspectorPrepareRenderState(VKRT* vkrt, Session* session);
void inspectorDrawRenderTab(VKRT* vkrt, Session* session);
void inspectorDrawSceneBrowser(VKRT* vkrt, Session* session);
void inspectorDrawSelectionPanel(VKRT* vkrt, Session* session);
