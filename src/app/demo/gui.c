#include "gui.h"

#include "dcimgui.h"
#include "dcimgui_impl_glfw.h"
#include "dcimgui_impl_vulkan.h"
#include "dcimgui_internal.h"
#include <math.h>
#include <stdio.h>

#include "tinyfiledialogs.h"

static const char* openMeshFileDialog(void) {
    const char* filters[] = {"*.glb", "*.gltf"};
    return tinyfd_openFileDialog(
        "Select mesh",
        "assets/models",
        2,
        filters,
        "glTF files",
        0
    );
}

// Called after all of Vulkan has been initialized
void initGUI(VKRT* vkrt, void* userData) {
    (void)userData;

    ImGui_CreateContext(NULL);

    ImGuiIO* io = ImGui_GetIO();
    io->ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    setDefaultStyle();

    cImGui_ImplGlfw_InitForVulkan(vkrt->runtime.window, true);

    uint32_t imgCount = (uint32_t)vkrt->runtime.swapChainImageCount;
    uint32_t minImgCount = (imgCount > 1u) ? (imgCount - 1u) : imgCount;
    ImGui_ImplVulkan_InitInfo imGuiVulkanInitInfo = {
        .Instance = vkrt->core.instance,
        .PhysicalDevice = vkrt->core.physicalDevice,
        .Device = vkrt->core.device,
        .QueueFamily = (uint32_t)vkrt->core.indices.graphics,
        .Queue = vkrt->core.graphicsQueue,
        .PipelineCache = VK_NULL_HANDLE,
        .DescriptorPool = vkrt->core.descriptorPool,
        .Allocator = VK_NULL_HANDLE,
        .MinImageCount = minImgCount,
        .ImageCount = imgCount,
        .CheckVkResultFn = VK_NULL_HANDLE,
        .RenderPass = vkrt->runtime.renderPass,
    };

    cImGui_ImplVulkan_Init(&imGuiVulkanInitInfo);
    cImGui_ImplVulkan_CreateFontsTexture();
}

void deinitGUI(VKRT* vkrt, void* userData) {
    (void)vkrt;
    (void)userData;
    cImGui_ImplVulkan_Shutdown();
    cImGui_ImplGlfw_Shutdown();
    ImGui_DestroyContext(NULL);
}

void drawGUI(VKRT* vkrt, VkCommandBuffer commandBuffer, void* userData) {
    DemoGUIState* guiState = (DemoGUIState*)userData;

    cImGui_ImplGlfw_NewFrame();
    cImGui_ImplVulkan_NewFrame();
    ImGui_NewFrame();

    const ImGuiViewport* mainViewport = ImGui_GetMainViewport();
    ImGuiID dockspaceID = ImGui_GetID("MainDockSpace");

    ImGui_SetNextWindowPos(mainViewport->Pos, ImGuiCond_Always);
    ImGui_SetNextWindowSize(mainViewport->Size, ImGuiCond_Always);
    ImGui_SetNextWindowViewport(mainViewport->ID);

    ImGuiWindowFlags dockHostFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                                     ImGuiWindowFlags_NoBackground;

    ImGui_PushStyleVarImVec2(ImGuiStyleVar_WindowPadding, (ImVec2){0.0f, 0.0f});
    ImGui_PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui_Begin("MainDockHost", NULL, dockHostFlags);
    ImGui_PopStyleVarEx(2);

    ImGui_DockSpaceEx(dockspaceID, (ImVec2){0.0f, 0.0f}, ImGuiDockNodeFlags_None, NULL);

    static int dockLayoutInitialized = 0;
    if (!dockLayoutInitialized) {
        dockLayoutInitialized = 1;

        ImGui_DockBuilderRemoveNode(dockspaceID);
        ImGui_DockBuilderAddNodeEx(dockspaceID, ImGuiDockNodeFlags_DockSpace);
        ImGui_DockBuilderSetNodeSize(dockspaceID, mainViewport->Size);

        ImGuiID sceneDockID = 0;
        ImGuiID viewportDockID = dockspaceID;
        ImGui_DockBuilderSplitNode(dockspaceID, ImGuiDir_Left, 0.24f, &sceneDockID, &viewportDockID);
        ImGui_DockBuilderDockWindow("Scene", sceneDockID);
        ImGui_DockBuilderDockWindow("Viewport", viewportDockID);
        ImGui_DockBuilderFinish(dockspaceID);
    }

    ImGui_End();

    ImGui_PushStyleVarImVec2(ImGuiStyleVar_WindowPadding, (ImVec2){0.0f, 0.0f});
    ImGui_Begin("Viewport", NULL, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
    bool viewportHovered = ImGui_IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);

    ImVec2 contentPos = ImGui_GetCursorScreenPos();
    ImVec2 contentAvail = ImGui_GetContentRegionAvail();
    ImVec2 fbScale = ImGui_GetIO()->DisplayFramebufferScale;

    float viewportX0 = floorf(contentPos.x * fbScale.x);
    float viewportY0 = floorf(contentPos.y * fbScale.y);
    float viewportX1 = ceilf((contentPos.x + contentAvail.x) * fbScale.x);
    float viewportY1 = ceilf((contentPos.y + contentAvail.y) * fbScale.y);

    float viewportW = viewportX1 - viewportX0;
    float viewportH = viewportY1 - viewportY0;

    uint32_t renderX = viewportW > 0.0f ? (uint32_t)viewportX0 : 0;
    uint32_t renderY = viewportH > 0.0f ? (uint32_t)viewportY0 : 0;
    uint32_t renderW = viewportW > 1.0f ? (uint32_t)viewportW : 1;
    uint32_t renderH = viewportH > 1.0f ? (uint32_t)viewportH : 1;
    VKRT_setRenderViewport(vkrt, renderX, renderY, renderW, renderH);

    ImGui_End();
    ImGui_PopStyleVar();

    ImGui_PushStyleColorImVec4(ImGuiCol_WindowBg, (ImVec4){0.08f, 0.08f, 0.08f, 1.00f});
    ImGui_Begin("Scene", NULL, 0);
    ImGui_PopStyleColor();

    ImGui_Text("Device: %s", vkrt->core.deviceName);
    ImGui_Text("Resolution: %dx%d", vkrt->state.camera.width, vkrt->state.camera.height);

    if (ImGui_Checkbox("V-Sync", (bool*)&vkrt->runtime.vsync)) {
        vkrt->runtime.framebufferResized = VK_TRUE;
    }

    ImGui_Separator();
    ImGui_Text("FPS:                %6d", vkrt->state.framesPerSecond);
    ImGui_Text("Render time:        %6.3f ms", vkrt->state.renderTimeMs);
    ImGui_Text("Frame time:         %6.3f ms", vkrt->state.displayTimeMs);
    ImGui_Text("Average frame time: %6.3f ms", vkrt->state.averageFrametime);

    ImGui_PlotLinesEx("##frametimes", vkrt->state.frametimes, COUNT_OF(vkrt->state.frametimes), (int)vkrt->state.frametimeStartIndex, "", 0.0f, 2 * vkrt->state.averageFrametime, (ImVec2){220.0f, 60.0f}, sizeof(float));

    ImGui_Separator();
    ImGui_Text("Meshes");

    if (ImGui_ButtonEx("+ Add Mesh", (ImVec2){-1.0f, 0.0f})) {
        const char* selectedPath = openMeshFileDialog();
        if (selectedPath && selectedPath[0]) {
            demoGUIQueueAddMeshPath(guiState, selectedPath);
        }
    }

    uint32_t meshCount = VKRT_getMeshCount(vkrt);
    if (meshCount == 0) {
        ImGui_TextDisabled("No meshes loaded");
    }

    for (uint32_t i = 0; i < meshCount;) {
        Mesh* mesh = &vkrt->core.meshes[i];
        MeshInfo* meshInfo = &mesh->info;

        char header[160] = {0};
        snprintf(header, sizeof(header), "Mesh %u (%s)", i, demoGUIGetMeshLabel(guiState, i));

        ImGui_PushIDInt((int)i);
        bool visible = true;
        bool open = ImGui_CollapsingHeaderBoolPtr(header, &visible, ImGuiTreeNodeFlags_DefaultOpen);
        if (!visible) {
            guiState->pendingRemoveIndex = i;
            ImGui_PopID();
            i++;
            continue;
        }

        if (!mesh->ownsGeometry && mesh->geometrySource < meshCount) {
            ImGui_SameLine();
            ImGui_TextDisabled("-> %u", mesh->geometrySource);
        }

        if (!open) {
            ImGui_PopID();
            i++;
            continue;
        }

        float position[3] = {meshInfo->position[0], meshInfo->position[1], meshInfo->position[2]};
        float rotation[3] = {meshInfo->rotation[0], meshInfo->rotation[1], meshInfo->rotation[2]};
        float scale[3] = {meshInfo->scale[0], meshInfo->scale[1], meshInfo->scale[2]};

        bool transformChanged = false;
        transformChanged |= ImGui_DragFloat3Ex("Translate", position, 0.001f, 0.0f, 0.0f, "%.3f", ImGuiSliderFlags_None);
        transformChanged |= ImGui_DragFloat3Ex("Rotate", rotation, 0.05f, 0.0f, 0.0f, "%.2f", ImGuiSliderFlags_None);
        transformChanged |= ImGui_DragFloat3Ex("Scale", scale, 0.001f, 0.001f, 1000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);

        if (transformChanged) {
            for (int axis = 0; axis < 3; axis++) {
                rotation[axis] = fmodf(rotation[axis], 360.0f);
                if (rotation[axis] < -180.0f) rotation[axis] += 360.0f;
                if (rotation[axis] >= 180.0f) rotation[axis] -= 360.0f;
            }

            VKRT_setMeshTransform(vkrt, i, position, rotation, scale);
        }

        ImGui_PopID();
        i++;
    }
    ImGui_End();

    ImGuiIO* io = ImGui_GetIO();
    VKRT_CameraInput cameraInput = {
        .orbitDx = io->MouseDelta.x,
        .orbitDy = io->MouseDelta.y,
        .panDx = io->MouseDelta.x,
        .panDy = io->MouseDelta.y,
        .scroll = io->MouseWheel,
        .orbiting = viewportHovered && ImGui_IsMouseDragging(ImGuiMouseButton_Left, -1.0f),
        .panning = viewportHovered && ImGui_IsMouseDragging(ImGuiMouseButton_Right, -1.0f),
        .captureMouse = !viewportHovered,
    };
    VKRT_applyCameraInput(vkrt, &cameraInput);

    ImGui_Render();
    cImGui_ImplVulkan_RenderDrawData(ImGui_GetDrawData(), commandBuffer);
}

void setDefaultStyle() {
    ImGuiStyle* style = ImGui_GetStyle();
    style->WindowRounding = 8.0f;
    style->GrabRounding = 8.0f;
    style->FrameRounding = 4.0f;
    style->FrameBorderSize = 0.0f;
    style->WindowBorderSize = 0.0f;
    style->DockingSeparatorSize = 1.0f;
    style->WindowPadding = (ImVec2){8, 5};

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
