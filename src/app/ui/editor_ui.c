#include "editor_ui.h"

#include "dcimgui.h"
#include "dcimgui_impl_glfw.h"
#include "dcimgui_impl_vulkan.h"
#include "dcimgui_internal.h"
#include "editor_state.h"
#include "editor_theme.h"
#include "tinyfiledialogs.h"
#include "IBMPlexMono_Regular.h"

#include <math.h>
#include <stdio.h>

static const char* openMeshImportDialog(void) {
    const char* filters[] = {"*.glb", "*.gltf"};
    return tinyfd_openFileDialog("Import mesh", "assets/models", 2, filters, "glTF files", 0);
}

static void initializeDockLayout(void) {
    static bool isInitialized = false;
    if (isInitialized) return;

    isInitialized = true;
    const ImGuiViewport* viewport = ImGui_GetMainViewport();
    ImGuiID dockspaceID = ImGui_GetID("WorkspaceDockspace");

    ImGui_DockBuilderRemoveNode(dockspaceID);
    ImGui_DockBuilderAddNodeEx(dockspaceID, ImGuiDockNodeFlags_DockSpace);
    ImGui_DockBuilderSetNodeSize(dockspaceID, viewport->Size);

    ImGuiID inspectorDockID = 0;
    ImGuiID viewportDockID = dockspaceID;
    ImGui_DockBuilderSplitNode(dockspaceID, ImGuiDir_Left, 0.26f, &inspectorDockID, &viewportDockID);
    ImGui_DockBuilderDockWindow("Scene Inspector", inspectorDockID);
    ImGui_DockBuilderDockWindow("Viewport", viewportDockID);
    ImGui_DockBuilderFinish(dockspaceID);
}

static bool drawWorkspaceDockspace(void) {
    const ImGuiViewport* mainViewport = ImGui_GetMainViewport();

    ImGui_SetNextWindowPos(mainViewport->Pos, ImGuiCond_Always);
    ImGui_SetNextWindowSize(mainViewport->Size, ImGuiCond_Always);
    ImGui_SetNextWindowViewport(mainViewport->ID);

    ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                                 ImGuiWindowFlags_NoBackground;

    ImGui_PushStyleVarImVec2(ImGuiStyleVar_WindowPadding, (ImVec2){0.0f, 0.0f});
    ImGui_PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui_Begin("Workspace", NULL, hostFlags);
    ImGui_PopStyleVarEx(2);

    ImGuiID dockspaceID = ImGui_GetID("WorkspaceDockspace");
    ImGui_DockSpaceEx(dockspaceID, (ImVec2){0.0f, 0.0f}, ImGuiDockNodeFlags_None, NULL);
    initializeDockLayout();

    ImGui_End();
    return true;
}

static bool drawViewportWindow(VKRT* runtime) {
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_WindowPadding, (ImVec2){0.0f, 0.0f});
    ImGui_Begin("Viewport", NULL, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                                     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);

    bool viewportHovered = ImGui_IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);

    ImVec2 contentPosition = ImGui_GetCursorScreenPos();
    ImVec2 contentSize = ImGui_GetContentRegionAvail();
    ImVec2 framebufferScale = ImGui_GetIO()->DisplayFramebufferScale;

    float x0 = floorf(contentPosition.x * framebufferScale.x);
    float y0 = floorf(contentPosition.y * framebufferScale.y);
    float x1 = ceilf((contentPosition.x + contentSize.x) * framebufferScale.x);
    float y1 = ceilf((contentPosition.y + contentSize.y) * framebufferScale.y);

    float width = x1 - x0;
    float height = y1 - y0;

    uint32_t viewportX = width > 0.0f ? (uint32_t)x0 : 0;
    uint32_t viewportY = height > 0.0f ? (uint32_t)y0 : 0;
    uint32_t viewportWidth = width > 1.0f ? (uint32_t)width : 1;
    uint32_t viewportHeight = height > 1.0f ? (uint32_t)height : 1;
    VKRT_setRenderViewport(runtime, viewportX, viewportY, viewportWidth, viewportHeight);

    ImGui_End();
    ImGui_PopStyleVar();

    return viewportHovered;
}

static void drawPerformanceSection(const VKRT* runtime) {
    ImGui_Separator();
    ImGui_Text("FPS:                %6d", runtime->state.framesPerSecond);
    ImGui_Text("Render time:        %6.3f ms", runtime->state.renderTimeMs);
    ImGui_Text("Frame time:         %6.3f ms", runtime->state.displayTimeMs);
    ImGui_PlotLinesEx("##frametimes", runtime->state.frametimes, COUNT_OF(runtime->state.frametimes),
                      (int)runtime->state.frametimeStartIndex, "", 0.0f,
                      2 * runtime->state.averageFrametime, (ImVec2){220.0f, 60.0f}, sizeof(float));
}

static void drawMeshInspector(VKRT* runtime, EditorState* state) {
    uint32_t meshCount = VKRT_getMeshCount(runtime);
    if (meshCount == 0) {
        ImGui_TextDisabled("No meshes loaded");
        return;
    }

    for (uint32_t meshIndex = 0; meshIndex < meshCount; meshIndex++) {
        Mesh* mesh = &runtime->core.meshes[meshIndex];
        MeshInfo* meshInfo = &mesh->info;

        char header[160] = {0};
        snprintf(header, sizeof(header), "Mesh %u (%s)", meshIndex, editorStateGetMeshName(state, meshIndex));

        ImGui_PushIDInt((int)meshIndex);
        bool visible = true;
        bool open = ImGui_CollapsingHeaderBoolPtr(header, &visible, ImGuiTreeNodeFlags_None);
        if (!visible) {
            state->pendingMeshRemovalIndex = meshIndex;
            ImGui_PopID();
            continue;
        }

        if (!mesh->ownsGeometry && mesh->geometrySource < meshCount) {
            ImGui_SameLine();
            ImGui_TextDisabled("-> %u", mesh->geometrySource);
        }

        if (!open) {
            ImGui_PopID();
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
            VKRT_setMeshTransform(runtime, meshIndex, position, rotation, scale);
        }

        ImGui_PopID();
    }
}

static void drawSceneInspectorWindow(VKRT* runtime, EditorState* state) {
    ImGui_PushStyleColorImVec4(ImGuiCol_WindowBg, (ImVec4){0.08f, 0.08f, 0.08f, 1.00f});
    ImGui_Begin("Scene Inspector", NULL, 0);
    ImGui_PopStyleColor();

    ImGui_Text("Device: %s", runtime->core.deviceName);
    ImGui_Text("Resolution: %dx%d", runtime->state.camera.width, runtime->state.camera.height);

    if (ImGui_Checkbox("V-Sync", (bool*)&runtime->runtime.vsync)) {
        runtime->runtime.framebufferResized = VK_TRUE;
    }

    drawPerformanceSection(runtime);
    ImGui_Separator();

    if (ImGui_Button("Import mesh")) {
        const char* selectedPath = openMeshImportDialog();
        if (selectedPath && selectedPath[0]) {
            editorStateQueueMeshImport(state, selectedPath);
        }
    }

    drawMeshInspector(runtime, state);
    ImGui_End();
}

static void applyCameraInput(VKRT* runtime, bool viewportHovered) {
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
    VKRT_applyCameraInput(runtime, &cameraInput);
}

void editorUIInitialize(VKRT* runtime, void* userData) {
    (void)userData;
    ImGui_CreateContext(NULL);

    ImGuiIO* io = ImGui_GetIO();
    io->ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImFontAtlas_AddFontFromMemoryTTF(io->Fonts, (void*)IBMPlexMono_Regular, IBMPlexMono_Regular_len, 22.0, NULL, NULL);
    editorThemeApplyDefault();

    cImGui_ImplGlfw_InitForVulkan(runtime->runtime.window, true);

    uint32_t imageCount = (uint32_t)runtime->runtime.swapChainImageCount;
    uint32_t minImageCount = (imageCount > 1u) ? (imageCount - 1u) : imageCount;

    ImGui_ImplVulkan_InitInfo initInfo = {
        .Instance = runtime->core.instance,
        .PhysicalDevice = runtime->core.physicalDevice,
        .Device = runtime->core.device,
        .QueueFamily = (uint32_t)runtime->core.indices.graphics,
        .Queue = runtime->core.graphicsQueue,
        .PipelineCache = VK_NULL_HANDLE,
        .DescriptorPool = runtime->core.descriptorPool,
        .Allocator = VK_NULL_HANDLE,
        .MinImageCount = minImageCount,
        .ImageCount = imageCount,
        .CheckVkResultFn = VK_NULL_HANDLE,
        .RenderPass = runtime->runtime.renderPass,
    };

    cImGui_ImplVulkan_Init(&initInfo);
    cImGui_ImplVulkan_CreateFontsTexture();
}

void editorUIShutdown(VKRT* runtime, void* userData) {
    (void)runtime;
    (void)userData;

    cImGui_ImplVulkan_Shutdown();
    cImGui_ImplGlfw_Shutdown();
    ImGui_DestroyContext(NULL);
}

void editorUIDraw(VKRT* runtime, VkCommandBuffer commandBuffer, void* userData) {
    EditorState* state = (EditorState*)userData;

    cImGui_ImplGlfw_NewFrame();
    cImGui_ImplVulkan_NewFrame();
    ImGui_NewFrame();

    drawWorkspaceDockspace();
    bool viewportHovered = drawViewportWindow(runtime);
    drawSceneInspectorWindow(runtime, state);
    applyCameraInput(runtime, viewportHovered);

    ImGui_Render();
    cImGui_ImplVulkan_RenderDrawData(ImGui_GetDrawData(), commandBuffer);
}
