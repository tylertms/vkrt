#pragma once
#ifdef __cplusplus
extern "C" {
#endif

typedef struct VKRT VKRT;

VKRT* vkrt_create(void);
void  vkrt_destroy(VKRT* vkrt);
void  vkrt_set_rgen(VKRT* vkrt, const char* path);
void  vkrt_set_rmiss(VKRT* vkrt, const char* path);
void  vkrt_set_rchit(VKRT* vkrt, const char* path);
int   vkrt_run(VKRT* vkrt);

#ifdef __cplusplus
}
#endif
