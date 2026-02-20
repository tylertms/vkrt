#include "scene.h"
#include "buffer.h"
#include "debug.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

static uint32_t nextSPPStep(uint32_t current, uint8_t increase, uint8_t strongDrop) {
    static const uint32_t ladder[] = {
        1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536, 2048
    };

    size_t index = 0;
    while (index + 1 < COUNT_OF(ladder) && ladder[index] < current) {
        index++;
    }

    if (increase) {
        if (index + 1 < COUNT_OF(ladder)) index++;
    } else {
        size_t step = strongDrop ? 2 : 1;
        if (index > step) index -= step;
        else index = 0;
    }

    return ladder[index];
}

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

    const float displaySmoothing = 0.12f;
    if (vkrt->state.displayFrameTimeMs <= 0.0f) vkrt->state.displayFrameTimeMs = vkrt->state.displayTimeMs;
    else vkrt->state.displayFrameTimeMs = vkrt->state.displayFrameTimeMs * (1.0f - displaySmoothing) + vkrt->state.displayTimeMs * displaySmoothing;

    if (vkrt->state.displayRenderTimeMs <= 0.0f) vkrt->state.displayRenderTimeMs = vkrt->state.renderTimeMs;
    else vkrt->state.displayRenderTimeMs = vkrt->state.displayRenderTimeMs * (1.0f - displaySmoothing) + vkrt->state.renderTimeMs * displaySmoothing;

    size_t n = (size_t)(vkrt->state.accumulationFrame + 1);
    size_t cap = COUNT_OF(vkrt->state.frametimes);
    if (n > cap) n = cap;

    float weight = 1.0f / (float)n;

    vkrt->state.averageFrametime = vkrt->state.averageFrametime * (1.0f - weight) + vkrt->state.displayFrameTimeMs * weight;
    vkrt->state.framesPerSecond = (uint32_t)(1000.0f / vkrt->state.displayFrameTimeMs);
    vkrt->state.frametimes[vkrt->state.frametimeStartIndex] = vkrt->state.displayFrameTimeMs;
    vkrt->state.frametimeStartIndex = (vkrt->state.frametimeStartIndex + 1) % COUNT_OF(vkrt->state.frametimes);
}

void createSceneUniform(VKRT* vkrt) {
    VkDeviceSize uniformBufferSize = sizeof(SceneData);
    createBuffer(vkrt, uniformBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &vkrt->core.sceneDataBuffer, &vkrt->core.sceneDataMemory);
    vkMapMemory(vkrt->core.device, vkrt->core.sceneDataMemory, 0, uniformBufferSize, 0, (void**)&vkrt->core.sceneData);
    memset(vkrt->core.sceneData, 0, uniformBufferSize);
    vkrt->state.samplesPerPixel = 8;
    vkrt->state.maxBounces = 8;
    vkrt->state.toneMappingMode = VKRT_TONE_MAPPING_ACES;
    vkrt->state.autoSPPEnabled = 1;
    vkrt->state.autoSPPTargetFps = 120;
    vkrt->state.autoSPPTargetFrameMs = 1000.0f / (float)vkrt->state.autoSPPTargetFps;
    vkrt->runtime.autoSPPFastFrames = 0;
    vkrt->runtime.autoSPPSlowFrames = 0;
    vkrt->runtime.autoSPPCooldownFrames = 0;

    vkrt->state.camera = (Camera){
        .width = WIDTH, .height = HEIGHT,
        .nearZ = 0.001f, .farZ = 10000.0f,
        .vfov = 40.0f,
        .pos = {-0.5f, 0.2f, -0.2f},
        .target = {0.0f, 0.0f, 0.0f},
        .up = {0.0f, 0.0f, 1.0f}
    };

    vkrt->core.sceneData->maxBounces = vkrt->state.maxBounces;
    vkrt->core.sceneData->toneMappingMode = vkrt->state.toneMappingMode;

    vkrt->core.sceneData->viewportRect[0] = 0;
    vkrt->core.sceneData->viewportRect[1] = 0;
    vkrt->core.sceneData->viewportRect[2] = vkrt->state.camera.width;
    vkrt->core.sceneData->viewportRect[3] = vkrt->state.camera.height;

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
    vkrt->core.sceneData->samplesPerPixel = vkrt->state.samplesPerPixel;
    vkrt->core.sceneData->maxBounces = vkrt->state.maxBounces;
    vkrt->core.sceneData->toneMappingMode = vkrt->state.toneMappingMode;
    vkrt->state.accumulationFrame = 0;
    vkrt->state.totalSamples = 0;
    vkrt->state.averageFrametime = 0.0f;
    vkrt->state.frametimeStartIndex = 0;
    vkrt->runtime.autoSPPFastFrames = 0;
    vkrt->runtime.autoSPPSlowFrames = 0;
    vkrt->runtime.autoSPPCooldownFrames = 0;
    vkrt->core.accumulationNeedsReset = VK_TRUE;
    memset(vkrt->state.frametimes, 0, sizeof(vkrt->state.frametimes));
}




void updateAutoSPP(VKRT* vkrt) {
    if (!vkrt || !vkrt->state.autoSPPEnabled) return;

    float targetMs = vkrt->state.autoSPPTargetFrameMs;
    if (targetMs <= 0.0f) targetMs = 1000.0f / (float)(vkrt->state.autoSPPTargetFps ? vkrt->state.autoSPPTargetFps : 120);
    if (targetMs < 1.0f) targetMs = 1.0f;

    float controlMs = vkrt->state.displayRenderTimeMs > 0.0f ? vkrt->state.displayRenderTimeMs : vkrt->state.displayFrameTimeMs;
    if (controlMs <= 0.0f) return;

    if (controlMs > targetMs * 1.06f) {
        vkrt->runtime.autoSPPSlowFrames++;
        vkrt->runtime.autoSPPFastFrames = 0;
    } else if (controlMs < targetMs * 0.72f) {
        vkrt->runtime.autoSPPFastFrames++;
        vkrt->runtime.autoSPPSlowFrames = 0;
    } else {
        vkrt->runtime.autoSPPFastFrames = 0;
        vkrt->runtime.autoSPPSlowFrames = 0;
    }

    if (vkrt->runtime.autoSPPCooldownFrames > 0) {
        vkrt->runtime.autoSPPCooldownFrames--;
        return;
    }

    uint32_t spp = vkrt->state.samplesPerPixel;
    uint8_t changed = 0;

    if (vkrt->runtime.autoSPPSlowFrames >= 6) {
        uint32_t next = nextSPPStep(spp, 0, controlMs > targetMs * 1.30f);
        if (next != spp) {
            VKRT_setSamplesPerPixel(vkrt, next);
            changed = 1;
        }
        vkrt->runtime.autoSPPSlowFrames = 0;
    } else if (vkrt->runtime.autoSPPFastFrames >= 18) {
        uint32_t next = nextSPPStep(spp, 1, 0);
        if (next != spp) {
            VKRT_setSamplesPerPixel(vkrt, next);
            changed = 1;
        }
        vkrt->runtime.autoSPPFastFrames = 0;
    }

    if (changed) {
        vkrt->runtime.autoSPPCooldownFrames = 12;
    }
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
