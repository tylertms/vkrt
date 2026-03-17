#pragma once

#include <stddef.h>
#include <stdint.h>

extern const uint32_t shaderRgenData[];
extern const size_t shaderRgenSize;

extern const uint32_t shaderRchitData[];
extern const size_t shaderRchitSize;

extern const uint32_t shaderRmissData[];
extern const size_t shaderRmissSize;

extern const uint32_t shaderShadowMissData[];
extern const size_t shaderShadowMissSize;

extern const uint32_t shaderRgenSerData[];
extern const size_t shaderRgenSerSize;

extern const uint32_t shaderRchitSerData[];
extern const size_t shaderRchitSerSize;

extern const uint32_t shaderRmissSerData[];
extern const size_t shaderRmissSerSize;

extern const uint32_t shaderShadowMissSerData[];
extern const size_t shaderShadowMissSerSize;

#if VKRT_SELECTION_ENABLED
extern const uint32_t shaderCompData[];
extern const size_t shaderCompSize;

extern const uint32_t shaderSelectRgenData[];
extern const size_t shaderSelectRgenSize;

extern const uint32_t shaderSelectRchitData[];
extern const size_t shaderSelectRchitSize;

extern const uint32_t shaderSelectRmissData[];
extern const size_t shaderSelectRmissSize;
#endif
