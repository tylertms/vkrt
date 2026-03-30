#include "editor.h"

#include "GLFW/glfw3.h"
#include "config.h"
#include "constants.h"
#include "dcimgui.h"
#include "dcimgui_impl_glfw.h"
#include "dcimgui_impl_vulkan.h"
#include "editor_internal.h"
#include "inspector/panel.h"
#include "vkrt.h"
#include "vkrt_overlay.h"
#include "vkrt_types.h"
#include "vulkan/vulkan_core.h"

#include <stdint.h>
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 5287)
#endif
#include "dcimgui_internal.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include "IBMPlexMono_Regular.h"
#include "IconsFontAwesome6.h"
#include "debug.h"
#include "fa_solid_900.h"
#include "numeric.h"
#include "session.h"
#include "theme.h"

#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static const float kEditorBaseTextSizePx = 15.5f;
static const float kEditorBaseIconSizePx = 11.0f;
static const float kEditorScaleEpsilon = 0.01f;
static const float kRenderViewWheelStep = 1.12f;
static const float kSceneDockFraction = 0.18f;
static const float kPropertiesDockFraction = 0.28f;
static const float kWorkspaceHostBorderOverlapPx = 1.0f;
static const float kMinimumViewportExtentPx = 1.0f;
static const uint32_t kViewportRectSnapTolerancePx = 1u;

typedef struct EditorUIState {
    float uiScale;
    GLFWwindow* window;
    bool frameReady;
    bool dockLayoutInitialized;
    bool glfwBackendInitialized;
    bool vulkanBackendInitialized;
    VKRT_OverlayInfo overlay;
} EditorUIState;

static EditorUIState* getEditorUIState(Session* session) {
    if (!session) return NULL;
    return session->editor.uiState;
}

static ImGuiDockNodeFlags composeDockNodeFlags(ImGuiDockNodeFlags baseFlags, ImGuiDockNodeFlags extraFlags) {
    return (ImGuiDockNodeFlags)((int)baseFlags | (int)extraFlags);
}

static ImGuiDockNodeFlags queryPanelDockFlags(void) {
    int flags = ImGuiDockNodeFlags_NoTabBar;
    flags |= ImGuiDockNodeFlags_NoWindowMenuButton;
    flags |= ImGuiDockNodeFlags_NoCloseButton;
    return (ImGuiDockNodeFlags)flags;
}

static ImGuiDockNodeFlags queryViewportDockFlags(void) {
    int flags = ImGuiDockNodeFlags_NoTabBar;
    flags |= ImGuiDockNodeFlags_NoUndocking;
    flags |= ImGuiDockNodeFlags_NoWindowMenuButton;
    flags |= ImGuiDockNodeFlags_NoCloseButton;
    return (ImGuiDockNodeFlags)flags;
}

static void initializeDockNodeFlags(ImGuiDockNode* dockNode, ImGuiDockNodeFlags extraFlags) {
    if (!dockNode) return;
    ImGuiDockNode_SetLocalFlags(dockNode, composeDockNodeFlags(dockNode->LocalFlags, extraFlags));
}

static uint32_t absDiffU32(uint32_t a, uint32_t b) {
    return (a > b) ? (a - b) : (b - a);
}

static ImFontConfig makeDefaultFontConfig(void) {
    ImFontConfig config = {0};
    config.FontDataOwnedByAtlas = true;
    config.OversampleH = 0;
    config.OversampleV = 0;
    config.ExtraSizeScale = 1.0f;
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

    int windowWidth = 0;
    int windowHeight = 0;
    glfwGetWindowSize(window, &windowWidth, &windowHeight);

    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);

    float framebufferScaleX = windowWidth > 0 ? ((float)framebufferWidth / (float)windowWidth) : 1.0f;
    float framebufferScaleY = windowHeight > 0 ? ((float)framebufferHeight / (float)windowHeight) : 1.0f;
    float scale = fmaxf(fmaxf(scaleX, scaleY), fmaxf(framebufferScaleX, framebufferScaleY));
    return vkrtFiniteClampf(scale, 1.0f, FLT_MIN, INFINITY);
}

static bool overlayInfoMatches(const VKRT_OverlayInfo* a, const VKRT_OverlayInfo* b) {
    if (!a || !b) return false;

    return (a->window == b->window && a->apiVersion == b->apiVersion && a->instance == b->instance &&
            a->physicalDevice == b->physicalDevice && a->device == b->device &&
            a->graphicsQueueFamily == b->graphicsQueueFamily && a->graphicsQueue == b->graphicsQueue &&
            a->descriptorPool == b->descriptorPool && a->colorAttachmentFormat == b->colorAttachmentFormat &&
            a->swapchainImageCount == b->swapchainImageCount &&
            a->swapchainMinImageCount == b->swapchainMinImageCount) != 0;
}

static VkPipelineRenderingCreateInfoKHR makeOverlayPipelineRenderingCreateInfo(const VkFormat* colorAttachmentFormat) {
    return (VkPipelineRenderingCreateInfoKHR){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = colorAttachmentFormat,
    };
}

static ImGui_ImplVulkan_PipelineInfo makeOverlayPipelineInfo(const VkFormat* colorAttachmentFormat) {
    return (ImGui_ImplVulkan_PipelineInfo){
        .MSAASamples = VK_SAMPLE_COUNT_1_BIT,
        .PipelineRenderingCreateInfo = makeOverlayPipelineRenderingCreateInfo(colorAttachmentFormat),
    };
}

static void populateVulkanInitInfo(const VKRT_OverlayInfo* overlay, ImGui_ImplVulkan_InitInfo* outInitInfo) {
    if (!overlay || !outInitInfo) return;

    *outInitInfo = (ImGui_ImplVulkan_InitInfo){
        .ApiVersion = overlay->apiVersion,
        .Instance = overlay->instance,
        .PhysicalDevice = overlay->physicalDevice,
        .Device = overlay->device,
        .QueueFamily = overlay->graphicsQueueFamily,
        .Queue = overlay->graphicsQueue,
        .PipelineCache = VK_NULL_HANDLE,
        .DescriptorPool = overlay->descriptorPool,
        .Allocator = VK_NULL_HANDLE,
        .MinImageCount = overlay->swapchainMinImageCount,
        .ImageCount = overlay->swapchainImageCount,
        .PipelineInfoMain = makeOverlayPipelineInfo(&overlay->colorAttachmentFormat),
        .PipelineInfoForViewports = makeOverlayPipelineInfo(&overlay->colorAttachmentFormat),
        .CheckVkResultFn = VK_NULL_HANDLE,
        .UseDynamicRendering = true,
    };
}

static bool queryEditorOverlayInfo(VKRT* vkrt, VKRT_OverlayInfo* outOverlay) {
    if (!vkrt || !outOverlay) return false;

    VKRT_Result result = VKRT_getOverlayInfo(vkrt, outOverlay);
    if (result != VKRT_SUCCESS) {
        LOG_ERROR("Querying overlay info failed (%d)", (int)result);
        return false;
    }

    return true;
}

static bool initializeEditorVulkanBackend(EditorUIState* state, const VKRT_OverlayInfo* overlay) {
    if (!state || !overlay) return false;

    state->overlay = *overlay;

    ImGui_ImplVulkan_InitInfo initInfo = {0};
    populateVulkanInitInfo(&state->overlay, &initInfo);
    if (!cImGui_ImplVulkan_Init(&initInfo)) {
        memset(&state->overlay, 0, sizeof(state->overlay));
        return false;
    }

    state->vulkanBackendInitialized = true;
    return true;
}

static bool initializeEditorGlfwBackend(EditorUIState* state) {
    if (!state || !state->window) return false;
    if (state->glfwBackendInitialized) return true;

    if (!cImGui_ImplGlfw_InitForVulkan(state->window, true)) {
        return false;
    }

    state->glfwBackendInitialized = true;
    return true;
}

static void shutdownEditorGlfwBackend(EditorUIState* state) {
    if (!state || !state->glfwBackendInitialized) return;

    cImGui_ImplGlfw_Shutdown();
    state->glfwBackendInitialized = false;
}

static void shutdownEditorVulkanBackend(EditorUIState* state) {
    if (!state || !state->vulkanBackendInitialized) return;

    cImGui_ImplVulkan_Shutdown();
    state->vulkanBackendInitialized = false;
    memset(&state->overlay, 0, sizeof(state->overlay));
}

static bool refreshEditorOverlayBackend(EditorUIState* state, VKRT* vkrt) {
    if (!state || !vkrt) return false;

    VKRT_OverlayInfo overlay = {0};
    if (!queryEditorOverlayInfo(vkrt, &overlay)) {
        return false;
    }

    state->window = overlay.window;

    if (!state->glfwBackendInitialized) {
        if (!initializeEditorGlfwBackend(state)) {
            LOG_ERROR("Initializing editor GLFW backend failed");
            return false;
        }
    }

    if (!state->vulkanBackendInitialized) {
        if (!initializeEditorVulkanBackend(state, &overlay)) {
            LOG_ERROR("Initializing editor Vulkan backend failed");
            return false;
        }
        return true;
    }

    if (overlayInfoMatches(&state->overlay, &overlay)) {
        return true;
    }

    shutdownEditorVulkanBackend(state);
    shutdownEditorGlfwBackend(state);
    if (!initializeEditorGlfwBackend(state)) {
        LOG_ERROR("Refreshing editor GLFW backend failed");
        return false;
    }
    if (!initializeEditorVulkanBackend(state, &overlay)) {
        LOG_ERROR("Refreshing editor Vulkan backend failed");
        return false;
    }

    return true;
}

static void rebuildEditorFonts(ImGuiIO* imguiIO, float uiScale) {
    if (!imguiIO) return;

    ImFontAtlas_Clear(imguiIO->Fonts);

    ImFontConfig textConfig = makeDefaultFontConfig();
    textConfig.FontDataOwnedByAtlas = false;
    ImFontAtlas_AddFontFromMemoryTTF(
        imguiIO->Fonts,
        (void*)IBMPlexMono_Regular,
        IBMPlexMono_Regular_len,
        kEditorBaseTextSizePx * uiScale,
        &textConfig,
        NULL
    );

    ImFontConfig iconConfig = makeDefaultFontConfig();
    iconConfig.FontDataOwnedByAtlas = false;
    iconConfig.MergeMode = true;
    iconConfig.PixelSnapH = true;
    static const ImWchar iconRanges[] = {ICON_MIN_FA, ICON_MAX_FA, 0};
    ImFontAtlas_AddFontFromMemoryTTF(
        imguiIO->Fonts,
        (void*)fa_solid_900,
        fa_solid_900_len,
        kEditorBaseIconSizePx * uiScale,
        &iconConfig,
        iconRanges
    );
}

static void applyEditorUIScale(EditorUIState* state, float uiScale) {
    if (!state) return;
    if (state->uiScale > 0.0f && fabsf(uiScale - state->uiScale) <= kEditorScaleEpsilon) return;

    ImGuiIO* imguiIO = ImGui_GetIO();
    ImGuiStyle* style = ImGui_GetStyle();
    if (state->uiScale <= 0.0f) {
        editorThemeApplyDefault();
        ImGuiStyle_ScaleAllSizes(style, uiScale);
    } else {
        ImGuiStyle_ScaleAllSizes(style, uiScale / state->uiScale);
    }

    rebuildEditorFonts(imguiIO, uiScale);
    state->uiScale = uiScale;
}

static float queryTopMenuBarHeight(void) {
    return ImGui_GetFrameHeight();
}

typedef enum EditorMenuAction {
    EDITOR_MENU_ACTION_OPEN_SCENE = 0,
    EDITOR_MENU_ACTION_RESET_SCENE,
    EDITOR_MENU_ACTION_SAVE_SCENE,
    EDITOR_MENU_ACTION_SAVE_SCENE_AS,
    EDITOR_MENU_ACTION_IMPORT_MESH,
    EDITOR_MENU_ACTION_LOAD_ENVIRONMENT,
    EDITOR_MENU_ACTION_CLEAR_ENVIRONMENT,
    EDITOR_MENU_ACTION_SAVE_RENDER,
    EDITOR_MENU_ACTION_RESET_ACCUMULATION,
} EditorMenuAction;

static void queueEditorMenuAction(Session* session, EditorMenuAction action, const char* currentScenePath) {
    if (!session) return;

    switch (action) {
        case EDITOR_MENU_ACTION_OPEN_SCENE:
            sessionRequestSceneOpenDialog(session);
            break;
        case EDITOR_MENU_ACTION_RESET_SCENE:
            if (currentScenePath && currentScenePath[0]) {
                sessionQueueSceneOpen(session, currentScenePath);
            }
            break;
        case EDITOR_MENU_ACTION_SAVE_SCENE:
            if (currentScenePath && currentScenePath[0]) {
                sessionQueueSceneSave(session, currentScenePath);
            }
            break;
        case EDITOR_MENU_ACTION_SAVE_SCENE_AS:
            sessionRequestSceneSaveDialog(session);
            break;
        case EDITOR_MENU_ACTION_IMPORT_MESH:
            sessionRequestMeshImportDialog(session);
            break;
        case EDITOR_MENU_ACTION_LOAD_ENVIRONMENT:
            sessionRequestEnvironmentImportDialog(session);
            break;
        case EDITOR_MENU_ACTION_CLEAR_ENVIRONMENT:
            sessionQueueEnvironmentClear(session);
            break;
        case EDITOR_MENU_ACTION_SAVE_RENDER:
            sessionRequestRenderSaveDialog(session);
            break;
        case EDITOR_MENU_ACTION_RESET_ACCUMULATION:
            sessionQueueRenderResetAccumulation(session);
            break;
        default:
            break;
    }
}

static void applyMainMenuShortcuts(
    Session* session,
    const VKRT_RenderStatusSnapshot* status,
    const char* currentScenePath,
    bool canClearEnvironment
) {
    if (!session) return;

    ImGuiIO* imguiIO = ImGui_GetIO();
    if (imguiIO->WantTextInput || ImGui_IsAnyItemActive() || ImGui_IsPopupOpen(NULL, ImGuiPopupFlags_AnyPopupId)) {
        return;
    }

    bool haveCurrentScenePath = (currentScenePath && currentScenePath[0]) != 0;
    bool canSaveRender = (status && VKRT_renderStatusIsComplete(status)) != 0;

    if (ImGui_IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O)) {
        queueEditorMenuAction(session, EDITOR_MENU_ACTION_OPEN_SCENE, currentScenePath);
    }
    if (haveCurrentScenePath && ImGui_IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) {
        queueEditorMenuAction(session, EDITOR_MENU_ACTION_SAVE_SCENE, currentScenePath);
    }
    if (ImGui_IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S)) {
        queueEditorMenuAction(session, EDITOR_MENU_ACTION_SAVE_SCENE_AS, currentScenePath);
    }
    if (ImGui_IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_I)) {
        queueEditorMenuAction(session, EDITOR_MENU_ACTION_IMPORT_MESH, currentScenePath);
    }
    if (ImGui_IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_E)) {
        queueEditorMenuAction(session, EDITOR_MENU_ACTION_LOAD_ENVIRONMENT, currentScenePath);
    }
    if (canClearEnvironment && ImGui_IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiKey_E)) {
        queueEditorMenuAction(session, EDITOR_MENU_ACTION_CLEAR_ENVIRONMENT, currentScenePath);
    }
    if (canSaveRender && ImGui_IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_R)) {
        queueEditorMenuAction(session, EDITOR_MENU_ACTION_SAVE_RENDER, currentScenePath);
    }
    if (status && ImGui_IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_R)) {
        queueEditorMenuAction(session, EDITOR_MENU_ACTION_RESET_ACCUMULATION, currentScenePath);
    }
}

static void drawFileMenu(
    Session* session,
    const VKRT_RenderStatusSnapshot* status,
    const char* currentScenePath,
    bool canClearEnvironment
) {
    bool haveCurrentScenePath = (currentScenePath && currentScenePath[0]) != 0;
    bool canSaveRender = (status && VKRT_renderStatusIsComplete(status)) != 0;

    if (!ImGui_BeginMenu("File")) return;

    if (ImGui_MenuItemEx("Open", "\tCtrl+O", false, true)) {
        queueEditorMenuAction(session, EDITOR_MENU_ACTION_OPEN_SCENE, currentScenePath);
    }
    if (ImGui_MenuItemEx("Reset Scene", NULL, false, haveCurrentScenePath)) {
        queueEditorMenuAction(session, EDITOR_MENU_ACTION_RESET_SCENE, currentScenePath);
    }
    if (ImGui_MenuItemEx("Save", "\tCtrl+S", false, haveCurrentScenePath)) {
        queueEditorMenuAction(session, EDITOR_MENU_ACTION_SAVE_SCENE, currentScenePath);
    }
    if (ImGui_MenuItemEx("Save As", "\tCtrl+Shift+S", false, true)) {
        queueEditorMenuAction(session, EDITOR_MENU_ACTION_SAVE_SCENE_AS, currentScenePath);
    }

    ImGui_Separator();
    if (ImGui_BeginMenu("Import")) {
        if (ImGui_MenuItemEx("Mesh", "\tCtrl+I", false, true)) {
            queueEditorMenuAction(session, EDITOR_MENU_ACTION_IMPORT_MESH, currentScenePath);
        }
        ImGui_EndMenu();
    }
    if (ImGui_BeginMenu("Environment")) {
        if (ImGui_MenuItemEx("Load", "\tCtrl+Shift+E", false, true)) {
            queueEditorMenuAction(session, EDITOR_MENU_ACTION_LOAD_ENVIRONMENT, currentScenePath);
        }
        if (ImGui_MenuItemEx("Clear", "\tCtrl+Alt+E", false, canClearEnvironment)) {
            queueEditorMenuAction(session, EDITOR_MENU_ACTION_CLEAR_ENVIRONMENT, currentScenePath);
        }
        ImGui_EndMenu();
    }
    if (ImGui_MenuItemEx("Save Render", "\tCtrl+Shift+R", false, canSaveRender)) {
        queueEditorMenuAction(session, EDITOR_MENU_ACTION_SAVE_RENDER, currentScenePath);
    }
    ImGui_EndMenu();
}

static void drawViewMenu(Session* session, const VKRT_RenderStatusSnapshot* status, const char* currentScenePath) {
    if (!ImGui_BeginMenu("View")) return;

    if (ImGui_MenuItemEx("Reset Accumulation", "\tCtrl+R", false, status != NULL)) {
        queueEditorMenuAction(session, EDITOR_MENU_ACTION_RESET_ACCUMULATION, currentScenePath);
    }
    ImGui_Separator();
    ImGui_MenuItemEx("Panels", NULL, false, false);
    ImGui_EndMenu();
}

static void drawHelpMenu(void) {
    if (!ImGui_BeginMenu("Help")) return;
    ImGui_MenuItemEx("Controls", NULL, false, false);
    ImGui_MenuItemEx("About", NULL, false, false);
    ImGui_EndMenu();
}

static void drawMainMenuBar(
    Session* session,
    const VKRT_RenderStatusSnapshot* status,
    const char* currentScenePath,
    bool canClearEnvironment
) {
    if (!session) return;
    if (!ImGui_BeginMainMenuBar()) return;

    drawFileMenu(session, status, currentScenePath, canClearEnvironment);
    drawViewMenu(session, status, currentScenePath);
    drawHelpMenu();
    ImGui_EndMainMenuBar();
}

static int updateRenderViewState(VKRT* vkrt, float zoom, float panX, float panY, const char* failureLabel) {
    VKRT_Result result = VKRT_setRenderViewState(vkrt, zoom, panX, panY);
    if (result == VKRT_SUCCESS) return 1;

    LOG_ERROR("%s (%d)", failureLabel, (int)result);
    return 0;
}

static void applyInteractiveCameraInput(VKRT* vkrt, bool viewportHovered, ImGuiIO* imguiIO) {
    VKRT_CameraInput cameraInput = {
        .orbitDx = imguiIO->MouseDelta.x,
        .orbitDy = imguiIO->MouseDelta.y,
        .panDx = imguiIO->MouseDelta.x,
        .panDy = imguiIO->MouseDelta.y,
        .scroll = imguiIO->MouseWheel,
        .orbiting = viewportHovered && !imguiIO->KeyShift && ImGui_IsMouseDragging(ImGuiMouseButton_Middle, -1.0f),
        .panning = viewportHovered && ((imguiIO->KeyShift && ImGui_IsMouseDragging(ImGuiMouseButton_Middle, -1.0f)) ||
                                       ImGui_IsMouseDragging(ImGuiMouseButton_Right, -1.0f)),
        .captureMouse = !viewportHovered,
    };
    VKRT_Result result = VKRT_applyCameraInput(vkrt, &cameraInput);
    if (result != VKRT_SUCCESS) {
        LOG_ERROR("Applying camera input failed (%d)", (int)result);
    }
}

static int applyRenderViewCameraInput(
    VKRT* vkrt,
    const VKRT_RuntimeSnapshot* runtime,
    bool viewportHovered,
    ImGuiIO* imguiIO
) {
    float zoom = 1.0f;
    float panX = 0.0f;
    float panY = 0.0f;
    VKRT_Result result = VKRT_getRenderViewState(vkrt, &zoom, &panX, &panY);
    if (result != VKRT_SUCCESS) {
        LOG_ERROR("Querying render view state failed (%d)", (int)result);
        return 0;
    }
    if (!viewportHovered) return 1;

    if (imguiIO->MouseWheel != 0.0f) {
        zoom = zoom * powf(kRenderViewWheelStep, imguiIO->MouseWheel);
        zoom = vkrtClampf(zoom, VKRT_RENDER_VIEW_ZOOM_MIN, VKRT_RENDER_VIEW_ZOOM_MAX);
        if (!updateRenderViewState(vkrt, zoom, panX, panY, "Updating render zoom failed")) return 0;
        result = VKRT_getRenderViewState(vkrt, &zoom, &panX, &panY);
        if (result != VKRT_SUCCESS) {
            LOG_ERROR("Querying render view state failed (%d)", (int)result);
            return 0;
        }
    }

    if ((ImGui_IsMouseDragging(ImGuiMouseButton_Middle, -1.0f) ||
         ImGui_IsMouseDragging(ImGuiMouseButton_Right, -1.0f)) == 0) {
        return 1;
    }

    float scaleX = imguiIO->DisplayFramebufferScale.x > 0.0f ? imguiIO->DisplayFramebufferScale.x : 1.0f;
    float scaleY = imguiIO->DisplayFramebufferScale.y > 0.0f ? imguiIO->DisplayFramebufferScale.y : 1.0f;
    float viewWidth = (float)(runtime->displayViewportRect[2] > 0 ? runtime->displayViewportRect[2] : 1u) / scaleX;
    float viewHeight = (float)(runtime->displayViewportRect[3] > 0 ? runtime->displayViewportRect[3] : 1u) / scaleY;
    float cropWidth = 1.0f;
    float cropHeight = 1.0f;

    result = VKRT_getRenderViewCrop(vkrt, zoom, &cropWidth, &cropHeight);
    if (result != VKRT_SUCCESS) {
        LOG_ERROR("Querying render view crop failed (%d)", (int)result);
        return 0;
    }

    panX -= (imguiIO->MouseDelta.x * cropWidth) / viewWidth;
    panY -= (imguiIO->MouseDelta.y * cropHeight) / viewHeight;
    return updateRenderViewState(vkrt, zoom, panX, panY, "Updating render pan failed");
}

static void initializeDockLayout(EditorUIState* state) {
    if (!state || state->dockLayoutInitialized) return;

    const ImGuiViewport* viewport = ImGui_GetMainViewport();
    ImVec2 dockspaceSize = viewport->Size;
    dockspaceSize.y -= queryTopMenuBarHeight();
    if (dockspaceSize.y < kMinimumViewportExtentPx) dockspaceSize.y = kMinimumViewportExtentPx;
    ImGuiID dockspaceID = ImGui_GetID("WorkspaceDockspace");
    ImGuiDockNode* existingDockNode = ImGui_DockBuilderGetNode(dockspaceID);
    if (existingDockNode &&
        (existingDockNode->ChildNodes[0] || existingDockNode->ChildNodes[1] || existingDockNode->Windows.Size > 0)) {
        state->dockLayoutInitialized = true;
        return;
    }

    ImGui_DockBuilderRemoveNode(dockspaceID);
    ImGui_DockBuilderAddNodeEx(dockspaceID, ImGuiDockNodeFlags_DockSpace);
    ImGui_DockBuilderSetNodeSize(dockspaceID, dockspaceSize);

    ImGuiID sceneDockID = 0;
    ImGuiID propertiesDockID = 0;
    ImGuiID centerWorkspaceDockID = 0;
    ImGuiID viewportDockID = 0;

    ImGui_DockBuilderSplitNode(dockspaceID, ImGuiDir_Left, kSceneDockFraction, &sceneDockID, &centerWorkspaceDockID);
    ImGui_DockBuilderSplitNode(
        centerWorkspaceDockID,
        ImGuiDir_Right,
        kPropertiesDockFraction,
        &propertiesDockID,
        &viewportDockID
    );

    ImGui_DockBuilderDockWindow("Scene Browser", sceneDockID);
    ImGui_DockBuilderDockWindow("Properties", propertiesDockID);
    ImGui_DockBuilderDockWindow("Viewport###ViewWindow", viewportDockID);

    ImGuiDockNode* sceneDockNode = ImGui_DockBuilderGetNode(sceneDockID);
    ImGuiDockNode* propertiesDockNode = ImGui_DockBuilderGetNode(propertiesDockID);
    ImGuiDockNode* viewportDockNode = ImGui_DockBuilderGetNode(viewportDockID);
    initializeDockNodeFlags(sceneDockNode, queryPanelDockFlags());
    initializeDockNodeFlags(propertiesDockNode, queryPanelDockFlags());
    initializeDockNodeFlags(viewportDockNode, queryViewportDockFlags());

    ImGui_DockBuilderFinish(dockspaceID);
    state->dockLayoutInitialized = true;
}

static bool drawWorkspaceDockspace(EditorUIState* state) {
    const ImGuiViewport* mainViewport = ImGui_GetMainViewport();
    ImVec2 dockspacePos = mainViewport->Pos;
    dockspacePos.y += queryTopMenuBarHeight() - kWorkspaceHostBorderOverlapPx;
    ImVec2 dockspaceSize = mainViewport->Size;
    dockspaceSize.y -= queryTopMenuBarHeight() - kWorkspaceHostBorderOverlapPx;
    if (dockspaceSize.y < kMinimumViewportExtentPx) dockspaceSize.y = kMinimumViewportExtentPx;

    ImGui_SetNextWindowPos(dockspacePos, ImGuiCond_Always);
    ImGui_SetNextWindowSize(dockspaceSize, ImGuiCond_Always);
    ImGui_SetNextWindowViewport(mainViewport->ID);

    ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                 ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;

    ImGui_PushStyleVarImVec2(ImGuiStyleVar_WindowPadding, (ImVec2){0.0f, 0.0f});
    ImGui_PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui_Begin("Workspace", NULL, hostFlags);
    ImGui_PopStyleVarEx(2);

    ImGuiID dockspaceID = ImGui_GetID("WorkspaceDockspace");
    ImGui_DockSpaceEx(dockspaceID, (ImVec2){0.0f, 0.0f}, ImGuiDockNodeFlags_None, NULL);
    initializeDockLayout(state);

    ImGui_End();
    return true;
}

static bool drawViewportWindow(VKRT* vkrt, const VKRT_RenderStatusSnapshot* status, VKRT_RuntimeSnapshot* runtime) {
    if (!vkrt || !status || !runtime) return false;

    ImGuiWindowClass viewportWindowClass = {0};
    viewportWindowClass.DockNodeFlagsOverrideSet =
        composeDockNodeFlags(ImGuiDockNodeFlags_None, queryViewportDockFlags());
    ImGui_SetNextWindowClass(&viewportWindowClass);

    ImGui_PushStyleVarImVec2(ImGuiStyleVar_WindowPadding, (ImVec2){0.0f, 0.0f});
    const char* viewportWindowLabel =
        VKRT_renderStatusIsActive(status) ? "Render###ViewWindow" : "Viewport###ViewWindow";
    ImGui_Begin(
        viewportWindowLabel,
        NULL,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground
    );

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
    uint32_t viewportWidth = width > kMinimumViewportExtentPx ? (uint32_t)lroundf(width) : 1;
    uint32_t viewportHeight = height > kMinimumViewportExtentPx ? (uint32_t)lroundf(height) : 1;

    const uint32_t* prevViewport = runtime->displayViewportRect;
    if (absDiffU32(viewportX, prevViewport[0]) <= kViewportRectSnapTolerancePx &&
        absDiffU32(viewportY, prevViewport[1]) <= kViewportRectSnapTolerancePx &&
        absDiffU32(viewportWidth, prevViewport[2]) <= kViewportRectSnapTolerancePx &&
        absDiffU32(viewportHeight, prevViewport[3]) <= kViewportRectSnapTolerancePx) {
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

    runtime->displayViewportRect[0] = viewportX;
    runtime->displayViewportRect[1] = viewportY;
    runtime->displayViewportRect[2] = viewportWidth;
    runtime->displayViewportRect[3] = viewportHeight;

    ImGui_End();
    ImGui_PopStyleVar();

    return viewportHovered;
}

static void applyCompletedViewportSelection(VKRT* vkrt, Session* session) {
    if (!vkrt || !session) return;

    uint32_t meshIndex = VKRT_INVALID_INDEX;
    uint8_t ready = 0;
    VKRT_Result result = VKRT_consumeSelectedMesh(vkrt, &meshIndex, &ready);
    if (result != VKRT_SUCCESS) {
        LOG_ERROR("Consuming viewport selection failed (%d)", (int)result);
        return;
    }
    if (!ready) return;

    result = VKRT_setSelectedMesh(vkrt, meshIndex);
    if (result != VKRT_SUCCESS) {
        LOG_ERROR("Applying viewport selection failed (%d)", (int)result);
        return;
    }
    sessionSelectSceneObjectForMesh(session, meshIndex);
}

static bool queryViewportMousePixel(uint32_t* outX, uint32_t* outY) {
    if (!outX || !outY) return false;

    ImGuiIO* imguiIO = ImGui_GetIO();
    float scaleX = imguiIO->DisplayFramebufferScale.x > 0.0f ? imguiIO->DisplayFramebufferScale.x : 1.0f;
    float scaleY = imguiIO->DisplayFramebufferScale.y > 0.0f ? imguiIO->DisplayFramebufferScale.y : 1.0f;
    float mouseX = imguiIO->MousePos.x * scaleX;
    float mouseY = imguiIO->MousePos.y * scaleY;
    if (!isfinite(mouseX) || !isfinite(mouseY) || mouseX < 0.0f || mouseY < 0.0f) return false;

    *outX = (uint32_t)mouseX;
    *outY = (uint32_t)mouseY;
    return true;
}

static bool queryViewportClickPixel(const VKRT_RuntimeSnapshot* runtime, uint32_t* outX, uint32_t* outY) {
    if (!runtime || !outX || !outY) return false;
    uint32_t mouseX = 0;
    uint32_t mouseY = 0;
    if (!queryViewportMousePixel(&mouseX, &mouseY)) return false;

    uint32_t viewportX = runtime->displayViewportRect[0];
    uint32_t viewportY = runtime->displayViewportRect[1];
    uint32_t viewportWidth = runtime->displayViewportRect[2];
    uint32_t viewportHeight = runtime->displayViewportRect[3];
    if (viewportWidth == 0 || viewportHeight == 0) return false;
    if (mouseX < viewportX || mouseY < viewportY || mouseX >= viewportX + viewportWidth ||
        mouseY >= viewportY + viewportHeight) {
        return false;
    }

    *outX = mouseX;
    *outY = mouseY;
    return true;
}

static void requestViewportSelection(
    VKRT* vkrt,
    const VKRT_RenderStatusSnapshot* status,
    const VKRT_RuntimeSnapshot* runtime,
    bool viewportHovered
) {
    if (!vkrt || !status || !runtime || !viewportHovered) return;
    if (VKRT_renderStatusIsActive(status)) return;

    if (!ImGui_IsMouseClicked(ImGuiMouseButton_Left)) return;

    uint32_t selectionX = 0;
    uint32_t selectionY = 0;
    if (!queryViewportClickPixel(runtime, &selectionX, &selectionY)) return;

    VKRT_Result result = VKRT_requestSelectionAtPixel(vkrt, selectionX, selectionY);
    if (result != VKRT_SUCCESS) {
        LOG_ERROR("Requesting viewport selection failed (%d)", (int)result);
    }
}

static void clearViewportSelectionOnEscape(VKRT* vkrt, Session* session) {
    if (!vkrt || !session) return;
    if (!ImGui_IsKeyPressed(ImGuiKey_Escape)) return;

    VKRT_Result result = VKRT_setSelectedMesh(vkrt, VKRT_INVALID_INDEX);
    if (result != VKRT_SUCCESS) {
        LOG_ERROR("Clearing viewport selection failed (%d)", (int)result);
    }
    sessionSetSelectedSceneObject(session, VKRT_INVALID_INDEX);
}

static void queueSelectedSceneObjectRemovalOnDelete(
    VKRT* vkrt,
    Session* session,
    const VKRT_RenderStatusSnapshot* status
) {
    if (!vkrt || !session || !status) return;
    if (VKRT_renderStatusIsActive(status)) return;

    ImGuiIO* imguiIO = ImGui_GetIO();
    if (imguiIO->WantTextInput || ImGui_IsAnyItemActive() || ImGui_IsPopupOpen(NULL, ImGuiPopupFlags_AnyPopupId)) {
        return;
    }
    if (!ImGui_IsKeyPressed(ImGuiKey_Delete) && !ImGui_IsKeyPressed(ImGuiKey_Backspace)) return;

    uint32_t selectedObjectIndex = sessionGetSelectedSceneObject(session);
    if (selectedObjectIndex == VKRT_INVALID_INDEX) {
        uint32_t selectedMeshIndex = VKRT_INVALID_INDEX;
        VKRT_Result result = VKRT_getSelectedMesh(vkrt, &selectedMeshIndex);
        if (result != VKRT_SUCCESS) {
            LOG_ERROR("Querying selected mesh failed (%d)", (int)result);
            return;
        }
        if (selectedMeshIndex != VKRT_INVALID_INDEX) {
            sessionSelectSceneObjectForMesh(session, selectedMeshIndex);
            selectedObjectIndex = sessionGetSelectedSceneObject(session);
        }
    }
    if (selectedObjectIndex == VKRT_INVALID_INDEX) return;

    sessionQueueSceneObjectRemoval(session, selectedObjectIndex);
}

static void applyEditorCameraInput(
    VKRT* vkrt,
    const VKRT_RenderStatusSnapshot* status,
    const VKRT_RuntimeSnapshot* runtime,
    bool viewportHovered
) {
    if (!vkrt || !status || !runtime) return;

    ImGuiIO* imguiIO = ImGui_GetIO();
    if (VKRT_renderStatusIsActive(status)) {
        (void)applyRenderViewCameraInput(vkrt, runtime, viewportHovered, imguiIO);
        return;
    }
    applyInteractiveCameraInput(vkrt, viewportHovered, imguiIO);
}

void editorRegisterAppHooks(VKRT* vkrt, Session* session) {
    if (!vkrt || !session) return;

    VKRT_AppHooks hooks = {
        .init = editorUIInitialize,
        .deinit = editorUIShutdown,
        .drawOverlay = editorUIDraw,
        .userData = session,
    };
    VKRT_registerAppHooks(vkrt, hooks);
}

void editorUIInitialize(VKRT* vkrt, void* userData) {
    Session* session = (Session*)userData;
    if (!session || session->editor.uiState) return;

    EditorUIState* state = (EditorUIState*)calloc(1, sizeof(*state));
    if (!state) {
        LOG_ERROR("Failed to allocate editor UI state");
        return;
    }
    session->editor.uiState = state;

    ImGui_CreateContext(NULL);
    state->frameReady = false;

    ImGuiIO* imguiIO = ImGui_GetIO();
    imguiIO->ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    VKRT_OverlayInfo overlay = {0};
    if (!queryEditorOverlayInfo(vkrt, &overlay)) {
        ImGui_DestroyContext(NULL);
        free(state);
        session->editor.uiState = NULL;
        return;
    }

    state->window = overlay.window;
    editorUIInitializeDialogs(session, state->window);
    float uiScale = queryEditorContentScale(state->window);
    applyEditorUIScale(state, uiScale);

    if (!initializeEditorGlfwBackend(state)) {
        ImGui_DestroyContext(NULL);
        editorUIShutdownDialogs(session);
        free(state);
        session->editor.uiState = NULL;
        return;
    }

    if (!initializeEditorVulkanBackend(state, &overlay)) {
        shutdownEditorGlfwBackend(state);
        ImGui_DestroyContext(NULL);
        editorUIShutdownDialogs(session);
        free(state);
        session->editor.uiState = NULL;
        return;
    }
}

void editorUIShutdown(VKRT* vkrt, void* userData) {
    (void)vkrt;
    Session* session = (Session*)userData;
    EditorUIState* state = getEditorUIState(session);
    if (!state) return;

    shutdownEditorVulkanBackend(state);
    shutdownEditorGlfwBackend(state);
    LOG_TRACE("Destroying UI context");

    ImGui_DestroyContext(NULL);
    editorUIShutdownDialogs(session);

    LOG_TRACE("UI shutdown complete");

    free(state);
    session->editor.uiState = NULL;
}

void editorUIDraw(VKRT* vkrt, VkCommandBuffer commandBuffer, void* userData) {
    (void)vkrt;
    Session* session = (Session*)userData;
    const EditorUIState* state = getEditorUIState(session);

    if (!ImGui_GetCurrentContext()) return;
    if (!state || !state->frameReady || !state->glfwBackendInitialized || !state->vulkanBackendInitialized) return;
    cImGui_ImplVulkan_RenderDrawData(ImGui_GetDrawData(), commandBuffer);
}

void editorUIUpdate(VKRT* vkrt, Session* session) {
    if (!vkrt || !session) return;
    if (!ImGui_GetCurrentContext()) return;
    EditorUIState* state = getEditorUIState(session);
    if (!state) return;
    state->frameReady = false;

    if (!refreshEditorOverlayBackend(state, vkrt) || !state->window || !state->glfwBackendInitialized) return;

    applyEditorUIScale(state, queryEditorContentScale(state->window));

    cImGui_ImplGlfw_NewFrame();
    cImGui_ImplVulkan_NewFrame();
    ImGui_NewFrame();
    applyCompletedViewportSelection(vkrt, session);
    clearViewportSelectionOnEscape(vkrt, session);

    VKRT_RenderStatusSnapshot status = {0};
    bool hasStatus = VKRT_getRenderStatus(vkrt, &status) == VKRT_SUCCESS;
    VKRT_SceneSettingsSnapshot sceneSettings = {0};
    bool haveSceneSettings = VKRT_getSceneSettings(vkrt, &sceneSettings) == VKRT_SUCCESS;
    bool canClearEnvironment = (haveSceneSettings && sceneSettings.environmentTextureIndex != VKRT_INVALID_INDEX) != 0;
    const char* currentScenePath = sessionGetCurrentScenePath(session);
    VKRT_RuntimeSnapshot runtime = {0};
    bool hasRuntime = VKRT_getRuntimeSnapshot(vkrt, &runtime) == VKRT_SUCCESS;
    queueSelectedSceneObjectRemovalOnDelete(vkrt, session, (int)hasStatus ? &status : NULL);
    applyMainMenuShortcuts(session, (int)hasStatus ? &status : NULL, currentScenePath, canClearEnvironment);

    drawMainMenuBar(session, (int)hasStatus ? &status : NULL, currentScenePath, canClearEnvironment);
    drawWorkspaceDockspace(state);

    bool viewportHovered = false;
    if (hasStatus && hasRuntime) {
        viewportHovered = drawViewportWindow(vkrt, &status, &runtime);
        inspectorDrawWorkspacePanels(vkrt, session);
        requestViewportSelection(vkrt, &status, &runtime, viewportHovered);
        applyEditorCameraInput(vkrt, &status, &runtime, viewportHovered);
    }

    ImGui_Render();
    state->frameReady = true;
}
