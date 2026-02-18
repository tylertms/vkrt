#include "scene.h"
#include "buffer.h"
#include "device.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

void applyCameraInput(VKRT* vkrt, const VKRT_CameraInput* input) {
    if (!vkrt || !input) return;
    const float panSpeed = 0.0015f;
    const float orbitSpeed = 0.004f;
    const float zoomSpeed = -0.1f;

    if (input->captureMouse) return;

    vec3 viewDir;
    glm_vec3_sub(vkrt->state.camera.target, vkrt->state.camera.pos, viewDir);
    float dist = glm_vec3_norm(viewDir);

    vec3 worldUp = {0.0f, 0.0f, 1.0f};
    vec3 right, up;
    glm_vec3_cross(viewDir, worldUp, right);

    if (glm_vec3_norm(right) < 1e-6f) {
        right[0] = 0.0f; right[1] = 1.0f; right[2] = 0.0f;
    } else {
        glm_vec3_normalize(right);
    }

    glm_vec3_cross(right, viewDir, up);
    glm_vec3_normalize(up);

    if (input->panning) {
        float dx = input->panDx;
        float dy = input->panDy;

        vec3 move, tmp;
        glm_vec3_scale(right, -dx * panSpeed * dist, move);
        glm_vec3_scale(up,    -dy * panSpeed * dist, tmp);
        glm_vec3_add(move, tmp, move);

        glm_vec3_add(vkrt->state.camera.pos, move, vkrt->state.camera.pos);
        glm_vec3_add(vkrt->state.camera.target, move, vkrt->state.camera.target);
        updateMatricesFromCamera(vkrt);
    }

    if (input->orbiting) {
        float dx = input->orbitDx;
        float dy = input->orbitDy;

        const float PI_2 = 1.5707963267948966f;
        const float EPS = 0.001f;

        float yaw = atan2f(viewDir[1], viewDir[0]);
        float xyLen = sqrtf(viewDir[0] * viewDir[0] + viewDir[1] * viewDir[1]);
        float pitch = atan2f(viewDir[2], xyLen);

        yaw -= dx * orbitSpeed;
        pitch += dy * orbitSpeed;

        if (pitch >  PI_2 - EPS) pitch =  PI_2 - EPS;
        if (pitch < -PI_2 + EPS) pitch = -PI_2 + EPS;

        vec3 fwd = {
            cosf(pitch) * cosf(yaw),
            cosf(pitch) * sinf(yaw),
            sinf(pitch)
        };

        vec3 offset;
        glm_vec3_scale(fwd, dist, offset);
        glm_vec3_sub(vkrt->state.camera.target, offset, vkrt->state.camera.pos);

        updateMatricesFromCamera(vkrt);
    }

    float scroll = input->scroll;
    if (scroll != 0.0f) {
        glm_vec3_scale(viewDir, scroll * zoomSpeed, viewDir);
        glm_vec3_sub(vkrt->state.camera.pos, viewDir, vkrt->state.camera.pos);
        updateMatricesFromCamera(vkrt);
    }
}

void recordFrameTime(VKRT* vkrt) {
    uint64_t currentTime = getMicroseconds();

    if (vkrt->state.lastFrameTimestamp == 0) {
        vkrt->state.lastFrameTimestamp = currentTime;
        return;
    }

    vkrt->state.displayTimeMs = (currentTime - vkrt->state.lastFrameTimestamp) / 1000.0f;
    vkrt->state.lastFrameTimestamp = currentTime;

    uint64_t ts[2];
    vkGetQueryPoolResults(vkrt->core.device, vkrt->runtime.timestampPool, vkrt->runtime.currentFrame * 2, 2, sizeof(ts), ts, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
    vkrt->state.renderTimeMs = (float)((ts[1] - ts[0]) * vkrt->runtime.timestampPeriod / 1e6);

    int32_t frameNumber = vkrt->core.sceneData->frameNumber;

    size_t n = (frameNumber < 0) ? 1u : (size_t)(frameNumber + 1);
    size_t cap = COUNT_OF(vkrt->state.frametimes);
    if (n > cap) n = cap;

    float weight = 1.0f / (float)n;

    vkrt->state.averageFrametime = vkrt->state.averageFrametime * (1 - weight) + vkrt->state.displayTimeMs * weight;
    vkrt->state.framesPerSecond = (uint32_t)(1000.0f / vkrt->state.displayTimeMs);
    vkrt->state.frametimes[vkrt->state.frametimeStartIndex] = vkrt->state.displayTimeMs;
    vkrt->state.frametimeStartIndex = (vkrt->state.frametimeStartIndex + 1) % COUNT_OF(vkrt->state.frametimes);

}

void createSceneUniform(VKRT* vkrt) {
    VkDeviceSize uniformBufferSize = sizeof(SceneData);
    createBuffer(vkrt, uniformBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &vkrt->core.sceneDataBuffer, &vkrt->core.sceneDataMemory);
    vkMapMemory(vkrt->core.device, vkrt->core.sceneDataMemory, 0, uniformBufferSize, 0, (void**)&vkrt->core.sceneData);
    memset(vkrt->core.sceneData, 0, uniformBufferSize);

    vkrt->state.camera = (Camera){
        .width = WIDTH, .height = HEIGHT,
        .nearZ = 0.001f, .farZ = 10000.0f,
        .vfov = 40.0f,
        .pos = {-0.5f, 0.2f, -0.2f},
        .target = {0.0f, 0.0f, 0.0f},
        .up = {0.0f, 0.0f, 1.0f}
    };

    updateMatricesFromCamera(vkrt);
}

void updateMatricesFromCamera(VKRT* vkrt) {
    mat4 view, proj;
    Camera cam = vkrt->state.camera;

    glm_lookat(cam.pos, cam.target, cam.up, view);
    glm_perspective(glm_rad(cam.vfov), (float)cam.width / cam.height, cam.nearZ, cam.farZ, proj);

    glm_mat4_inv(view, vkrt->core.sceneData->viewInverse);
    glm_mat4_inv(proj, vkrt->core.sceneData->projInverse);

    resetSceneData(vkrt);
}

void resetSceneData(VKRT* vkrt) {
    vkrt->core.sceneData->frameNumber = 0;
    vkrt->state.averageFrametime = 0.0f;
    vkrt->state.frametimeStartIndex = 0;
    memset(vkrt->state.frametimes, 0, sizeof(vkrt->state.frametimes));
}

VkTransformMatrixKHR getMeshTransform(MeshInfo* meshInfo) {
    vec3 scale;
    glm_vec3_copy(meshInfo->scale, scale);
    scale[1] = -scale[1];

    vec3 position;
    glm_vec3_copy(meshInfo->position, position);

    vec3 rotation = {
        glm_rad(meshInfo->rotation[0] - 90),
        glm_rad(meshInfo->rotation[1]),
        glm_rad(meshInfo->rotation[2] - 90)
    };

    mat4 matrix;
    glm_mat4_identity(matrix);
    glm_translate(matrix, position);
    glm_rotate(matrix, rotation[2], (vec3){0.f, 0.f, 1.f});
    glm_rotate(matrix, rotation[1], (vec3){0.f, 1.f, 0.f});
    glm_rotate(matrix, rotation[0], (vec3){1.f, 0.f, 0.f});
    glm_scale(matrix, scale);

    VkTransformMatrixKHR transform = {0};
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 4; ++c)
            transform.matrix[r][c] = matrix[c][r];

    return transform;
}
