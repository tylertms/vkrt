#include "scene.h"

#include <math.h>

void applyCameraInput(VKRT* vkrt, const VKRT_CameraInput* input) {
    if (!vkrt || !input) return;
    if (input->captureMouse) return;

    const float panSpeed = 0.001f;
    const float orbitSpeed = 0.004f;
    const float zoomSpeed = -0.075f;

    float* pos = vkrt->state.camera.pos;
    float* tgt = vkrt->state.camera.target;
    vec3 viewDir = {tgt[0] - pos[0], tgt[1] - pos[1], tgt[2] - pos[2]};
    float dist = glm_vec3_norm(viewDir);

    vec3 right, up;
    glm_vec3_cross(viewDir, (vec3){0, 0, 1}, right);

    if (glm_vec3_norm(right) < 1e-6f) {
        right[0] = 0;
        right[1] = 1;
        right[2] = 0;
    } else {
        glm_vec3_normalize(right);
    }

    glm_vec3_cross(right, viewDir, up);
    glm_vec3_normalize(up);

    if (input->panning) {
        float s = panSpeed * dist;
        float mx = -input->panDx * s;
        float my = -input->panDy * s;
        for (int i = 0; i < 3; i++) {
            float d = right[i] * mx + up[i] * my;
            pos[i] += d;
            tgt[i] += d;
        }
        updateMatricesFromCamera(vkrt);
    }

    if (input->orbiting) {
        const float PI_2 = 1.5707963267948966f;
        const float EPS = 0.001f;

        float yaw = atan2f(viewDir[1], viewDir[0]) - input->orbitDx * orbitSpeed;
        float pitch = atan2f(viewDir[2], sqrtf(viewDir[0] * viewDir[0] + viewDir[1] * viewDir[1])) + input->orbitDy * orbitSpeed;
        pitch = glm_clamp(pitch, -PI_2 + EPS, PI_2 - EPS);

        float cp = cosf(pitch);
        vec3 fwd = {cp * cosf(yaw), cp * sinf(yaw), sinf(pitch)};
        for (int i = 0; i < 3; i++)
            pos[i] = tgt[i] - fwd[i] * dist;

        updateMatricesFromCamera(vkrt);
    }

    if (input->scroll != 0.0f) {
        float s = input->scroll * zoomSpeed;
        for (int i = 0; i < 3; i++)
            pos[i] -= viewDir[i] * s;
        updateMatricesFromCamera(vkrt);
    }
}

void updateMatricesFromCamera(VKRT* vkrt) {
    if (!vkrt || !vkrt->core.sceneData) return;

    mat4 view, proj;
    Camera cam = vkrt->state.camera;

    glm_lookat(cam.pos, cam.target, cam.up, view);
    glm_perspective(glm_rad(cam.vfov), (float)cam.width / cam.height, cam.nearZ, cam.farZ, proj);

    glm_mat4_inv(view, vkrt->core.sceneData->viewInverse);
    glm_mat4_inv(proj, vkrt->core.sceneData->projInverse);

    markSelectionMaskDirty(vkrt);
    resetSceneData(vkrt);
}
