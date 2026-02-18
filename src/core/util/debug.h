#pragma once
#include "vkrt.h"

#include <stdio.h>
#include <stdint.h>

static inline void dumpStructHex(const void* p, size_t n) {
    const uint8_t* b = (const uint8_t*)p;
    for (size_t i = 0; i < n; i++) {
        if ((i % 16) == 0) printf("%04zx: ", i);
        printf("%02x ", b[i]);
        if ((i % 16) == 15 || i + 1 == n) printf("\n");
    }
}

static inline void printVec3(const vec3 v) {
    printf("(%.6f, %.6f, %.6f)", v[0], v[1], v[2]);
}

static inline void printMat4(const mat4 m) {
    for (int r = 0; r < 4; r++) {
        printf("[ %.6f %.6f %.6f %.6f ]\n", m[r][0], m[r][1], m[r][2], m[r][3]);
    }
}

static inline void printVertex(const Vertex* v) {
    printf("Vertex{ pos=(%.6f, %.6f, %.6f, %.6f), normal=(%.6f, %.6f, %.6f, %.6f) }\n",
           v->position[0], v->position[1], v->position[2], v->position[3],
           v->normal[0],   v->normal[1],   v->normal[2],   v->normal[3]);
}

static inline void printMesh(const Mesh* m) {
    printf("Mesh{\n  position="); printVec3(m->info.position);
    printf(", rotation=");        printVec3(m->info.rotation);
    printf(", scale=");           printVec3(m->info.scale);
    printf("\n  vertexCount=%u, vertexBase=%u, indexCount=%u, indexBase=%u\n}\n",
           m->info.vertexCount, m->info.vertexBase, m->info.indexCount, m->info.indexBase);
}

static inline void printSceneUniform(const SceneData* u) {
    printf("SceneUniform{\n  viewInverse=\n"); printMat4(u->viewInverse);
    printf("  projInverse=\n");                printMat4(u->projInverse);
    printf("}\n");
}

static inline void printCamera(const Camera* c) {
    printf("Camera{\n  pos=");    printVec3(c->pos);
    printf(", target=");          printVec3(c->target);
    printf(", up=");              printVec3(c->up);
    printf("\n  size=%ux%u, near=%.6f, far=%.6f, vfov=%.6f\n}\n",
           c->width, c->height, c->nearZ, c->farZ, c->vfov);
}
