#include "scene.h"
#include "vkrt_types.h"

#include <cam.h>
#include <mat4.h>
#include <math.h>
#include <stdint.h>
#include <types.h>
#include <util.h>
#include <vec3.h>

static void queryCameraBasis(const float* position, const float* target, vec3 viewDirection, vec3 right, vec3 upVector) {
    viewDirection[0] = target[0] - position[0];
    viewDirection[1] = target[1] - position[1];
    viewDirection[2] = target[2] - position[2];

    glm_vec3_cross(viewDirection, (vec3){0, 0, 1}, right);
    if (glm_vec3_norm(right) < 1e-6f) {
        right[0] = 0.0f;
        right[1] = 1.0f;
        right[2] = 0.0f;
    } else {
        glm_vec3_normalize(right);
    }

    glm_vec3_cross(right, viewDirection, upVector);
    glm_vec3_normalize(upVector);
}

static void applyPanInput(
    const VKRT_CameraInput* input,
    float distance,
    const vec3 right,
    const vec3 upVector,
    float* position,
    float* target
) {
    float panScale = 0.001f * distance;
    float moveX = -input->panDx * panScale;
    float moveY = input->panDy * panScale;

    for (int axisIndex = 0; axisIndex < 3; axisIndex++) {
        float delta = (right[axisIndex] * moveX) + (upVector[axisIndex] * moveY);
        position[axisIndex] += delta;
        target[axisIndex] += delta;
    }
}

static void applyOrbitInput(const VKRT_CameraInput* input, const vec3 viewDirection, float distance, float* position, const float* target) {
    const float orbitSpeed = 0.004f;
    const float piOverTwo = 1.5707963267948966f;
    const float epsilon = 0.001f;
    float yaw = atan2f(viewDirection[1], viewDirection[0]) - (input->orbitDx * orbitSpeed);
    float pitch =
        atan2f(viewDirection[2], sqrtf((viewDirection[0] * viewDirection[0]) + (viewDirection[1] * viewDirection[1])))
        - (input->orbitDy * orbitSpeed);
    float cosinePitch = 0.0f;
    vec3 forward = GLM_VEC3_ZERO_INIT;

    pitch = glm_clamp(pitch, -piOverTwo + epsilon, piOverTwo - epsilon);
    cosinePitch = cosf(pitch);
    forward[0] = cosinePitch * cosf(yaw);
    forward[1] = cosinePitch * sinf(yaw);
    forward[2] = sinf(pitch);

    for (int axisIndex = 0; axisIndex < 3; axisIndex++) {
        position[axisIndex] = target[axisIndex] - (forward[axisIndex] * distance);
    }
}

static void applyZoomInput(const VKRT_CameraInput* input, const vec3 viewDirection, float* position) {
    float zoomStep = input->scroll * -0.075f;
    for (int axisIndex = 0; axisIndex < 3; axisIndex++) {
        position[axisIndex] -= viewDirection[axisIndex] * zoomStep;
    }
}

void applyCameraInput(VKRT* vkrt, const VKRT_CameraInput* input) {
    if (!vkrt || !input) return;
    if (input->captureMouse) return;

    float* pos = vkrt->sceneSettings.camera.pos;
    float* tgt = vkrt->sceneSettings.camera.target;
    vec3 viewDir = GLM_VEC3_ZERO_INIT;
    vec3 right;
    vec3 upVector;
    float dist = 0.0f;
    int hasPanDelta = input->panDx != 0.0f || input->panDy != 0.0f;
    int hasOrbitDelta = input->orbitDx != 0.0f || input->orbitDy != 0.0f;

    queryCameraBasis(pos, tgt, viewDir, right, upVector);
    dist = glm_vec3_norm(viewDir);

    if (input->panning && hasPanDelta) {
        applyPanInput(input, dist, right, upVector, pos, tgt);
        updateCamera(vkrt);
    }

    if (input->orbiting && hasOrbitDelta) {
        applyOrbitInput(input, viewDir, dist, pos, tgt);
        updateCamera(vkrt);
    }

    if (input->scroll != 0.0f) {
        applyZoomInput(input, viewDir, pos);
        updateCamera(vkrt);
    }
}

void updateCamera(VKRT* vkrt) {
    syncCameraMatrices(vkrt);
    markSelectionMaskDirty(vkrt);
    resetSceneData(vkrt);
}

void syncCameraMatrices(VKRT* vkrt) {
    if (!vkrt || !vkrt->core.sceneData) return;

    mat4 view;
    mat4 proj;
    Camera cam = vkrt->sceneSettings.camera;
    uint32_t viewportWidth = vkrt->core.sceneData->viewportRect[2] > 0u ? vkrt->core.sceneData->viewportRect[2] : 1u;
    uint32_t viewportHeight = vkrt->core.sceneData->viewportRect[3] > 0u ? vkrt->core.sceneData->viewportRect[3] : 1u;

    glm_lookat(cam.pos, cam.target, cam.up, view);
    glm_perspective(glm_rad(cam.vfov), (float)viewportWidth / (float)viewportHeight, cam.nearZ, cam.farZ, proj);
    proj[1][1] *= -1.0f;

    glm_mat4_inv(view, vkrt->core.sceneData->viewInverse);
    glm_mat4_inv(proj, vkrt->core.sceneData->projInverse);
}
