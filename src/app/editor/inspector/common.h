#pragma once

#include "dcimgui.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

extern const float kInspectorControlSpacing;
extern const float kInspectorSectionIndent;
extern const float kInspectorSpacerHairline;
extern const float kInspectorSpacerMedium;

void formatDriverVersionText(uint32_t vendorID, uint32_t driverVersion, char* out, size_t outSize);
void formatByteSize(uint64_t bytes, char* out, size_t outSize);
void inspectorIndentSection(void);
void inspectorUnindentSection(void);
bool inspectorBeginKeyValueTable(const char* id);
void inspectorKeyValueRow(const char* label, const char* value);
void inspectorEndKeyValueTable(void);
bool inspectorPaddedButton(const char* label);
void tooltipOnHover(const char* text);
uint32_t clampRenderDimension(int value);
void formatTime(float seconds, char* out, size_t outSize);
