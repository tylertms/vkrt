#include <stdlib.h>
#include <string.h>

#include "vkrt.h"
#include "app.h"

static void vkrt_free_str(char **p) {
    if (*p) { free(*p); *p = NULL; }
}

VKRT* vkrt_create(void) {
    VKRT* vkrt = (VKRT*)calloc(1, sizeof(VKRT));
    return vkrt;
}

void vkrt_destroy(VKRT* vkrt) {
    if (!vkrt) return;
    vkrt_free_str(&vkrt->rgenPath);
    vkrt_free_str(&vkrt->rmissPath);
    vkrt_free_str(&vkrt->rchitPath);
    free(vkrt);
}

void vkrt_set_rgen(VKRT* vkrt, const char* path) {
    if (!vkrt) return;
    vkrt_free_str(&vkrt->rgenPath);
    vkrt->rgenPath = strdup(path);
}

void vkrt_set_rmiss(VKRT* vkrt, const char* path) {
    if (!vkrt) return;
    vkrt_free_str(&vkrt->rmissPath);
    vkrt->rmissPath = strdup(path);
}

void vkrt_set_rchit(VKRT* vkrt, const char* path) {
    if (!vkrt) return;
    vkrt_free_str(&vkrt->rchitPath);
    vkrt->rchitPath = strdup(path);
}

int vkrt_run(VKRT* vkrt) {
    if (!vkrt) return -1;
    run(vkrt);
    return 0;
}
