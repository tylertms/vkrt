#include "editor.h"
#include "editor_internal.h"
#include "inspector/panel.h"

#include "dcimgui.h"
#include "dcimgui_impl_glfw.h"
#include "dcimgui_impl_vulkan.h"
#include "dcimgui_internal.h"
#include "theme.h"
#include "debug.h"
#include "numeric.h"
#include "IBMPlexMono_Regular.h"
#include "fa_solid_900.h"
#include "IconsFontAwesome6.h"

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
    return vkrtFiniteClampf(scale, 1.0f, FLT_MIN, INFINITY);
}

static bool overlayInfoMatches(const VKRT_OverlayInfo* a, const VKRT_OverlayInfo* b) {
    if (!a || !b) return false;

    return a->window == b->window &&
        a->instance == b->instance &&
        a->physicalDevice == b->physicalDevice &&
        a->device == b->device &&
        a->graphicsQueueFamily == b->graphicsQueueFamily &&
        a->graphicsQueue == b->graphicsQueue &&
        a->descriptorPool == b->descriptorPool &&
        a->renderPass == b->renderPass &&
        a->swapchainImageCount == b->swapchainImageCount &&
        a->swapchainMinImageCount == b->swapchainMinImageCount;
}

static void populateVulkanInitInfo(const VKRT_OverlayInfo* overlay, ImGui_ImplVulkan_InitInfo* outInitInfo) {
    if (!overlay || !outInitInfo) return;

    *outInitInfo = (ImGui_ImplVulkan_InitInfo){
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
        .CheckVkResultFn = VK_NULL_HANDLE,
        .RenderPass = overlay->renderPass,
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

    ImGui_ImplVulkan_InitInfo initInfo = {0};
    populateVulkanInitInfo(overlay, &initInfo);
    if (!cImGui_ImplVulkan_Init(&initInfo)) {
        return false;
    }
    if (!cImGui_ImplVulkan_CreateFontsTexture()) {
        cImGui_ImplVulkan_Shutdown();
        return false;
    }

    state->overlay = *overlay;
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

static void rebuildEditorFonts(ImGuiIO* io, float uiScale) {
    if (!io) return;

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

    unsigned char* pixels = NULL;
    int width = 0;
    int height = 0;
    ImFontAtlas_GetTexDataAsRGBA32(io->Fonts, &pixels, &width, &height, NULL);
}

static void applyEditorUIScale(EditorUIState* state, float uiScale, bool refreshVulkanFonts) {
    if (!state) return;
    if (state->uiScale > 0.0f && fabsf(uiScale - state->uiScale) <= kEditorScaleEpsilon) return;

    ImGuiIO* io = ImGui_GetIO();
    ImGuiStyle* style = ImGui_GetStyle();
    if (state->uiScale <= 0.0f) {
        editorThemeApplyDefault();
        ImGuiStyle_ScaleAllSizes(style, uiScale);
    } else {
        ImGuiStyle_ScaleAllSizes(style, uiScale / state->uiScale);
    }

    rebuildEditorFonts(io, uiScale);
    state->uiScale = uiScale;

    if (refreshVulkanFonts) {
        cImGui_ImplVulkan_CreateFontsTexture();
    }
}

static float queryTopMenuBarHeight(void) {
    return ImGui_GetFrameHeight();
}

static void drawMainMenuBar(VKRT* vkrt, Session* session, const VKRT_RenderStatusSnapshot* status) {
    if (!vkrt || !session) return;
    if (!ImGui_BeginMainMenuBar()) return;

    if (ImGui_BeginMenu("File")) {
        if (ImGui_MenuItem("Import Mesh")) {
            sessionRequestMeshImportDialog(session);
        }

        bool canSaveRender = status &&
            status->renderModeActive &&
            status->renderModeFinished &&
            !session->runtime.sequencer.active;
        if (ImGui_MenuItemEx("Save Render", NULL, false, canSaveRender)) {
            sessionRequestRenderSaveDialog(session);
        }

        ImGui_Separator();
        ImGui_MenuItemEx("Open Scene", NULL, false, false);
        ImGui_MenuItemEx("Recent Scenes", NULL, false, false);
        ImGui_EndMenu();
    }

    if (ImGui_BeginMenu("View")) {
        if (ImGui_MenuItemEx("Reset Accumulation", NULL, false, status != NULL)) {
            VKRT_Result result = VKRT_invalidateAccumulation(vkrt);
            if (result != VKRT_SUCCESS) {
                LOG_ERROR("Resetting accumulation failed (%d)", (int)result);
            }
        }

        ImGui_Separator();
        ImGui_MenuItemEx("Panels", NULL, false, false);
        ImGui_EndMenu();
    }

    if (ImGui_BeginMenu("Help")) {
        ImGui_MenuItemEx("Controls", NULL, false, false);
        ImGui_MenuItemEx("About", NULL, false, false);
        ImGui_EndMenu();
    }

    ImGui_EndMainMenuBar();
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
    ImGui_DockBuilderSplitNode(centerWorkspaceDockID, ImGuiDir_Right, kPropertiesDockFraction, &propertiesDockID, &viewportDockID);

    ImGui_DockBuilderDockWindow("Scene Browser", sceneDockID);
    ImGui_DockBuilderDockWindow("Properties", propertiesDockID);
    ImGui_DockBuilderDockWindow("Viewport###ViewWindow", viewportDockID);

    ImGuiDockNode* sceneDockNode = ImGui_DockBuilderGetNode(sceneDockID);
    if (sceneDockNode) {
        ImGuiDockNodeFlags localFlags = sceneDockNode->LocalFlags |
            ImGuiDockNodeFlags_NoTabBar |
            ImGuiDockNodeFlags_NoWindowMenuButton |
            ImGuiDockNodeFlags_NoCloseButton;
        ImGuiDockNode_SetLocalFlags(sceneDockNode, localFlags);
    }
    ImGuiDockNode* propertiesDockNode = ImGui_DockBuilderGetNode(propertiesDockID);
    if (propertiesDockNode) {
        ImGuiDockNodeFlags localFlags = propertiesDockNode->LocalFlags |
            ImGuiDockNodeFlags_NoTabBar |
            ImGuiDockNodeFlags_NoWindowMenuButton |
            ImGuiDockNodeFlags_NoCloseButton;
        ImGuiDockNode_SetLocalFlags(propertiesDockNode, localFlags);
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
    initializeDockLayout(state);

    ImGui_End();
    return true;
}

static bool drawViewportWindow(
    VKRT* vkrt,
    const VKRT_RenderStatusSnapshot* status,
    VKRT_RuntimeSnapshot* runtime
) {
    if (!vkrt || !status || !runtime) return false;

    ImGuiWindowClass viewportWindowClass = {0};
    viewportWindowClass.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoTabBar |
                                                   ImGuiDockNodeFlags_NoUndocking |
                                                   ImGuiDockNodeFlags_NoWindowMenuButton |
                                                   ImGuiDockNodeFlags_NoCloseButton;
    ImGui_SetNextWindowClass(&viewportWindowClass);

    ImGui_PushStyleVarImVec2(ImGuiStyleVar_WindowPadding, (ImVec2){0.0f, 0.0f});
    const char* viewportWindowLabel = status->renderModeActive
        ? "Render###ViewWindow"
        : "Viewport###ViewWindow";
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

static void applyCompletedViewportSelection(VKRT* vkrt) {
    if (!vkrt) return;

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
    }
}

static bool queryViewportMousePixel(uint32_t* outX, uint32_t* outY) {
    if (!outX || !outY) return false;

    ImGuiIO* io = ImGui_GetIO();
    float scaleX = io->DisplayFramebufferScale.x > 0.0f ? io->DisplayFramebufferScale.x : 1.0f;
    float scaleY = io->DisplayFramebufferScale.y > 0.0f ? io->DisplayFramebufferScale.y : 1.0f;
    float mouseX = io->MousePos.x * scaleX;
    float mouseY = io->MousePos.y * scaleY;
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
    if (mouseX < viewportX || mouseY < viewportY ||
        mouseX >= viewportX + viewportWidth || mouseY >= viewportY + viewportHeight) {
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
    if (status->renderModeActive) return;

    if (!ImGui_IsMouseClicked(ImGuiMouseButton_Left)) return;

    uint32_t selectionX = 0;
    uint32_t selectionY = 0;
    if (!queryViewportClickPixel(runtime, &selectionX, &selectionY)) return;

    VKRT_Result result = VKRT_requestSelectionAtPixel(vkrt, selectionX, selectionY);
    if (result != VKRT_SUCCESS) {
        LOG_ERROR("Requesting viewport selection failed (%d)", (int)result);
    }
}

static void clearViewportSelectionOnEscape(VKRT* vkrt) {
    if (!vkrt) return;
    if (!ImGui_IsKeyPressed(ImGuiKey_Escape)) return;

    VKRT_Result result = VKRT_setSelectedMesh(vkrt, VKRT_INVALID_INDEX);
    if (result != VKRT_SUCCESS) {
        LOG_ERROR("Clearing viewport selection failed (%d)", (int)result);
    }
}

static void queueSelectedMeshRemovalOnDelete(
    VKRT* vkrt,
    Session* session,
    const VKRT_RenderStatusSnapshot* status
) {
    if (!vkrt || !session || !status) return;
    if (status->renderModeActive) return;

    ImGuiIO* io = ImGui_GetIO();
    if (io->WantTextInput || ImGui_IsAnyItemActive() || ImGui_IsPopupOpen(NULL, ImGuiPopupFlags_AnyPopupId)) {
        return;
    }
    if (!ImGui_IsKeyPressed(ImGuiKey_Delete) && !ImGui_IsKeyPressed(ImGuiKey_Backspace)) return;

    uint32_t selectedMeshIndex = VKRT_INVALID_INDEX;
    VKRT_Result result = VKRT_getSelectedMesh(vkrt, &selectedMeshIndex);
    if (result != VKRT_SUCCESS) {
        LOG_ERROR("Querying selected mesh failed (%d)", (int)result);
        return;
    }
    if (selectedMeshIndex == VKRT_INVALID_INDEX) return;

    sessionQueueMeshRemoval(session, selectedMeshIndex);
}

static void applyEditorCameraInput(
    VKRT* vkrt,
    const VKRT_RenderStatusSnapshot* status,
    const VKRT_RuntimeSnapshot* runtime,
    bool viewportHovered
) {
    if (!vkrt || !status || !runtime) return;

    ImGuiIO* io = ImGui_GetIO();
    if (status->renderModeActive) {
        if (!viewportHovered) return;

        float zoom = 1.0f;
        float panX = 0.0f;
        float panY = 0.0f;
        VKRT_Result result = VKRT_getRenderViewState(vkrt, &zoom, &panX, &panY);
        if (result != VKRT_SUCCESS) {
            LOG_ERROR("Querying render view state failed (%d)", (int)result);
            return;
        }

        if (io->MouseWheel != 0.0f) {
            zoom = zoom * powf(kRenderViewWheelStep, io->MouseWheel);
            zoom = vkrtClampf(zoom, VKRT_RENDER_VIEW_ZOOM_MIN, VKRT_RENDER_VIEW_ZOOM_MAX);
            result = VKRT_setRenderViewState(vkrt, zoom, panX, panY);
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

        bool panning = ImGui_IsMouseDragging(ImGuiMouseButton_Middle, -1.0f) ||
                       ImGui_IsMouseDragging(ImGuiMouseButton_Right, -1.0f);
        if (panning) {
            float scaleX = io->DisplayFramebufferScale.x > 0.0f ? io->DisplayFramebufferScale.x : 1.0f;
            float scaleY = io->DisplayFramebufferScale.y > 0.0f ? io->DisplayFramebufferScale.y : 1.0f;
            float viewWidth = (float)(runtime->displayViewportRect[2] > 0 ? runtime->displayViewportRect[2] : 1u) / scaleX;
            float viewHeight = (float)(runtime->displayViewportRect[3] > 0 ? runtime->displayViewportRect[3] : 1u) / scaleY;
            float cropWidth = 1.0f;
            float cropHeight = 1.0f;

            result = VKRT_getRenderViewCrop(vkrt, zoom, &cropWidth, &cropHeight);
            if (result != VKRT_SUCCESS) {
                LOG_ERROR("Querying render view crop failed (%d)", (int)result);
                return;
            }

            panX -= (io->MouseDelta.x * cropWidth) / viewWidth;
            panY -= (io->MouseDelta.y * cropHeight) / viewHeight;
            result = VKRT_setRenderViewState(vkrt, zoom, panX, panY);
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
        .orbiting = viewportHovered && !io->KeyShift && ImGui_IsMouseDragging(ImGuiMouseButton_Middle, -1.0f),
        .panning = viewportHovered &&
            ((io->KeyShift && ImGui_IsMouseDragging(ImGuiMouseButton_Middle, -1.0f)) ||
             ImGui_IsMouseDragging(ImGuiMouseButton_Right, -1.0f)),
        .captureMouse = !viewportHovered,
    };
    VKRT_Result result = VKRT_applyCameraInput(vkrt, &cameraInput);
    if (result != VKRT_SUCCESS) {
        LOG_ERROR("Applying camera input failed (%d)", (int)result);
    }
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

    ImGuiIO* io = ImGui_GetIO();
    io->ConfigFlags |= ImGuiConfigFlags_DockingEnable;

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
    applyEditorUIScale(state, uiScale, false);

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

    uint64_t shutdownStartTime = getMicroseconds();

    uint64_t vulkanShutdownStartTime = getMicroseconds();
    shutdownEditorVulkanBackend(state);
    double vulkanShutdownMs = (double)(getMicroseconds() - vulkanShutdownStartTime) / 1e3;

    uint64_t glfwShutdownStartTime = getMicroseconds();
    shutdownEditorGlfwBackend(state);
    double glfwShutdownMs = (double)(getMicroseconds() - glfwShutdownStartTime) / 1e3;

    LOG_TRACE("UI backends shut down. Vulkan: %.3f ms, GLFW: %.3f ms", vulkanShutdownMs, glfwShutdownMs);
    LOG_TRACE("Destroying UI context");

    ImGui_DestroyContext(NULL);
    editorUIShutdownDialogs(session);

    LOG_TRACE("UI shutdown complete in %.3f ms", (double)(getMicroseconds() - shutdownStartTime) / 1e3);

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

    applyEditorUIScale(state, queryEditorContentScale(state->window), true);

    cImGui_ImplGlfw_NewFrame();
    cImGui_ImplVulkan_NewFrame();
    ImGui_NewFrame();
    applyCompletedViewportSelection(vkrt);
    clearViewportSelectionOnEscape(vkrt);

    VKRT_RenderStatusSnapshot status = {0};
    bool hasStatus = VKRT_getRenderStatus(vkrt, &status) == VKRT_SUCCESS;
    VKRT_RuntimeSnapshot runtime = {0};
    bool hasRuntime = VKRT_getRuntimeSnapshot(vkrt, &runtime) == VKRT_SUCCESS;
    queueSelectedMeshRemovalOnDelete(vkrt, session, hasStatus ? &status : NULL);

    drawMainMenuBar(vkrt, session, hasStatus ? &status : NULL);
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
