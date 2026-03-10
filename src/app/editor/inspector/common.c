#include "common.h"

#include <inttypes.h>
#include <stdio.h>
#include <vulkan/vulkan.h>

const float kInspectorControlSpacing = 4.0f;
const float kInspectorSectionIndent = 8.0f;
const float kInspectorSpacerHairline = 1.0f;
const float kInspectorSpacerMedium = 6.0f;
static const ImVec2 kTooltipPadding = {8.0f, 4.0f};

void inspectorIndentSection(void) {
    ImGui_IndentEx(kInspectorSectionIndent);
}

void inspectorUnindentSection(void) {
    ImGui_UnindentEx(kInspectorSectionIndent);
}

bool inspectorBeginKeyValueTable(const char* id) {
    if (!id) return false;

    ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings;
    if (!ImGui_BeginTableEx(id, 2, flags, (ImVec2){-1.0f, 0.0f}, 0.0f)) {
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

void inspectorKeyValueRow(const char* label, const char* value) {
    if (!label || !value) return;

    ImGui_TableNextRow();
    ImGui_TableSetColumnIndex(0);
    ImGui_TextDisabled("%s", label);
    ImGui_TableSetColumnIndex(1);
    ImGui_TextWrapped("%s", value);
}

void inspectorEndKeyValueTable(void) {
    ImGui_EndTable();
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

    if (vendorID == 0x10DEu) { // NVIDIA
        uint32_t major = (driverVersion >> 22u) & 0x3ffu;
        uint32_t minor = (driverVersion >> 14u) & 0xffu;
        snprintf(out, outSize, "%u.%02u", major, minor);
        return;
    }

    if (vendorID == 0x8086u) { // INTEL
        uint32_t major = driverVersion >> 14u;
        uint32_t minor = driverVersion & 0x3fffu;
        if (major > 0 && minor > 0) {
            snprintf(out, outSize, "%u.%u", major, minor);
            return;
        }
    }

    snprintf(out, outSize, "%u.%u.%u", // AMD / UNKNOWN
        VK_API_VERSION_MAJOR(driverVersion),
        VK_API_VERSION_MINOR(driverVersion),
        VK_API_VERSION_PATCH(driverVersion));
}

void formatByteSize(uint64_t bytes, char* out, size_t outSize) {
    if (!out || outSize == 0) return;

    static const char* symbols[] = {"B", "KB", "MB", "GB"};
    double _bytes = bytes;
    int index;

    for (index = 0; index < 3; index++) {
        if (_bytes < 1024.0) break;
        _bytes /= 1024.0;
    }

    snprintf(out, outSize, "%.*f %s", index > 0, _bytes, symbols[index]);
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
        snprintf(out, outSize, "%" PRIu64 "%s %" PRIu64 "%s",
            firstValue, unitLabels[firstUnit],
            secondValue, unitLabels[secondUnit]);
    } else {
        snprintf(out, outSize, "%" PRIu64 "%s",
            firstValue, unitLabels[firstUnit]);
    }
}
