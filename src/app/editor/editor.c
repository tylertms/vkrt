#include "editor.h"
#include "editor_internal.h"

#include "dcimgui.h"
#include "dcimgui_impl_glfw.h"
#include "dcimgui_impl_vulkan.h"
#include "dcimgui_internal.h"
#include "theme.h"
#include "debug.h"
#include "nfd.h"
#include "IBMPlexMono_Regular.h"
#include "fa_solid_900.h"
#include "IconsFontAwesome6.h"

#include <float.h>
#include <math.h>
#include <stdbool.h>

static const float kEditorBaseTextSizePx = 15.5f;
static const float kEditorBaseIconSizePx = 11.0f;
static const float kEditorScaleEpsilon = 0.01f;
static const float kRenderViewWheelStep = 1.12f;

static float gEditorUIScale = 0.0f;
static GLFWwindow* gEditorWindow = NULL;

static uint32_t absDiffU32(uint32_t a, uint32_t b) {
    return (a > b) ? (a - b) : (b - a);
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
    if (!window) return 1.0f;

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

    const ImGuiViewport* viewport = ImGui_GetMainViewport();
    ImGuiID dockspaceID = ImGui_GetID("WorkspaceDockspace");
    ImGuiDockNode* existingDockNode = ImGui_DockBuilderGetNode(dockspaceID);
    if (existingDockNode &&
        (existingDockNode->ChildNodes[0] || existingDockNode->ChildNodes[1] || existingDockNode->Windows.Size > 0)) {
        isInitialized = true;
        return;
    }

    ImGui_DockBuilderRemoveNode(dockspaceID);
    ImGui_DockBuilderAddNodeEx(dockspaceID, ImGuiDockNodeFlags_DockSpace);
    ImGui_DockBuilderSetNodeSize(dockspaceID, viewport->Size);

    ImGuiID inspectorDockID = 0;
    ImGuiID viewportDockID = dockspaceID;
    ImGui_DockBuilderSplitNode(dockspaceID, ImGuiDir_Left, 0.26f, &inspectorDockID, &viewportDockID);
    ImGui_DockBuilderDockWindow("Scene Inspector", inspectorDockID);
    ImGui_DockBuilderDockWindow("Viewport###ViewWindow", viewportDockID);

    ImGuiDockNode* inspectorDockNode = ImGui_DockBuilderGetNode(inspectorDockID);
    if (inspectorDockNode) {
        ImGuiDockNodeFlags localFlags = inspectorDockNode->LocalFlags |
            ImGuiDockNodeFlags_NoTabBar |
            ImGuiDockNodeFlags_NoWindowMenuButton |
            ImGuiDockNodeFlags_NoCloseButton;
        ImGuiDockNode_SetLocalFlags(inspectorDockNode, localFlags);
    }
    ImGuiDockNode* viewportDockNode = ImGui_DockBuilderGetNode(viewportDockID);
    if (viewportDockNode) {
        ImGuiDockNodeFlags localFlags = viewportDockNode->LocalFlags |
            ImGuiDockNodeFlags_NoTabBar |
            ImGuiDockNodeFlags_NoUndocking |
            ImGuiDockNodeFlags_NoWindowMenuButton |
            ImGuiDockNodeFlags_NoCloseButton;
        ImGuiDockNode_SetLocalFlags(viewportDockNode, localFlags);
    }

    ImGui_DockBuilderFinish(dockspaceID);
    isInitialized = true;
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
    VKRT_PublicState state = {0};
    VKRT_RuntimeSnapshot runtime = {0};
    if (VKRT_getPublicState(vkrt, &state) != VKRT_SUCCESS ||
        VKRT_getRuntimeSnapshot(vkrt, &runtime) != VKRT_SUCCESS) {
        return false;
    }

    ImGuiWindowClass viewportWindowClass = {0};
    viewportWindowClass.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoTabBar |
                                                   ImGuiDockNodeFlags_NoUndocking |
                                                   ImGuiDockNodeFlags_NoWindowMenuButton |
                                                   ImGuiDockNodeFlags_NoCloseButton;
    ImGui_SetNextWindowClass(&viewportWindowClass);

    ImGui_PushStyleVarImVec2(ImGuiStyleVar_WindowPadding, (ImVec2){0.0f, 0.0f});
    const char* viewportWindowLabel = state.renderModeActive
        ? "Render###ViewWindow"
        : "Viewport###ViewWindow";
    ImGui_Begin(viewportWindowLabel, NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                                             ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);

    bool viewportHovered = ImGui_IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);

    ImVec2 contentPosition = ImGui_GetCursorScreenPos();
    ImVec2 contentSize = ImGui_GetContentRegionAvail();
    ImVec2 framebufferScale = ImGui_GetIO()->DisplayFramebufferScale;

    float x = contentPosition.x * framebufferScale.x;
    float y = contentPosition.y * framebufferScale.y;
    float width = contentSize.x * framebufferScale.x;
    float height = contentSize.y * framebufferScale.y;

    uint32_t viewportX = x > 0.0f ? (uint32_t)lroundf(x) : 0;
    uint32_t viewportY = y > 0.0f ? (uint32_t)lroundf(y) : 0;
    uint32_t viewportWidth = width > 1.0f ? (uint32_t)lroundf(width) : 1;
    uint32_t viewportHeight = height > 1.0f ? (uint32_t)lroundf(height) : 1;

    const uint32_t* prevViewport = runtime.displayViewportRect;
    if (absDiffU32(viewportX, prevViewport[0]) <= 1 &&
        absDiffU32(viewportY, prevViewport[1]) <= 1 &&
        absDiffU32(viewportWidth, prevViewport[2]) <= 1 &&
        absDiffU32(viewportHeight, prevViewport[3]) <= 1) {
        viewportX = prevViewport[0];
        viewportY = prevViewport[1];
        viewportWidth = prevViewport[2];
        viewportHeight = prevViewport[3];
    }

    VKRT_Result viewportResult = VKRT_setRenderViewport(vkrt, viewportX, viewportY, viewportWidth, viewportHeight);
    if (viewportResult != VKRT_SUCCESS) {
        LOG_ERROR("Updating render viewport failed (%d)", (int)viewportResult);
        ImGui_End();
        ImGui_PopStyleVar();
        return false;
    }

    ImGui_End();
    ImGui_PopStyleVar();

    return viewportHovered;
}

static float clampRenderViewValue(float value, float minValue, float maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

static void applyEditorCameraInput(VKRT* vkrt, bool viewportHovered) {
    VKRT_PublicState state = {0};
    if (VKRT_getPublicState(vkrt, &state) != VKRT_SUCCESS) return;

    ImGuiIO* io = ImGui_GetIO();
    if (state.renderModeActive) {
        if (!viewportHovered) return;

        float zoom = state.renderViewZoom;
        float panX = state.renderViewPanX;
        float panY = state.renderViewPanY;

        if (io->MouseWheel != 0.0f) {
            zoom = zoom * powf(kRenderViewWheelStep, io->MouseWheel);
            zoom = clampRenderViewValue(zoom, VKRT_RENDER_VIEW_ZOOM_MIN, VKRT_RENDER_VIEW_ZOOM_MAX);
            VKRT_Result result = VKRT_setRenderViewState(vkrt, zoom, panX, panY);
            if (result != VKRT_SUCCESS) {
                LOG_ERROR("Updating render zoom failed (%d)", (int)result);
                return;
            }
            result = VKRT_getRenderViewState(vkrt, &zoom, &panX, &panY);
            if (result != VKRT_SUCCESS) {
                LOG_ERROR("Querying render view state failed (%d)", (int)result);
                return;
            }
        }

        bool panning = ImGui_IsMouseDragging(ImGuiMouseButton_Left, -1.0f) ||
                       ImGui_IsMouseDragging(ImGuiMouseButton_Middle, -1.0f) ||
                       ImGui_IsMouseDragging(ImGuiMouseButton_Right, -1.0f);
        if (panning) {
            VKRT_RuntimeSnapshot runtime = {0};
            if (VKRT_getRuntimeSnapshot(vkrt, &runtime) != VKRT_SUCCESS) return;

            float scaleX = io->DisplayFramebufferScale.x > 0.0f ? io->DisplayFramebufferScale.x : 1.0f;
            float scaleY = io->DisplayFramebufferScale.y > 0.0f ? io->DisplayFramebufferScale.y : 1.0f;
            float viewWidth = (float)(runtime.displayViewportRect[2] > 0 ? runtime.displayViewportRect[2] : 1u) / scaleX;
            float viewHeight = (float)(runtime.displayViewportRect[3] > 0 ? runtime.displayViewportRect[3] : 1u) / scaleY;
            float cropWidth = 1.0f;
            float cropHeight = 1.0f;

            if (VKRT_getRenderViewCrop(vkrt, zoom, &cropWidth, &cropHeight) != VKRT_SUCCESS) return;

            panX -= (io->MouseDelta.x * cropWidth) / viewWidth;
            panY -= (io->MouseDelta.y * cropHeight) / viewHeight;
            VKRT_Result result = VKRT_setRenderViewState(vkrt, zoom, panX, panY);
            if (result != VKRT_SUCCESS) {
                LOG_ERROR("Updating render pan failed (%d)", (int)result);
                return;
            }
        }
        return;
    }

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
    VKRT_Result result = VKRT_applyCameraInput(vkrt, &cameraInput);
    if (result != VKRT_SUCCESS) {
        LOG_ERROR("Applying camera input failed (%d)", (int)result);
    }
}

void editorUIInitialize(VKRT* vkrt, void* userData) {
    (void)userData;

    NFD_Init();
    ImGui_CreateContext(NULL);

    ImGuiIO* io = ImGui_GetIO();
    io->ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    VKRT_OverlayInfo overlay = {0};
    if (VKRT_getOverlayInfo(vkrt, &overlay) != VKRT_SUCCESS) {
        return;
    }

    gEditorWindow = overlay.window;
    float uiScale = queryEditorContentScale(gEditorWindow);
    applyEditorUIScale(uiScale, false);

    cImGui_ImplGlfw_InitForVulkan(overlay.window, true);

    ImGui_ImplVulkan_InitInfo initInfo = {
        .Instance = overlay.instance,
        .PhysicalDevice = overlay.physicalDevice,
        .Device = overlay.device,
        .QueueFamily = overlay.graphicsQueueFamily,
        .Queue = overlay.graphicsQueue,
        .PipelineCache = VK_NULL_HANDLE,
        .DescriptorPool = overlay.descriptorPool,
        .Allocator = VK_NULL_HANDLE,
        .MinImageCount = overlay.swapchainMinImageCount,
        .ImageCount = overlay.swapchainImageCount,
        .CheckVkResultFn = VK_NULL_HANDLE,
        .RenderPass = overlay.renderPass,
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
    NFD_Quit();
    gEditorUIScale = 0.0f;
    gEditorWindow = NULL;

    LOG_TRACE("UI shutdown complete in %.3f ms", (double)(getMicroseconds() - shutdownStartTime) / 1e3);
}

void editorUIDraw(VKRT* vkrt, VkCommandBuffer commandBuffer, void* userData) {
    Session* session = (Session*)userData;

    if (!gEditorWindow) {
        VKRT_OverlayInfo overlay = {0};
        if (VKRT_getOverlayInfo(vkrt, &overlay) == VKRT_SUCCESS) {
            gEditorWindow = overlay.window;
        }
    }

    applyEditorUIScale(queryEditorContentScale(gEditorWindow), true);

    cImGui_ImplGlfw_NewFrame();
    cImGui_ImplVulkan_NewFrame();
    ImGui_NewFrame();

    drawWorkspaceDockspace();
    bool viewportHovered = drawViewportWindow(vkrt);
    editorUIDrawSceneInspector(vkrt, session);
    applyEditorCameraInput(vkrt, viewportHovered);

    ImGui_Render();
    cImGui_ImplVulkan_RenderDrawData(ImGui_GetDrawData(), commandBuffer);
}
