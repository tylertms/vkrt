#include "scene.h"

VkTransformMatrixKHR getMeshTransform(MeshInfo* meshInfo) {
    vec3 scale = {meshInfo->scale[0], -meshInfo->scale[1], meshInfo->scale[2]};
    float rx = glm_rad(meshInfo->rotation[0] - 90);
    float ry = glm_rad(meshInfo->rotation[1]);
    float rz = glm_rad(meshInfo->rotation[2] - 90);

    mat4 matrix;
    glm_mat4_identity(matrix);
    glm_translate(matrix, meshInfo->position);
    glm_rotate(matrix, rz, (vec3){0.f, 0.f, 1.f});
    glm_rotate(matrix, ry, (vec3){0.f, 1.f, 0.f});
    glm_rotate(matrix, rx, (vec3){1.f, 0.f, 0.f});
    glm_scale(matrix, scale);

    VkTransformMatrixKHR transform = {0};
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 4; ++c)
            transform.matrix[r][c] = matrix[c][r];

    return transform;
}
