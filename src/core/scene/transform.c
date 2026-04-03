#include "../../../external/cglm/include/types.h"
#include "scene.h"
#include "vkrt_engine_types.h"
#include "vulkan/vulkan_core.h"

#include <affine-pre.h>
#include <affine.h>
#include <mat4.h>
#include <math.h>
#include <util.h>
#include <vec3.h>

static const float kTransformEpsilon = 1e-6f;
static const vec3 kImportedMeshBasisRotationDegrees = {90.0f, 0.0f, 0.0f};
static const vec3 kImportedMeshBasisScale = {1.0f, 1.0f, 1.0f};

static float rotationDeterminant(mat4 rotationMatrix) {
    vec3 column0 = {rotationMatrix[0][0], rotationMatrix[0][1], rotationMatrix[0][2]};
    vec3 column1 = {rotationMatrix[1][0], rotationMatrix[1][1], rotationMatrix[1][2]};
    vec3 column2 = {rotationMatrix[2][0], rotationMatrix[2][1], rotationMatrix[2][2]};
    vec3 cross01 = GLM_VEC3_ZERO_INIT;
    glm_vec3_cross(column0, column1, cross01);
    return glm_vec3_dot(cross01, column2);
}

void VKRT_buildMeshTransformMatrix(const vec3 position, const vec3 rotationDegrees, const vec3 scale, mat4 outMatrix) {
    if (!position || !rotationDegrees || !scale || !outMatrix) return;

    glm_mat4_identity(outMatrix);
    glm_translate(outMatrix, (vec3){position[0], position[1], position[2]});
    glm_rotate(outMatrix, glm_rad(rotationDegrees[2]), (vec3){0.0f, 0.0f, 1.0f});
    glm_rotate(outMatrix, glm_rad(rotationDegrees[1]), (vec3){0.0f, 1.0f, 0.0f});
    glm_rotate(outMatrix, glm_rad(rotationDegrees[0]), (vec3){1.0f, 0.0f, 0.0f});
    glm_scale(outMatrix, (vec3){scale[0], scale[1], scale[2]});
}

static void extractRotationAndScale(mat4 worldTransform, mat4 outRotationMatrix, vec3 outScale) {
    glm_mat4_identity(outRotationMatrix);
    outScale[0] = 1.0f;
    outScale[1] = 1.0f;
    outScale[2] = 1.0f;

    for (int axis = 0; axis < 3; axis++) {
        vec3 column = {
            worldTransform[axis][0],
            worldTransform[axis][1],
            worldTransform[axis][2],
        };
        float axisScale = glm_vec3_norm(column);
        if (axisScale < kTransformEpsilon || !isfinite(axisScale)) {
            outRotationMatrix[axis][0] = axis == 0 ? 1.0f : 0.0f;
            outRotationMatrix[axis][1] = axis == 1 ? 1.0f : 0.0f;
            outRotationMatrix[axis][2] = axis == 2 ? 1.0f : 0.0f;
            continue;
        }

        outScale[axis] = axisScale;
        outRotationMatrix[axis][0] = column[0] / axisScale;
        outRotationMatrix[axis][1] = column[1] / axisScale;
        outRotationMatrix[axis][2] = column[2] / axisScale;
    }
}

static void extractEulerZYXRadians(mat4 rotationMatrix, vec3 outRotationRadians) {
    if (!outRotationRadians) return;

    float sineY = -rotationMatrix[0][2];
    if (sineY < -1.0f) {
        sineY = -1.0f;
    } else if (sineY > 1.0f) {
        sineY = 1.0f;
    }
    outRotationRadians[1] = asinf(sineY);

    if (fabsf(cosf(outRotationRadians[1])) > kTransformEpsilon) {
        outRotationRadians[0] = atan2f(rotationMatrix[1][2], rotationMatrix[2][2]);
        outRotationRadians[2] = atan2f(rotationMatrix[0][1], rotationMatrix[0][0]);
    } else {
        outRotationRadians[0] = atan2f(-rotationMatrix[2][1], rotationMatrix[1][1]);
        outRotationRadians[2] = 0.0f;
    }
}

static void buildImportedMeshBasisTransform(mat4 outTransform) {
    if (!outTransform) return;

    glm_mat4_identity(outTransform);
    glm_rotate(outTransform, glm_rad(kImportedMeshBasisRotationDegrees[2]), (vec3){0.0f, 0.0f, 1.0f});
    glm_rotate(outTransform, glm_rad(kImportedMeshBasisRotationDegrees[1]), (vec3){0.0f, 1.0f, 0.0f});
    glm_rotate(outTransform, glm_rad(kImportedMeshBasisRotationDegrees[0]), (vec3){1.0f, 0.0f, 0.0f});
    glm_scale(
        outTransform,
        (vec3){
            kImportedMeshBasisScale[0],
            kImportedMeshBasisScale[1],
            kImportedMeshBasisScale[2],
        }
    );
}

void VKRT_buildImportedNodeTransform(mat4 worldTransform, mat4 outEngineTransform) {
    if (!worldTransform || !outEngineTransform) return;

    mat4 importBasisTransform = GLM_MAT4_IDENTITY_INIT;
    mat4 importBasisInverse = GLM_MAT4_IDENTITY_INIT;
    mat4 basisAdjustedTransform = GLM_MAT4_IDENTITY_INIT;
    buildImportedMeshBasisTransform(importBasisTransform);
    glm_mat4_inv(importBasisTransform, importBasisInverse);
    glm_mat4_mul(importBasisTransform, worldTransform, basisAdjustedTransform);
    glm_mat4_mul(basisAdjustedTransform, importBasisInverse, outEngineTransform);
}

void VKRT_decomposeMeshNodeTransform(mat4 worldTransform, vec3 outPosition, vec3 outRotation, vec3 outScale) {
    if (!outPosition || !outRotation || !outScale) return;

    mat4 engineTransform = GLM_MAT4_IDENTITY_INIT;
    VKRT_buildImportedNodeTransform(worldTransform, engineTransform);
    VKRT_decomposeMeshTransform(engineTransform, outPosition, outRotation, outScale);
}

static void assignSignedScaleCandidate(
    mat4 normalizedRotationMatrix,
    const vec3 absoluteScale,
    int flippedAxis,
    mat4 outRotationMatrix,
    vec3 outScale
) {
    if (!normalizedRotationMatrix || !absoluteScale || !outRotationMatrix || !outScale) return;

    glm_mat4_copy(normalizedRotationMatrix, outRotationMatrix);
    outScale[0] = absoluteScale[0];
    outScale[1] = absoluteScale[1];
    outScale[2] = absoluteScale[2];

    if (flippedAxis < 0 || flippedAxis > 2) {
        return;
    }

    outScale[flippedAxis] = -outScale[flippedAxis];
    outRotationMatrix[flippedAxis][0] = -outRotationMatrix[flippedAxis][0];
    outRotationMatrix[flippedAxis][1] = -outRotationMatrix[flippedAxis][1];
    outRotationMatrix[flippedAxis][2] = -outRotationMatrix[flippedAxis][2];
}

static float transform3x4Error(mat4 a, mat4 b) {
    float maxError = 0.0f;
    for (int column = 0; column < 4; column++) {
        for (int row = 0; row < 3; row++) {
            float delta = fabsf(a[column][row] - b[column][row]);
            if (delta > maxError) {
                maxError = delta;
            }
        }
    }
    return maxError;
}

void VKRT_decomposeMeshTransform(mat4 worldTransform, vec3 outPosition, vec3 outRotation, vec3 outScale) {
    if (!outPosition || !outRotation || !outScale) return;

    outPosition[0] = worldTransform[3][0];
    outPosition[1] = worldTransform[3][1];
    outPosition[2] = worldTransform[3][2];

    mat4 normalizedRotationMatrix = GLM_MAT4_IDENTITY_INIT;
    vec3 absoluteScale = {1.0f, 1.0f, 1.0f};
    extractRotationAndScale(worldTransform, normalizedRotationMatrix, absoluteScale);

    float determinant = rotationDeterminant(normalizedRotationMatrix);
    int candidateCount = determinant < 0.0f ? 3 : 1;
    float bestError = INFINITY;
    vec3 bestRotation = GLM_VEC3_ZERO_INIT;
    vec3 bestScale = {absoluteScale[0], absoluteScale[1], absoluteScale[2]};

    for (int candidateIndex = 0; candidateIndex < candidateCount; candidateIndex++) {
        int flippedAxis = determinant < 0.0f ? candidateIndex : -1;
        mat4 candidateRotationMatrix = GLM_MAT4_IDENTITY_INIT;
        vec3 candidateScale = {1.0f, 1.0f, 1.0f};
        assignSignedScaleCandidate(
            normalizedRotationMatrix,
            absoluteScale,
            flippedAxis,
            candidateRotationMatrix,
            candidateScale
        );

        vec3 candidateRotationRadians = {0.0f, 0.0f, 0.0f};
        extractEulerZYXRadians(candidateRotationMatrix, candidateRotationRadians);

        vec3 candidateRotationDegrees = {
            glm_deg(candidateRotationRadians[0]),
            glm_deg(candidateRotationRadians[1]),
            glm_deg(candidateRotationRadians[2]),
        };

        mat4 recomposedTransform = GLM_MAT4_IDENTITY_INIT;
        VKRT_buildMeshTransformMatrix(outPosition, candidateRotationDegrees, candidateScale, recomposedTransform);
        float candidateError = transform3x4Error(worldTransform, recomposedTransform);
        if (candidateError < bestError) {
            bestError = candidateError;
            glm_vec3_copy(candidateRotationDegrees, bestRotation);
            glm_vec3_copy(candidateScale, bestScale);
        }
    }

    for (int axis = 0; axis < 3; axis++) {
        outRotation[axis] = bestRotation[axis];
        outScale[axis] = bestScale[axis];
    }
}

VkTransformMatrixKHR getMeshWorldTransform(const Mesh* mesh) {
    VkTransformMatrixKHR transform = {0};
    if (!mesh) return transform;

    for (int rowIndex = 0; rowIndex < 3; ++rowIndex) {
        for (int columnIndex = 0; columnIndex < 4; ++columnIndex) {
            transform.matrix[rowIndex][columnIndex] = mesh->worldTransform[columnIndex][rowIndex];
        }
    }

    return transform;
}
