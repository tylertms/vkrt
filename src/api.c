#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "vkrt.h"

static void VKRT_freeStr(char **p) {
    if (*p) { free(*p); *p = NULL; }
}

VKRT* VKRT_Create(void) {
    VKRT* vkrt = (VKRT*)calloc(1, sizeof(VKRT));
    return vkrt;
}

void VKRT_Destroy(VKRT* vkrt) {
    if (!vkrt) return;
    VKRT_freeStr(&vkrt->rgenPath);
    VKRT_freeStr(&vkrt->rmissPath);
    VKRT_freeStr(&vkrt->rchitPath);
    free(vkrt);
}

void VKRT_SetRGEN(VKRT* vkrt, const char* path) {
    if (!vkrt) return;
    VKRT_freeStr(&vkrt->rgenPath);
    vkrt->rgenPath = strdup(path);
}

void VKRT_SetRMISS(VKRT* vkrt, const char* path) {
    if (!vkrt) return;
    VKRT_freeStr(&vkrt->rmissPath);
    vkrt->rmissPath = strdup(path);
}

void VKRT_SetRCHIT(VKRT* vkrt, const char* path) {
    if (!vkrt) return;
    VKRT_freeStr(&vkrt->rchitPath);
    vkrt->rchitPath = strdup(path);
}

int VKRT_Run(VKRT* vkrt) {
    if (!vkrt) return -1;
    run(vkrt);
    return 0;
}
