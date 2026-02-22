#include "editor.h"

#include "dcimgui.h"
#include "dcimgui_impl_glfw.h"
#include "dcimgui_impl_vulkan.h"
#include "dcimgui_internal.h"
#include "session.h"
#include "theme.h"
#include "debug.h"
#include "tinyfiledialogs.h"
#include "IBMPlexMono_Regular.h"
#include "fa_solid_900.h"
#include "IconsFontAwesome6.h"

#include <math.h>
#include <float.h>
#include <stdbool.h>
#include <stdio.h>

static const float kEditorBaseTextSizePx = 15.5f;
static const float kEditorBaseIconSizePx = 11.0f;
static const float kEditorScaleEpsilon = 0.01f;
static float gEditorUIScale = 0.0f;

static const char* openMeshImportDialog(void) {
    const char* filters[] = {"*.glb", "*.gltf"};
    return tinyfd_openFileDialog("Import mesh", "assets/models", 2, filters, "glTF 2.0 (.glb/.gltf)", 0);
}

static ImFontConfig makeDefaultFontConfig(void) {
    ImFontConfig config = {0};
    config.FontDataOwnedByAtlas = true;
    config.OversampleH = 0;
    config.OversampleV = 0;
    config.GlyphMaxAdvanceX = FLT_MAX;
    config.RasterizerMultiply = 1.0f;
    config.RasterizerDensity = 1.0f;
    config.EllipsisChar = 0;
    return config;
}

static float queryEditorContentScale(GLFWwindow* window) {
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    glfwGetWindowContentScale(window, &scaleX, &scaleY);

    float scale = fmaxf(scaleX, scaleY);
    if (!isfinite(scale) || scale <= 0.0f) return 1.0f;
    return scale;
}

static void rebuildEditorFonts(ImGuiIO* io, float uiScale) {
    ImFontAtlas_Clear(io->Fonts);

    ImFontConfig textConfig = makeDefaultFontConfig();
    textConfig.FontDataOwnedByAtlas = false;
    ImFontAtlas_AddFontFromMemoryTTF(io->Fonts, (void*)IBMPlexMono_Regular, IBMPlexMono_Regular_len, kEditorBaseTextSizePx * uiScale, &textConfig, NULL);

    ImFontConfig iconConfig = makeDefaultFontConfig();
    iconConfig.FontDataOwnedByAtlas = false;
    iconConfig.MergeMode = true;
    iconConfig.PixelSnapH = true;
    static const ImWchar iconRanges[] = {ICON_MIN_FA, ICON_MAX_FA, 0};
    ImFontAtlas_AddFontFromMemoryTTF(io->Fonts, (void*)fa_solid_900, fa_solid_900_len, kEditorBaseIconSizePx * uiScale, &iconConfig, iconRanges);
}

static void applyEditorUIScale(float uiScale, bool refreshVulkanFonts) {
    if (gEditorUIScale > 0.0f && fabsf(uiScale - gEditorUIScale) <= kEditorScaleEpsilon) return;

    ImGuiIO* io = ImGui_GetIO();
    ImGuiStyle* style = ImGui_GetStyle();
    if (gEditorUIScale <= 0.0f) {
        editorThemeApplyDefault();
        ImGuiStyle_ScaleAllSizes(style, uiScale);
    } else {
        ImGuiStyle_ScaleAllSizes(style, uiScale / gEditorUIScale);
    }

    rebuildEditorFonts(io, uiScale);
    gEditorUIScale = uiScale;

    if (refreshVulkanFonts) {
        cImGui_ImplVulkan_CreateFontsTexture();
    }
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

static bool drawViewportWindow(VKRT* vkrt) {
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
    VKRT_setRenderViewport(vkrt, viewportX, viewportY, viewportWidth, viewportHeight);

    ImGui_End();
    ImGui_PopStyleVar();

    return viewportHovered;
}

static void drawPerformanceSection(VKRT* vkrt) {
    ImGui_Separator();
    ImGui_Text("FPS:          %8u", vkrt->state.framesPerSecond);
    ImGui_Text("Frames:       %8u", vkrt->state.accumulationFrame);
    ImGui_Text("Samples:  %12llu", (unsigned long long)vkrt->state.totalSamples);
    ImGui_Text("Samples / px: %8u", vkrt->state.samplesPerPixel);
    ImGui_Text("Frame (ms):   %8.3f ms", vkrt->state.displayFrameTimeMs);
    ImGui_Text("Render (ms):  %8.3f ms", vkrt->state.displayRenderTimeMs);

    bool autoSPP = vkrt->state.autoSPPEnabled != 0;
    if (ImGui_Checkbox("Auto SPP", &autoSPP)) {
        VKRT_setAutoSPPEnabled(vkrt, autoSPP ? 1 : 0);
    }

    if (autoSPP) {
        int targetFPS = (int)vkrt->state.autoSPPTargetFPS;
        if (ImGui_SliderIntEx("Target FPS", &targetFPS, 30, 360, "%d", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic)) {
            VKRT_setAutoSPPTargetFPS(vkrt, (uint32_t)targetFPS);
        }
    } else {
        int spp = (int)vkrt->state.samplesPerPixel;
        if (ImGui_SliderIntEx("SPP", &spp, 1, 2048, "%d", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic)) {
            VKRT_setSamplesPerPixel(vkrt, (uint32_t)spp);
        }
    }

    int maxBounces = (int)vkrt->state.maxBounces;
    if (ImGui_SliderIntEx("Max Bounces", &maxBounces, 1, 64, "%d", ImGuiSliderFlags_AlwaysClamp)) {
        vkrt->state.maxBounces = (uint32_t)maxBounces;
        VKRT_invalidateAccumulation(vkrt);
    }

    const char* toneMappingLabels[] = {"None", "ACES"};
    int toneMappingMode = (int)vkrt->state.toneMappingMode;
    if (ImGui_ComboCharEx("Tone Mapping", &toneMappingMode, toneMappingLabels, 2, 2)) {
        VKRT_setToneMappingMode(vkrt, (VKRT_ToneMappingMode)toneMappingMode);
    }

    if (ImGui_Button(ICON_FA_ARROWS_ROTATE " Reset accumulation")) {
        VKRT_invalidateAccumulation(vkrt);
    }
}

static void drawMeshInspector(VKRT* vkrt, Session* session) {
    uint32_t meshCount = VKRT_getMeshCount(vkrt);
    if (meshCount == 0) {
        ImGui_TextDisabled("No meshes loaded.");
        return;
    }

    for (uint32_t meshIndex = 0; meshIndex < meshCount; meshIndex++) {
        Mesh* mesh = &vkrt->core.meshes[meshIndex];
        MeshInfo* meshInfo = &mesh->info;

        char header[160] = {0};
        snprintf(header, sizeof(header), "Mesh %u (%s)", meshIndex, sessionGetMeshName(session, meshIndex));

        ImGui_PushIDInt((int)meshIndex);
        bool visible = true;
        bool open = ImGui_CollapsingHeaderBoolPtr(header, &visible, ImGuiTreeNodeFlags_None);
        if (!visible) {
            session->pendingMeshRemovalIndex = meshIndex;
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
            VKRT_setMeshTransform(vkrt, meshIndex, position, rotation, scale);
        }

        MaterialData material = mesh->material;
        bool materialChanged = false;
        materialChanged |= ImGui_ColorEdit3("Base Color", material.baseColor, ImGuiColorEditFlags_Float);
        materialChanged |= ImGui_SliderFloatEx("Roughness", &material.roughness, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        materialChanged |= ImGui_SliderFloatEx("Specular", &material.specular, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        materialChanged |= ImGui_ColorEdit3("Emission Color", material.emissionColor, ImGuiColorEditFlags_Float);
        materialChanged |= ImGui_DragFloatEx("Emission Strength", &material.emissionStrength, 0.01f, 0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);

        if (materialChanged) {
            VKRT_setMeshMaterial(vkrt, meshIndex, &material);
        }

        ImGui_PopID();
    }
}

static void drawSceneInspectorWindow(VKRT* vkrt, Session* session) {
    ImGui_PushStyleColorImVec4(ImGuiCol_WindowBg, (ImVec4){0.08f, 0.08f, 0.08f, 1.00f});
    ImGui_Begin("Scene Inspector", NULL, 0);
    ImGui_PopStyleColor();

    ImGui_Text("Device: %s", vkrt->core.deviceName);
    ImGui_Text("Resolution: %dx%d", vkrt->state.camera.width, vkrt->state.camera.height);

    bool vsync = vkrt->runtime.vsync != 0;
    if (ImGui_Checkbox("V-Sync", &vsync)) {
        vkrt->runtime.vsync = vsync ? 1 : 0;
        vkrt->runtime.framebufferResized = VK_TRUE;
    }

    drawPerformanceSection(vkrt);
    ImGui_Separator();

    if (ImGui_Button("Import mesh")) {
        const char* selectedPath = openMeshImportDialog();
        if (selectedPath && selectedPath[0]) {
            sessionQueueMeshImport(session, selectedPath);
        }
    }

    drawMeshInspector(vkrt, session);
    ImGui_End();
}

static void applyEditorCameraInput(VKRT* vkrt, bool viewportHovered) {
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
}

void editorUIInitialize(VKRT* vkrt, void* userData) {
    (void)userData;
    ImGui_CreateContext(NULL);

    ImGuiIO* io = ImGui_GetIO();
    io->ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    float uiScale = queryEditorContentScale(vkrt->runtime.window);
    applyEditorUIScale(uiScale, false);

    cImGui_ImplGlfw_InitForVulkan(vkrt->runtime.window, true);

    uint32_t imageCount = (uint32_t)vkrt->runtime.swapChainImageCount;
    uint32_t minImageCount = (imageCount > 1u) ? (imageCount - 1u) : imageCount;

    ImGui_ImplVulkan_InitInfo initInfo = {
        .Instance = vkrt->core.instance,
        .PhysicalDevice = vkrt->core.physicalDevice,
        .Device = vkrt->core.device,
        .QueueFamily = (uint32_t)vkrt->core.indices.graphics,
        .Queue = vkrt->core.graphicsQueue,
        .PipelineCache = VK_NULL_HANDLE,
        .DescriptorPool = vkrt->core.descriptorPool,
        .Allocator = VK_NULL_HANDLE,
        .MinImageCount = minImageCount,
        .ImageCount = imageCount,
        .CheckVkResultFn = VK_NULL_HANDLE,
        .RenderPass = vkrt->runtime.renderPass,
    };

    cImGui_ImplVulkan_Init(&initInfo);
    cImGui_ImplVulkan_CreateFontsTexture();
}

void editorUIShutdown(VKRT* vkrt, void* userData) {
    (void)vkrt;
    (void)userData;

    uint64_t shutdownStartTime = getMicroseconds();

    uint64_t vulkanShutdownStartTime = getMicroseconds();
    cImGui_ImplVulkan_Shutdown();
    double vulkanShutdownMs = (double)(getMicroseconds() - vulkanShutdownStartTime) / 1e3;

    uint64_t glfwShutdownStartTime = getMicroseconds();
    cImGui_ImplGlfw_Shutdown();
    double glfwShutdownMs = (double)(getMicroseconds() - glfwShutdownStartTime) / 1e3;

    LOG_TRACE("UI backends shut down. Vulkan: %.3f ms, GLFW: %.3f ms", vulkanShutdownMs, glfwShutdownMs);
    LOG_TRACE("Destroying UI context");

    ImGui_DestroyContext(NULL);
    gEditorUIScale = 0.0f;

    LOG_TRACE("UI shutdown complete in %.3f ms", (double)(getMicroseconds() - shutdownStartTime) / 1e3);
}

void editorUIDraw(VKRT* vkrt, VkCommandBuffer commandBuffer, void* userData) {
    Session* session = (Session*)userData;
    applyEditorUIScale(queryEditorContentScale(vkrt->runtime.window), true);

    cImGui_ImplGlfw_NewFrame();
    cImGui_ImplVulkan_NewFrame();
    ImGui_NewFrame();

    drawWorkspaceDockspace();
    bool viewportHovered = drawViewportWindow(vkrt);
    drawSceneInspectorWindow(vkrt, session);
    applyEditorCameraInput(vkrt, viewportHovered);

    ImGui_Render();
    cImGui_ImplVulkan_RenderDrawData(ImGui_GetDrawData(), commandBuffer);
}
