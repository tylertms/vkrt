#include "interface.h"
#include "device.h"

#include "dcimgui.h"
#include "dcimgui_impl_glfw.h"
#include "dcimgui_impl_vulkan.h"
#include "dcimgui_internal.h"

#include <stdint.h>
#include <math.h>

void pollCameraMovement(VKRT* vkrt) {
    const float panSpeed = 0.0015f;
    const float orbitSpeed = 0.004f;
    const float zoomSpeed = -0.1f;
    const float minDist = 0.001f, maxDist = 10000.0f;

    ImGuiIO* io = ImGui_GetIO();
    if (io->WantCaptureMouse) return;

    vec3 viewDir;
    glm_vec3_sub(vkrt->camera.target, vkrt->camera.pos, viewDir);
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

    if (ImGui_IsMouseDragging(ImGuiMouseButton_Right, -1.0f)) {
        float dx = io->MouseDelta.x;
        float dy = io->MouseDelta.y;

        vec3 move, tmp;
        glm_vec3_scale(right, -dx * panSpeed * dist, move);
        glm_vec3_scale(up,    -dy * panSpeed * dist, tmp);
        glm_vec3_add(move, tmp, move);

        glm_vec3_add(vkrt->camera.pos, move, vkrt->camera.pos);
        glm_vec3_add(vkrt->camera.target, move, vkrt->camera.target);
        updateMatricesFromCamera(vkrt);
    }

    if (ImGui_IsMouseDragging(ImGuiMouseButton_Left, -1.0f)) {
        float dx = io->MouseDelta.x;
        float dy = io->MouseDelta.y;

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
        glm_vec3_sub(vkrt->camera.target, offset, vkrt->camera.pos);

        updateMatricesFromCamera(vkrt);
    }

    float scroll = io->MouseWheel;
    if (scroll != 0.0f) {
        glm_vec3_scale(viewDir, scroll * zoomSpeed, viewDir);
        glm_vec3_sub(vkrt->camera.pos, viewDir, vkrt->camera.pos);
        updateMatricesFromCamera(vkrt);
    }
}

void setupSceneUniform(VKRT* vkrt) {
    vkrt->camera = (Camera){
        .width = WIDTH, .height = HEIGHT,
        .nearZ = 0.001f, .farZ = 10000.0f,
        .vfov = 40.0f,
        .pos = {-0.5f, 0.2f, -0.2f},
        .target = {0.0f, 0.0f, 0.0f},
        .up = {0.0f, 0.0f, 1.0f}
    };
    resetSceneFrame(vkrt);
    updateMatricesFromCamera(vkrt);
}

void resetSceneFrame(VKRT* vkrt) {
    vkrt->sceneData->maxRayDepth = 16;
    vkrt->sceneData->samplesPerPixel = 1;
    vkrt->sceneData->frameNumber = 0;
    vkrt->sceneData->totalSamples = 0;

    vkrt->averageFrametime = 0.0f;
    vkrt->frametimeStartIndex = 0;
    memset(vkrt->frametimes, 0, sizeof(vkrt->frametimes));
}

void updateMatricesFromCamera(VKRT* vkrt) {
    mat4 view, proj;
    Camera cam = vkrt->camera;

    glm_lookat(cam.pos, cam.target, cam.up, view);
    glm_perspective(glm_rad(cam.vfov), (float)cam.width / cam.height, cam.nearZ, cam.farZ, proj);

    glm_mat4_inv(view, vkrt->sceneData->viewInverse);
    glm_mat4_inv(proj, vkrt->sceneData->projInverse);

    resetSceneFrame(vkrt);
}

void setDarkTheme() {
    ImGuiStyle* style = ImGui_GetStyle();

    ImVec4* colors = style->Colors;

    const ImVec4 almostBlack = (ImVec4){0.05f, 0.05f, 0.05f, 1.00f};
    const ImVec4 darkGray = (ImVec4){0.10f, 0.10f, 0.10f, 1.00f};
    const ImVec4 midGray = (ImVec4){0.15f, 0.15f, 0.15f, 1.00f};
    const ImVec4 lightGray = (ImVec4){0.25f, 0.25f, 0.25f, 1.00f};
    const ImVec4 textColor = (ImVec4){1.00f, 1.00f, 1.00f, 1.00f};

    colors[ImGuiCol_Text] = textColor;
    colors[ImGuiCol_TextDisabled] = (ImVec4){0.50f, 0.50f, 0.50f, 1.00f};
    colors[ImGuiCol_WindowBg] = (ImVec4){0.05f, 0.05f, 0.05f, 0.60f};
    colors[ImGuiCol_ChildBg] = darkGray;
    colors[ImGuiCol_PopupBg] = almostBlack;
    colors[ImGuiCol_Border] = midGray;
    colors[ImGuiCol_FrameBg] = (ImVec4){0.05f, 0.05f, 0.05f, 0.60f};
    colors[ImGuiCol_FrameBgHovered] = lightGray;
    colors[ImGuiCol_FrameBgActive] = lightGray;
    colors[ImGuiCol_TitleBg] = darkGray;
    colors[ImGuiCol_TitleBgActive] = midGray;
    colors[ImGuiCol_TitleBgCollapsed] = almostBlack;
    colors[ImGuiCol_MenuBarBg] = darkGray;
    colors[ImGuiCol_ScrollbarBg] = darkGray;
    colors[ImGuiCol_ScrollbarGrab] = midGray;
    colors[ImGuiCol_ScrollbarGrabHovered] = lightGray;
    colors[ImGuiCol_ScrollbarGrabActive] = lightGray;
    colors[ImGuiCol_CheckMark] = textColor;
    colors[ImGuiCol_SliderGrab] = (ImVec4){0.40f, 0.40f, 0.40f, 1.00f};
    colors[ImGuiCol_SliderGrabActive] = (ImVec4){0.30f, 0.30f, 0.30f, 1.00f};
    colors[ImGuiCol_Button] = midGray;
    colors[ImGuiCol_ButtonHovered] = lightGray;
    colors[ImGuiCol_ButtonActive] = lightGray;
    colors[ImGuiCol_Header] = midGray;
    colors[ImGuiCol_HeaderHovered] = lightGray;
    colors[ImGuiCol_HeaderActive] = lightGray;
    colors[ImGuiCol_Separator] = midGray;
    colors[ImGuiCol_SeparatorHovered] = lightGray;
    colors[ImGuiCol_SeparatorActive] = lightGray;
    colors[ImGuiCol_ResizeGrip] = midGray;
    colors[ImGuiCol_ResizeGripHovered] = lightGray;
    colors[ImGuiCol_ResizeGripActive] = lightGray;
    colors[ImGuiCol_Tab] = midGray;
    colors[ImGuiCol_TabHovered] = lightGray;
    colors[ImGuiCol_TabActive] = lightGray;
    colors[ImGuiCol_TabUnfocused] = darkGray;
    colors[ImGuiCol_TabUnfocusedActive] = midGray;
    colors[ImGuiCol_TabSelectedOverline] = (ImVec4){0.00f, 0.00f, 0.00f, 0.00f};
    colors[ImGuiCol_DockingPreview] = lightGray;
    colors[ImGuiCol_PlotLines] = (ImVec4){0.61f, 0.61f, 0.61f, 1.00f};
    colors[ImGuiCol_PlotLinesHovered] = lightGray;
    colors[ImGuiCol_PlotHistogram] = midGray;
    colors[ImGuiCol_PlotHistogramHovered] = lightGray;
    colors[ImGuiCol_TextSelectedBg] = midGray;
    colors[ImGuiCol_DragDropTarget] = lightGray;
    colors[ImGuiCol_NavHighlight] = lightGray;
    colors[ImGuiCol_NavWindowingHighlight] = lightGray;
    colors[ImGuiCol_NavWindowingDimBg] = darkGray;
    colors[ImGuiCol_ModalWindowDimBg] = almostBlack;
}