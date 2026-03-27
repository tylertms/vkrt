#include "common.h"

#include "vulkan/vulkan_core.h"

#include <dcimgui.h>
#include <stdint.h>
#include <stdio.h>

const float kInspectorControlSpacing = 4.0f;
const float kInspectorSectionIndent = 12.0f;
const float kInspectorSpacerHairline = 1.0f;
const float kInspectorSpacerMedium = 6.0f;
static const ImVec2 kTooltipPadding = {8.0f, 4.0f};
static const ImVec2 kInspectorTableCellPadding = {4.0f, 0.0f};
static const float kCollapsingHeaderBottomSpacing = 2.0f;

void inspectorIndentSection(void) {
    ImGui_IndentEx(kInspectorSectionIndent);
}

void inspectorUnindentSection(void) {
    ImGui_UnindentEx(kInspectorSectionIndent);
}

bool inspectorBeginCollapsingHeaderSection(const char* label, ImGuiTreeNodeFlags flags) {
    if (!label) return false;
    return ImGui_CollapsingHeader(label, flags);
}

void inspectorEndCollapsingHeaderSection(void) {
    ImGui_Dummy((ImVec2){0.0f, kCollapsingHeaderBottomSpacing});
}

bool inspectorBeginKeyValueTableWithCellPadding(const char* tableId, ImVec2 cellPadding) {
    if (!tableId) return false;

    ImGui_PushStyleVarImVec2(ImGuiStyleVar_CellPadding, cellPadding);
    ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings;
    if (!ImGui_BeginTableEx(tableId, 2, flags, (ImVec2){-1.0f, 0.0f}, 0.0f)) {
        ImGui_PopStyleVar();
        return false;
    }

    const ImGuiStyle* style = ImGui_GetStyle();
    float labelWidth = ImGui_CalcTextSize("Accumulation").x;
    if (style) {
        labelWidth += style->CellPadding.x * 2.0f;
    }
    if (labelWidth < 86.0f) labelWidth = 86.0f;

    ImGui_TableSetupColumnEx("Label", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHide, labelWidth, 0);
    ImGui_TableSetupColumnEx("Value", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoHide, 0.0f, 0);
    return true;
}

bool inspectorBeginKeyValueTable(const char* tableId) {
    return inspectorBeginKeyValueTableWithCellPadding(tableId, kInspectorTableCellPadding);
}

void inspectorKeyValueRow(const char* label, const char* value) {
    if (!label || !value) return;

    ImGui_TableNextRow();
    ImGui_TableSetColumnIndex(0);
    ImGui_AlignTextToFramePadding();
    ImGui_TextDisabled("%s", label);
    ImGui_TableSetColumnIndex(1);
    ImGui_AlignTextToFramePadding();
    ImGui_PushTextWrapPos(0.0f);
    ImGui_TextUnformatted(value);
    ImGui_PopTextWrapPos();
}

void inspectorEndKeyValueTable(void) {
    ImGui_EndTable();
    ImGui_PopStyleVar();
}

bool inspectorPaddedButton(const char* label) {
    if (!label) return false;
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_FramePadding, (ImVec2){8.0f, 5.0f});
    bool pressed = ImGui_Button(label);
    ImGui_PopStyleVar();
    return pressed;
}

void formatDriverVersionText(uint32_t vendorID, uint32_t driverVersion, char* out, size_t outSize) {
    if (!out || outSize == 0) return;

    if (vendorID == 0x10DEu) {  // NVIDIA
        uint32_t major = (driverVersion >> 22u) & 0x3ffu;
        uint32_t minor = (driverVersion >> 14u) & 0xffu;
        snprintf(out, outSize, "%u.%02u", major, minor);
        return;
    }

    if (vendorID == 0x8086u) {  // INTEL
        uint32_t major = driverVersion >> 14u;
        uint32_t minor = driverVersion & 0x3fffu;
        if (major > 0 && minor > 0) {
            snprintf(out, outSize, "%u.%u", major, minor);
            return;
        }
    }

    snprintf(
        out,
        outSize,
        "%u.%u.%u",  // AMD / UNKNOWN
        VK_API_VERSION_MAJOR(driverVersion),
        VK_API_VERSION_MINOR(driverVersion),
        VK_API_VERSION_PATCH(driverVersion)
    );
}

void formatByteSize(uint64_t bytes, char* out, size_t outSize) {
    if (!out || outSize == 0) return;

    static const char* symbols[] = {"B", "KB", "MB", "GB"};
    double scaledBytes = (double)bytes;
    int index;

    for (index = 0; index < 3; index++) {
        if (scaledBytes < 1024.0) break;
        scaledBytes /= 1024.0;
    }

    snprintf(out, outSize, "%.*f %s", index > 0, scaledBytes, symbols[index]);
}

static void drawPaddedTooltip(const char* text) {
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

uint32_t clampRenderDimension(int value) {
    if (value < 1) return 1;
    if (value > 16384) return 16384;
    return (uint32_t)value;
}

static void formatUnsignedLongLong(char* out, size_t outSize, unsigned long long value, const char* unit) {
    snprintf(out, outSize, "%llu%s", value, unit);
}

static void formatTimeUnitValue(
    uint64_t totalMs,
    int startUnit,
    int* outUnitIndex,
    uint64_t* outUnitValue,
    uint64_t* outRemainder
) {
    static const uint64_t unitMs[] = {86400000ull, 3600000ull, 60000ull, 1000ull, 1ull};

    *outUnitIndex = -1;
    *outUnitValue = 0u;
    *outRemainder = totalMs;
    for (int unitIndex = startUnit; unitIndex < 5; unitIndex++) {
        uint64_t value = (*outRemainder) / unitMs[unitIndex];
        if (value == 0u) continue;
        *outUnitIndex = unitIndex;
        *outUnitValue = value;
        *outRemainder %= unitMs[unitIndex];
        return;
    }
}

void formatTime(float seconds, char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    if (seconds <= 0.0f) {
        snprintf(out, outSize, "0ms");
        return;
    }

    uint64_t totalMs = (uint64_t)((seconds * 1000.0f) + 0.5f);
    if (totalMs == 0) {
        snprintf(out, outSize, "0ms");
        return;
    }

    static const char* unitLabels[] = {"d", "h", "m", "s", "ms"};

    int firstUnit = -1;
    uint64_t firstValue = 0;
    uint64_t remainder = totalMs;
    formatTimeUnitValue(totalMs, 0, &firstUnit, &firstValue, &remainder);

    if (firstUnit < 0) {
        snprintf(out, outSize, "0ms");
        return;
    }

    int secondUnit = -1;
    uint64_t secondValue = 0;
    uint64_t ignoredRemainder = 0u;
    formatTimeUnitValue(remainder, firstUnit + 1, &secondUnit, &secondValue, &ignoredRemainder);

    if (secondUnit >= 0) {
        snprintf(
            out,
            outSize,
            "%llu%s %llu%s",
            (unsigned long long)firstValue,
            unitLabels[firstUnit],
            (unsigned long long)secondValue,
            unitLabels[secondUnit]
        );
    } else {
        formatUnsignedLongLong(out, outSize, (unsigned long long)firstValue, unitLabels[firstUnit]);
    }
}
