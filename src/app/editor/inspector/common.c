#include "common.h"

#include "dcimgui_internal.h"

#include <math.h>
#include <stdio.h>

const float kInspectorControlSpacing = 1.0f;
const float kInspectorSectionIndent = 10.0f;
const ImVec4 kMenuBgColor = {0.10f, 0.10f, 0.10f, 1.00f};
const ImVec4 kProgressBgColor = {0.16f, 0.16f, 0.16f, 1.00f};
const ImVec4 kProgressFillColor = {0.34f, 0.34f, 0.34f, 1.00f};
const ImVec4 kProgressTextColor = {0.97f, 0.97f, 0.97f, 1.00f};

static const float kInspectorCollapsedWidth = 44.0f;
static const float kInspectorMinExpandedWidth = 140.0f;
static const ImVec2 kTooltipPadding = {8.0f, 4.0f};
static const float kInspectorTabBarWidth = 44.0f;

static bool drawCenteredIconButton(const char* icon, ImVec2 size, bool selected) {
    if (!icon) return false;

    ImVec2 position = ImGui_GetCursorScreenPos();
    bool pressed = ImGui_InvisibleButton(icon, size, ImGuiButtonFlags_None);
    bool hovered = ImGui_IsItemHovered(ImGuiHoveredFlags_None);
    bool held = ImGui_IsItemActive();

    ImU32 backgroundColor = 0;
    if (held) {
        backgroundColor = ImGui_GetColorU32(ImGuiCol_ButtonActive);
    } else if (hovered) {
        backgroundColor = ImGui_GetColorU32(ImGuiCol_ButtonHovered);
    } else if (selected) {
        backgroundColor = ImGui_GetColorU32(ImGuiCol_Button);
    }

    if (backgroundColor != 0) {
        ImGui_RenderFrameEx(position,
            (ImVec2){position.x + size.x, position.y + size.y},
            backgroundColor,
            false,
            ImGui_GetStyle()->FrameRounding);
    }

    ImVec2 textSize = ImGui_CalcTextSize(icon);
    ImVec2 textPosition = {
        floorf(position.x + (size.x - textSize.x) * 0.5f),
        floorf(position.y + (size.y - textSize.y) * 0.5f),
    };
    ImDrawList_AddTextEx(ImGui_GetWindowDrawList(),
        textPosition,
        ImGui_GetColorU32Ex(ImGuiCol_Text, 1.0f),
        icon,
        NULL);
    return pressed;
}

float queryInspectorInputWidth(float preferredWidth, float labelReserve) {
    float available = ImGui_GetContentRegionAvail().x;
    float width = available - labelReserve;
    if (width < 96.0f) width = available * 0.58f;
    if (width < 72.0f) width = 72.0f;
    if (width > preferredWidth) width = preferredWidth;
    return width;
}

void formatDriverVersionText(uint32_t vendorID, uint32_t driverVersion, char* out, size_t outSize) {
    if (!out || outSize == 0) return;

    if (vendorID == 0x10DEu) {
        uint32_t major = (driverVersion >> 22u) & 0x3ffu;
        uint32_t minor = (driverVersion >> 14u) & 0xffu;
        snprintf(out, outSize, "%u.%02u", major, minor);
        return;
    }

    if (vendorID == 0x8086u) {
        uint32_t major = driverVersion >> 14u;
        uint32_t minor = driverVersion & 0x3fffu;
        if (major > 0 && minor > 0) {
            snprintf(out, outSize, "%u.%u", major, minor);
            return;
        }
    }

    snprintf(out, outSize, "%u.%u.%u",
        VK_API_VERSION_MAJOR(driverVersion),
        VK_API_VERSION_MINOR(driverVersion),
        VK_API_VERSION_PATCH(driverVersion));
}

void drawPaddedTooltip(const char* text) {
    if (!text || !text[0]) return;
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_WindowPadding, kTooltipPadding);
    ImGui_BeginTooltip();
    ImGui_Text("%s", text);
    ImGui_EndTooltip();
    ImGui_PopStyleVar();
}

void tooltipOnHover(const char* text) {
    if (!text) return;
    if (ImGui_IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
        drawPaddedTooltip(text);
    }
}

void drawVerticalIconTabs(const VerticalIconTab* tabs, int tabCount, int* currentTab) {
    const float barWidth = kInspectorTabBarWidth;
    const float btnSize = 30.f;
    const ImVec2 iconSize = {btnSize, btnSize};

    if (!tabs || tabCount <= 0 || !currentTab) return;
    if (*currentTab < -1 || *currentTab >= tabCount) *currentTab = 0;

    ImGui_PushStyleVarImVec2(ImGuiStyleVar_WindowPadding, (ImVec2){0.0f, 2.0f});
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_ItemSpacing, (ImVec2){0.0f, 6.0f});
    ImGui_BeginChild("##icon_bar", (ImVec2){barWidth, 0},
        ImGuiChildFlags_None,
        ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse);

    for (int i = 0; i < tabCount; i++) {
        ImGui_PushIDInt(i);
        bool selected = (*currentTab == i);

        ImGui_SetCursorPosX((barWidth - btnSize) * 0.5f);
        bool pressed = drawCenteredIconButton(tabs[i].icon, iconSize, selected);
        if (pressed) *currentTab = selected ? -1 : i;

        if (ImGui_IsItemHovered(ImGuiHoveredFlags_None) && tabs[i].tooltip) {
            drawPaddedTooltip(tabs[i].tooltip);
        }

        ImGui_PopID();
    }

    ImGui_EndChild();
    ImGui_PopStyleVarEx(2);
}

void drawInspectorVerticalDivider(void) {
    ImVec2 cursor = ImGui_GetCursorScreenPos();
    float height = ImGui_GetContentRegionAvail().y;
    ImDrawList_AddLineEx(ImGui_GetWindowDrawList(),
        (ImVec2){cursor.x + 0.5f, cursor.y},
        (ImVec2){cursor.x + 0.5f, cursor.y + height},
        ImGui_GetColorU32(ImGuiCol_Separator),
        1.0f);
    ImGui_Dummy((ImVec2){1.0f, height});
}

void syncInspectorDockWidthForTabState(int* currentTab) {
    static bool wasCollapsed = false;
    static float expandedWidth = 0.0f;

    if (!currentTab) return;

    ImGuiDockNode* inspectorNode = ImGui_GetWindowDockNode();
    if (!inspectorNode) return;

    ImGuiDockNode* parentNode = inspectorNode->ParentNode;
    if (!parentNode || parentNode->SplitAxis != ImGuiAxis_X) return;

    ImGuiDockNode* siblingNode = NULL;
    if (parentNode->ChildNodes[0] == inspectorNode) siblingNode = parentNode->ChildNodes[1];
    else if (parentNode->ChildNodes[1] == inspectorNode) siblingNode = parentNode->ChildNodes[0];
    if (!siblingNode) return;

    float totalWidth = parentNode->Size.x;
    if (totalWidth <= 2.0f) return;

    bool collapsed = *currentTab < 0;
    if (!collapsed &&
        !wasCollapsed &&
        expandedWidth <= 0.0f &&
        inspectorNode->Size.x <= (kInspectorCollapsedWidth + 2.0f)) {
        *currentTab = -1;
        collapsed = true;
    }

    if (!collapsed && expandedWidth <= 0.0f) {
        expandedWidth = inspectorNode->Size.x;
    }

    if (collapsed) {
        if (!wasCollapsed) {
            expandedWidth = inspectorNode->Size.x;
        }

        float collapsedWidth = kInspectorCollapsedWidth;
        if (collapsedWidth < 1.0f) collapsedWidth = 1.0f;
        if (collapsedWidth > totalWidth - 1.0f) collapsedWidth = totalWidth - 1.0f;

        inspectorNode->SizeRef.x = collapsedWidth;
        siblingNode->SizeRef.x = totalWidth - collapsedWidth;
        parentNode->WantLockSizeOnce = true;
        wasCollapsed = true;
        return;
    }

    if (wasCollapsed) {
        float restoredWidth = expandedWidth;
        if (restoredWidth < kInspectorMinExpandedWidth) restoredWidth = totalWidth * 0.26f;
        if (restoredWidth > totalWidth - 1.0f) restoredWidth = totalWidth - 1.0f;

        inspectorNode->SizeRef.x = restoredWidth;
        siblingNode->SizeRef.x = totalWidth - restoredWidth;
        parentNode->WantLockSizeOnce = true;
        wasCollapsed = false;
    }
}

uint32_t clampRenderDimension(int value) {
    if (value < 1) return 1;
    if (value > 16384) return 16384;
    return (uint32_t)value;
}

void formatTime(float seconds, char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    if (seconds <= 0.0f) {
        snprintf(out, outSize, "0ms");
        return;
    }

    uint64_t totalMs = (uint64_t)(seconds * 1000.0f + 0.5f);
    if (totalMs == 0) {
        snprintf(out, outSize, "0ms");
        return;
    }

    static const uint64_t unitMs[] = {86400000ull, 3600000ull, 60000ull, 1000ull, 1ull};
    static const char* unitLabels[] = {"d", "h", "m", "s", "ms"};

    int firstUnit = -1;
    uint64_t firstValue = 0;
    uint64_t remainder = totalMs;
    for (int i = 0; i < 5; i++) {
        uint64_t value = remainder / unitMs[i];
        if (value == 0) continue;
        firstUnit = i;
        firstValue = value;
        remainder %= unitMs[i];
        break;
    }

    if (firstUnit < 0) {
        snprintf(out, outSize, "0ms");
        return;
    }

    int secondUnit = -1;
    uint64_t secondValue = 0;
    for (int i = firstUnit + 1; i < 5; i++) {
        uint64_t value = remainder / unitMs[i];
        if (value == 0) continue;
        secondUnit = i;
        secondValue = value;
        break;
    }

    if (secondUnit >= 0) {
        snprintf(out, outSize, "%llu%s %llu%s",
            (unsigned long long)firstValue, unitLabels[firstUnit],
            (unsigned long long)secondValue, unitLabels[secondUnit]);
    } else {
        snprintf(out, outSize, "%llu%s",
            (unsigned long long)firstValue, unitLabels[firstUnit]);
    }
}
