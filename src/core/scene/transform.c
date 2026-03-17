#include "scene.h"

#include <math.h>

static const vec3 kMeshRotationOffsetsDegrees = {-90.0f, 0.0f, -90.0f};
static const vec3 kMeshScaleSigns = {1.0f, -1.0f, 1.0f};

void decomposeImportedMeshTransform(mat4 worldTransform, vec3 outPosition, vec3 outRotation, vec3 outScale) {
    if (!outPosition || !outRotation || !outScale) return;

    vec4 translation = {0.0f, 0.0f, 0.0f, 1.0f};
    mat4 rotationMatrix = GLM_MAT4_IDENTITY_INIT;
    vec3 scale = {1.0f, 1.0f, 1.0f};
    glm_decompose(worldTransform, translation, rotationMatrix, scale);

    for (int axis = 0; axis < 3; axis++) {
        if (fabsf(scale[axis]) < 1e-6f) {
            scale[axis] = 1.0f;
        }
    }

    vec3 rotationRadians = {0.0f, 0.0f, 0.0f};
    glm_euler_angles(rotationMatrix, rotationRadians);

    outPosition[0] = translation[0];
    outPosition[1] = translation[1];
    outPosition[2] = translation[2];

    for (int axis = 0; axis < 3; axis++) {
        outRotation[axis] = glm_deg(rotationRadians[axis]) - kMeshRotationOffsetsDegrees[axis];
        outScale[axis] = scale[axis] * kMeshScaleSigns[axis];
    }
}

VkTransformMatrixKHR getMeshTransform(MeshInfo* meshInfo) {
    vec3 scale = {
        meshInfo->scale[0] * kMeshScaleSigns[0],
        meshInfo->scale[1] * kMeshScaleSigns[1],
        meshInfo->scale[2] * kMeshScaleSigns[2]
    };
    float rx = glm_rad(meshInfo->rotation[0] + kMeshRotationOffsetsDegrees[0]);
    float ry = glm_rad(meshInfo->rotation[1] + kMeshRotationOffsetsDegrees[1]);
    float rz = glm_rad(meshInfo->rotation[2] + kMeshRotationOffsetsDegrees[2]);

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
