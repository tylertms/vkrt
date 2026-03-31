#pragma once

#include "types.h"

#include <stdint.h>

uint32_t packHalf2(const float input[2]);
uint32_t packOctNormal32(const float normal[3]);
uint32_t packSnorm15(float value);
uint32_t packTangent32(const float tangent[4]);
uint32_t packColorRGBA8(const float color[4]);
ShaderVertex packShaderVertex(const Vertex* vertex);
