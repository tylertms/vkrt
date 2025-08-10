#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

typedef struct VKRT VKRT;

VKRT* VKRT_Create(void);
void VKRT_Destroy(VKRT* vkrt);
void VKRT_SetRGEN(VKRT* vkrt, const char* path);
void VKRT_SetRMISS(VKRT* vkrt, const char* path);
void VKRT_SetRCHIT(VKRT* vkrt, const char* path);
int VKRT_Run(VKRT* vkrt);

#ifdef __cplusplus
}
#endif
