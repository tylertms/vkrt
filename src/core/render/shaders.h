#pragma once

#include <stddef.h>

extern const unsigned char shaderRgenData[];
extern const size_t shaderRgenSize;

extern const unsigned char shaderRchitData[];
extern const size_t shaderRchitSize;

extern const unsigned char shaderRmissData[];
extern const size_t shaderRmissSize;

extern const unsigned char shaderShadowMissData[];
extern const size_t shaderShadowMissSize;

extern const unsigned char shaderRgenSerData[];
extern const size_t shaderRgenSerSize;

extern const unsigned char shaderRchitSerData[];
extern const size_t shaderRchitSerSize;

extern const unsigned char shaderRmissSerData[];
extern const size_t shaderRmissSerSize;

extern const unsigned char shaderShadowMissSerData[];
extern const size_t shaderShadowMissSerSize;

#if VKRT_SELECTION_ENABLED
extern const unsigned char shaderCompData[];
extern const size_t shaderCompSize;

extern const unsigned char shaderSelectRgenData[];
extern const size_t shaderSelectRgenSize;

extern const unsigned char shaderSelectRchitData[];
extern const size_t shaderSelectRchitSize;

extern const unsigned char shaderSelectRmissData[];
extern const size_t shaderSelectRmissSize;
#endif
