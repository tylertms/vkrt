#include "editor.h"

#include "dcimgui.h"
#include "dcimgui_impl_glfw.h"
#include "dcimgui_impl_vulkan.h"
#include "dcimgui_internal.h"
#include "session.h"
#include "theme.h"
#include "debug.h"
#include "nfd.h"
#include "IBMPlexMono_Regular.h"
#include "fa_solid_900.h"
#include "IconsFontAwesome6.h"

#include <math.h>
#include <float.h>
#include <stdbool.h>
#include <limits.h>
#include <stdio.h>

static const float kEditorBaseTextSizePx = 15.5f;
static const float kEditorBaseIconSizePx = 11.0f;
static const float kEditorScaleEpsilon = 0.01f;
static float gEditorUIScale = 0.0f;
static const float kRenderViewWheelStep = 1.12f;
static const float kInspectorControlSpacing = 1.0f;
static const float kInspectorSectionIndent = 10.0f;
static const float kInspectorCollapsedWidth = 46.0f;
static const float kInspectorMinExpandedWidth = 140.0f;
static const ImVec2 kTooltipPadding = {8.0f, 4.0f};

typedef struct VerticalIconTab {
    const char* icon;
    const char* tooltip;
} VerticalIconTab;

typedef enum InspectorTab {
    INSPECTOR_TAB_MAIN = 0,
    INSPECTOR_TAB_CAMERA,
    INSPECTOR_TAB_RENDER,
    INSPECTOR_TAB_SCENE,
    INSPECTOR_TAB_COUNT
} InspectorTab;

static void tooltipOnHover(const char* text);
static void drawPaddedTooltip(const char* text);
static void drawInspectorVerticalDivider(void);
static void syncInspectorDockWidthForTabState(int currentTab);
static void formatDriverVersionText(uint32_t vendorID, uint32_t driverVersion, char* out, size_t outSize);

static uint32_t absDiffU32(uint32_t a, uint32_t b) {
    return (a > b) ? (a - b) : (b - a);
}

static float queryInspectorInputWidth(float preferredWidth, float labelReserve) {
    float available = ImGui_GetContentRegionAvail().x;
    float width = available - labelReserve;
    if (width < 96.0f) width = available * 0.58f;
    if (width < 72.0f) width = 72.0f;
    if (width > preferredWidth) width = preferredWidth;
    return width;
}

static void formatDriverVersionText(uint32_t vendorID, uint32_t driverVersion, char* out, size_t outSize) {
    if (!out || outSize == 0) return;

    if (vendorID == 0x10DEu) { // NVIDIA
        uint32_t major = (driverVersion >> 22u) & 0x3ffu;
        uint32_t minor = (driverVersion >> 14u) & 0xffu;
        snprintf(out, outSize, "%u.%02u", major, minor);
        return;
    }

    if (vendorID == 0x8086u) { // Intel
        uint32_t major = driverVersion >> 14u;
        uint32_t minor = driverVersion & 0x3fffu;
        if (major > 0 && minor > 0) {
            snprintf(out, outSize, "%u.%u", major, minor);
            return;
        }
    }

    snprintf(out, outSize, "%u.%u.%u",
        VK_API_VERSION_MAJOR(driverVersion),
        VK_API_VERSION_MINOR(driverVersion),
        VK_API_VERSION_PATCH(driverVersion));
}

static void drawVerticalIconTabs(const VerticalIconTab* tabs, int tabCount, int* currentTab) {
    const float barWidth = 44.f;
    const float btnSize = 30.f;
    const ImVec2 iconSize = { btnSize, btnSize };
    const ImVec4* colors = ImGui_GetStyle()->Colors;

    if (!tabs || tabCount <= 0 || !currentTab) return;
    if (*currentTab < -1 || *currentTab >= tabCount) *currentTab = 0;

    ImGui_PushStyleVarImVec2(ImGuiStyleVar_WindowPadding, (ImVec2){0.0f, 2.0f});
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_ItemSpacing, (ImVec2){0.0f, 6.0f});
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_ButtonTextAlign, (ImVec2){0.5f, 0.36f});
    ImGui_BeginChild("##icon_bar", (ImVec2){ barWidth, 0 },
                      ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground |
                      ImGuiWindowFlags_NoScrollbar |
                      ImGuiWindowFlags_NoScrollWithMouse
    );

    for (int i = 0; i < tabCount; i++) {
        ImGui_PushIDInt(i);
        bool sel = (*currentTab == i);

        if (sel) {
            ImGui_PushStyleColorImVec4(ImGuiCol_Button, colors[ImGuiCol_Button]);
            ImGui_PushStyleColorImVec4(ImGuiCol_ButtonHovered, colors[ImGuiCol_ButtonHovered]);
            ImGui_PushStyleColorImVec4(ImGuiCol_ButtonActive, colors[ImGuiCol_ButtonActive]);
        } else {
            ImGui_PushStyleColorImVec4(ImGuiCol_Button, (ImVec4){ 0.0f,0.0f,0.0f,0.0f });
            ImGui_PushStyleColorImVec4(ImGuiCol_ButtonHovered, colors[ImGuiCol_ButtonHovered]);
            ImGui_PushStyleColorImVec4(ImGuiCol_ButtonActive, colors[ImGuiCol_ButtonActive]);
        }

        ImGui_SetCursorPosX((barWidth - btnSize) * 0.5f);
        ImGui_PushStyleVarImVec2(ImGuiStyleVar_FramePadding, (ImVec2){0.0f, 1.0f});
        bool pressed = ImGui_ButtonEx(tabs[i].icon, iconSize);
        ImGui_PopStyleVar();
        if (pressed) *currentTab = sel ? -1 : i;

        if (ImGui_IsItemHovered(ImGuiHoveredFlags_None) && tabs[i].tooltip) {
            drawPaddedTooltip(tabs[i].tooltip);
        }

        ImGui_PopStyleColorEx(3);

        ImGui_PopID();
    }

    ImGui_EndChild();
    ImGui_PopStyleVarEx(3);
}


static char* openMeshImportDialog(void) {
    nfdchar_t* outPath = NULL;
    nfdfilteritem_t filters[] = {{"glTF 2.0", "glb,gltf"}};
    if (NFD_OpenDialog(&outPath, filters, 1, "assets/models") != NFD_OKAY) return NULL;
    return outPath;
}

static char* openRenderSaveDialog(void) {
    nfdchar_t* outPath = NULL;
    nfdfilteritem_t filters[] = {{"PNG image", "png"}};
    if (NFD_SaveDialog(&outPath, filters, 1, "captures", "render.png") != NFD_OKAY) return NULL;
    return outPath;
}

static char* openRenderSequenceFolderDialog(const Session* session) {
    const char* defaultPath = sessionGetRenderSequenceFolder(session);
    if (!defaultPath || !defaultPath[0]) defaultPath = "captures/sequence";
    nfdchar_t* outPath = NULL;
    if (NFD_PickFolder(&outPath, defaultPath) != NFD_OKAY) return NULL;
    return outPath;
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
    ImGuiWindowClass viewportWindowClass = {0};
    viewportWindowClass.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoTabBar |
                                                   ImGuiDockNodeFlags_NoUndocking |
                                                   ImGuiDockNodeFlags_NoWindowMenuButton |
                                                   ImGuiDockNodeFlags_NoCloseButton;
    ImGui_SetNextWindowClass(&viewportWindowClass);

    ImGui_PushStyleVarImVec2(ImGuiStyleVar_WindowPadding, (ImVec2){0.0f, 0.0f});
    const char* viewportWindowLabel = vkrt->state.renderModeActive
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

    const uint32_t* prevViewport = vkrt->runtime.displayViewportRect;
    if (absDiffU32(viewportX, prevViewport[0]) <= 1 &&
        absDiffU32(viewportY, prevViewport[1]) <= 1 &&
        absDiffU32(viewportWidth, prevViewport[2]) <= 1 &&
        absDiffU32(viewportHeight, prevViewport[3]) <= 1) {
        viewportX = prevViewport[0];
        viewportY = prevViewport[1];
        viewportWidth = prevViewport[2];
        viewportHeight = prevViewport[3];
    }

    VKRT_setRenderViewport(vkrt, viewportX, viewportY, viewportWidth, viewportHeight);

    ImGui_End();
    ImGui_PopStyleVar();

    return viewportHovered;
}

static void drawPerformanceSection(VKRT* vkrt, bool controlsDisabled) {
    if (ImGui_CollapsingHeader("Performance Stats", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui_Indent();
        ImGui_Text("FPS:          %8u", vkrt->state.framesPerSecond);
        ImGui_Text("Frames:       %8u", vkrt->state.accumulationFrame);
        ImGui_Text("Samples:  %12llu", (unsigned long long)vkrt->state.totalSamples);
        ImGui_Text("Samples / px: %8u", vkrt->state.samplesPerPixel);
        ImGui_Text("Frame (ms):   %8.3f ms", vkrt->state.displayFrameTimeMs);
        ImGui_Text("Render (ms):  %8.3f ms", vkrt->state.displayRenderTimeMs);
        ImGui_Dummy((ImVec2){0.0f, kInspectorControlSpacing});
        ImGui_Unindent();
    }

    if (!ImGui_CollapsingHeader("Sampling", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui_Indent();
    if (controlsDisabled) ImGui_BeginDisabled(true);

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

    if (controlsDisabled) {
        ImGui_EndDisabled();
    }
    ImGui_Dummy((ImVec2){0.0f, kInspectorControlSpacing});
    ImGui_Unindent();
}

static void drawCameraEffectsSection(VKRT* vkrt, bool controlsDisabled) {
    if (!ImGui_CollapsingHeader("Shading & Effects", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui_Indent();
    if (controlsDisabled) ImGui_BeginDisabled(true);

    int maxBounces = (int)vkrt->state.maxBounces;
    if (ImGui_SliderIntEx("Max Bounces", &maxBounces, 1, 64, "%d", ImGuiSliderFlags_AlwaysClamp)) {
        vkrt->state.maxBounces = (uint32_t)maxBounces;
        VKRT_invalidateAccumulation(vkrt);
    }

    float fogDensity = vkrt->state.fogDensity;
    if (ImGui_DragFloatEx("Fog Density", &fogDensity, 0.0005f, 0.0f, 4.0f, "%.4f", ImGuiSliderFlags_AlwaysClamp)) {
        VKRT_setFogDensity(vkrt, fogDensity);
    }
    tooltipOnHover("Global homogeneous fog density.");

    const char* toneMappingLabels[] = {"None", "ACES"};
    int toneMappingMode = (int)vkrt->state.toneMappingMode;
    if (ImGui_ComboCharEx("Tone Mapping", &toneMappingMode, toneMappingLabels, 2, 2)) {
        VKRT_setToneMappingMode(vkrt, (VKRT_ToneMappingMode)toneMappingMode);
    }

    if (ImGui_Button(ICON_FA_ARROWS_ROTATE " Reset accumulation")) {
        VKRT_invalidateAccumulation(vkrt);
    }

    if (controlsDisabled) {
        ImGui_EndDisabled();
    }
    ImGui_Dummy((ImVec2){0.0f, kInspectorControlSpacing});
    ImGui_Unindent();
}

static uint32_t clampRenderDimension(int value) {
    if (value < 1) return 1;
    if (value > 16384) return 16384;
    return (uint32_t)value;
}

static void drawCameraSection(VKRT* vkrt, bool renderModeActive) {
    if (!ImGui_CollapsingHeader("Camera Pose", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui_Indent();
    ImGui_BeginDisabled(renderModeActive);

    vec3 position = {0.0f, 0.0f, 0.0f};
    vec3 target = {0.0f, 0.0f, 0.0f};
    vec3 up = {0.0f, 0.0f, 1.0f};
    float fov = 40.0f;
    VKRT_cameraGetPose(vkrt, position, target, up, &fov);

    bool changed = false;
    changed |= ImGui_DragFloat3Ex("Position", position, 0.01f, 0.0f, 0.0f, "%.3f", ImGuiSliderFlags_None);
    changed |= ImGui_DragFloat3Ex("Target", target, 0.01f, 0.0f, 0.0f, "%.3f", ImGuiSliderFlags_None);
    bool fovChanged = ImGui_SliderFloatEx("FOV", &fov, 10.0f, 140.0f, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);

    if (!changed && !fovChanged) {
        ImGui_EndDisabled();
        ImGui_Dummy((ImVec2){0.0f, kInspectorControlSpacing});
        ImGui_Unindent();
        return;
    }

    vec3 viewDir;
    glm_vec3_sub(target, position, viewDir);
    if (glm_vec3_norm2(viewDir) < 1e-8f) {
        target[0] = position[0] + 0.001f;
        target[1] = position[1];
        target[2] = position[2];
    }
    VKRT_cameraSetPose(vkrt, position, target, up, fov);
    ImGui_EndDisabled();
    ImGui_Dummy((ImVec2){0.0f, kInspectorControlSpacing});
    ImGui_Unindent();
}


static float clampRenderViewValue(float value, float minValue, float maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

static void drawPaddedTooltip(const char* text) {
    if (!text || !text[0]) return;
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_WindowPadding, kTooltipPadding);
    ImGui_BeginTooltip();
    ImGui_Text("%s", text);
    ImGui_EndTooltip();
    ImGui_PopStyleVar();
}

static void tooltipOnHover(const char* text) {
    if (!text) return;
    if (ImGui_IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
        drawPaddedTooltip(text);
    }
}

static void drawInspectorVerticalDivider(void) {
    ImVec2 cursor = ImGui_GetCursorScreenPos();
    float height = ImGui_GetContentRegionAvail().y;
    ImDrawList_AddLineEx(ImGui_GetWindowDrawList(),
        (ImVec2){cursor.x + 0.5f, cursor.y},
        (ImVec2){cursor.x + 0.5f, cursor.y + height},
        ImGui_GetColorU32(ImGuiCol_Separator),
        1.0f);
    ImGui_Dummy((ImVec2){1.0f, height});
}

static void syncInspectorDockWidthForTabState(int currentTab) {
    static bool wasCollapsed = false;
    static float expandedWidth = 0.0f;

    ImGuiDockNode* inspectorNode = ImGui_GetWindowDockNode();
    if (!inspectorNode) return;

    ImGuiDockNode* parentNode = inspectorNode->ParentNode;
    if (!parentNode || parentNode->SplitAxis != ImGuiAxis_X) return;

    ImGuiDockNode* siblingNode = NULL;
    if (parentNode->ChildNodes[0] == inspectorNode) siblingNode = parentNode->ChildNodes[1];
    else if (parentNode->ChildNodes[1] == inspectorNode) siblingNode = parentNode->ChildNodes[0];
    if (!siblingNode) return;

    float totalWidth = parentNode->Size.x;
    if (totalWidth <= 2.0f) return;

    bool collapsed = currentTab < 0;
    if (!collapsed && expandedWidth <= 0.0f) {
        expandedWidth = inspectorNode->Size.x;
    }

    if (collapsed) {
        if (!wasCollapsed) {
            expandedWidth = inspectorNode->Size.x;
        }

        float collapsedWidth = kInspectorCollapsedWidth;
        if (collapsedWidth < 1.0f) collapsedWidth = 1.0f;
        if (collapsedWidth > totalWidth - 1.0f) collapsedWidth = totalWidth - 1.0f;

        inspectorNode->SizeRef.x = collapsedWidth;
        siblingNode->SizeRef.x = totalWidth - collapsedWidth;
        parentNode->WantLockSizeOnce = true;
        wasCollapsed = true;
        return;
    }

    if (wasCollapsed) {
        float restoredWidth = expandedWidth;
        if (restoredWidth < kInspectorMinExpandedWidth) restoredWidth = totalWidth * 0.26f;
        if (restoredWidth > totalWidth - 1.0f) restoredWidth = totalWidth - 1.0f;

        inspectorNode->SizeRef.x = restoredWidth;
        siblingNode->SizeRef.x = totalWidth - restoredWidth;
        parentNode->WantLockSizeOnce = true;
        wasCollapsed = false;
    }
}

static void queryRenderSourceExtent(const VKRT* vkrt, float* outWidth, float* outHeight) {
    if (!vkrt || !outWidth || !outHeight) return;

    uint32_t width = vkrt->runtime.renderExtent.width;
    uint32_t height = vkrt->runtime.renderExtent.height;
    if (width == 0 || height == 0) {
        width = vkrt->runtime.swapChainExtent.width;
        height = vkrt->runtime.swapChainExtent.height;
    }
    if (width == 0) width = 1;
    if (height == 0) height = 1;

    *outWidth = (float)width;
    *outHeight = (float)height;
}

static void queryDisplayViewportExtent(const VKRT* vkrt, float* outWidth, float* outHeight) {
    if (!vkrt || !outWidth || !outHeight) return;

    uint32_t width = vkrt->runtime.displayViewportRect[2];
    uint32_t height = vkrt->runtime.displayViewportRect[3];
    if (width == 0 || height == 0) {
        width = vkrt->runtime.swapChainExtent.width;
        height = vkrt->runtime.swapChainExtent.height;
    }
    if (width == 0) width = 1;
    if (height == 0) height = 1;

    *outWidth = (float)width;
    *outHeight = (float)height;
}

static void queryRenderViewCrop(const VKRT* vkrt, float zoom, float* outWidth, float* outHeight) {
    if (!vkrt || !outWidth || !outHeight) return;

    float sourceWidth = 1.0f;
    float sourceHeight = 1.0f;
    queryRenderSourceExtent(vkrt, &sourceWidth, &sourceHeight);

    float clampedZoom = clampRenderViewValue(zoom, VKRT_RENDER_VIEW_ZOOM_MIN, VKRT_RENDER_VIEW_ZOOM_MAX);
    bool fillViewport = clampedZoom > (VKRT_RENDER_VIEW_ZOOM_MIN + 0.0001f);
    float cropWidth = sourceWidth;
    float cropHeight = sourceHeight;

    if (fillViewport) {
        float viewWidth = 1.0f;
        float viewHeight = 1.0f;
        queryDisplayViewportExtent(vkrt, &viewWidth, &viewHeight);

        float sourceAspect = sourceWidth / sourceHeight;
        float viewAspect = viewWidth / viewHeight;
        float baseWidth = sourceWidth;
        float baseHeight = sourceHeight;
        if (viewAspect > sourceAspect) {
            baseHeight = sourceWidth / viewAspect;
        } else {
            baseWidth = sourceHeight * viewAspect;
        }

        cropWidth = baseWidth / clampedZoom;
        cropHeight = baseHeight / clampedZoom;
        if (cropWidth < 1.0f) cropWidth = 1.0f;
        if (cropHeight < 1.0f) cropHeight = 1.0f;
        if (cropWidth > sourceWidth) cropWidth = sourceWidth;
        if (cropHeight > sourceHeight) cropHeight = sourceHeight;
    }

    *outWidth = cropWidth;
    *outHeight = cropHeight;
}

static void clampRenderViewPan(VKRT* vkrt) {
    if (!vkrt) return;

    float sourceWidth = 1.0f;
    float sourceHeight = 1.0f;
    queryRenderSourceExtent(vkrt, &sourceWidth, &sourceHeight);

    float cropWidth = sourceWidth;
    float cropHeight = sourceHeight;
    queryRenderViewCrop(vkrt, vkrt->state.renderViewZoom, &cropWidth, &cropHeight);

    float maxPanX = (sourceWidth - cropWidth) * 0.5f;
    float maxPanY = (sourceHeight - cropHeight) * 0.5f;
    if (maxPanX <= 0.0f) vkrt->state.renderViewPanX = 0.0f;
    else vkrt->state.renderViewPanX = clampRenderViewValue(vkrt->state.renderViewPanX, -maxPanX, maxPanX);
    if (maxPanY <= 0.0f) vkrt->state.renderViewPanY = 0.0f;
    else vkrt->state.renderViewPanY = clampRenderViewValue(vkrt->state.renderViewPanY, -maxPanY, maxPanY);
}

static void initializeRenderConfig(Session* session) {
    if (!session) return;
    if (session->renderConfig.width == 0 || session->renderConfig.height == 0) {
        session->renderConfig.width = 1920;
        session->renderConfig.height = 1080;
    }
    sessionSanitizeAnimationSettings(&session->renderConfig.animation);
}

static void formatEstimatedDuration(float seconds, char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    if (!(seconds > 0.0f)) {
        snprintf(out, outSize, "0s");
        return;
    }

    uint64_t total = (uint64_t)(seconds + 0.5f);
    if (total == 0) {
        snprintf(out, outSize, "0s");
        return;
    }

    static const uint64_t unitSeconds[] = {86400ull, 3600ull, 60ull, 1ull};
    static const char* unitLabels[] = {"d", "h", "m", "s"};

    int firstUnit = -1;
    uint64_t firstValue = 0;
    uint64_t remainder = total;
    for (int i = 0; i < 4; i++) {
        uint64_t value = remainder / unitSeconds[i];
        if (value == 0) continue;
        firstUnit = i;
        firstValue = value;
        remainder %= unitSeconds[i];
        break;
    }

    if (firstUnit < 0) {
        snprintf(out, outSize, "0s");
        return;
    }

    int secondUnit = -1;
    uint64_t secondValue = 0;
    for (int i = firstUnit + 1; i < 4; i++) {
        uint64_t value = remainder / unitSeconds[i];
        if (value == 0) continue;
        secondUnit = i;
        secondValue = value;
        break;
    }

    if (secondUnit >= 0) {
        snprintf(out, outSize, "%llu%s %llu%s",
            (unsigned long long)firstValue, unitLabels[firstUnit],
            (unsigned long long)secondValue, unitLabels[secondUnit]);
    } else {
        snprintf(out, outSize, "%llu%s",
            (unsigned long long)firstValue, unitLabels[firstUnit]);
    }
}

static void drawRenderSection(VKRT* vkrt, Session* session) {
    initializeRenderConfig(session);

    SessionRenderAnimationSettings* anim = &session->renderConfig.animation;

    if (!vkrt->state.renderModeActive) {
        int outputSize[2] = {(int)session->renderConfig.width, (int)session->renderConfig.height};
        int targetSamples = (int)session->renderConfig.targetSamples;
        bool animationEnabled = anim->enabled != 0;
        uint32_t frameCount = sessionComputeAnimationFrameCount(anim);
        const char* sequenceFolder = sessionGetRenderSequenceFolder(session);
        if (!sequenceFolder || !sequenceFolder[0]) sequenceFolder = "(not set)";

        const float inputWidth = queryInspectorInputWidth(220.0f, 132.0f);
        const float folderWidth = queryInspectorInputWidth(260.0f, 100.0f);

        if (ImGui_CollapsingHeader("Output", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui_Indent();
            ImGui_SetNextItemWidth(inputWidth);
            if (ImGui_DragInt2Ex(ICON_FA_CAMERA_RETRO " Output Size", outputSize, 1.0f, 1, 16384, "%d", ImGuiSliderFlags_AlwaysClamp)) {
                session->renderConfig.width = clampRenderDimension(outputSize[0]);
                session->renderConfig.height = clampRenderDimension(outputSize[1]);
            }

            ImGui_SetNextItemWidth(inputWidth);
            if (ImGui_DragIntEx(ICON_FA_IMAGES " Samples", &targetSamples, 1.0f, 0, INT_MAX, "%d", ImGuiSliderFlags_AlwaysClamp)) {
                if (targetSamples < 0) targetSamples = 0;
                session->renderConfig.targetSamples = (uint32_t)targetSamples;
            }
            tooltipOnHover("Total samples to render. Set to 0 for manual stop.");
            ImGui_Dummy((ImVec2){0.0f, kInspectorControlSpacing});
            ImGui_Unindent();
        }

        if (ImGui_CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui_Indent();
            if (ImGui_Checkbox("Enabled##render_animation_enabled", &animationEnabled)) {
                anim->enabled = animationEnabled ? 1 : 0;
                if (!animationEnabled) {
                    sessionSanitizeAnimationSettings(anim);
                }
            }
            tooltipOnHover("Render a sequence by stepping light travel time from Min to Max.");
            ImGui_Dummy((ImVec2){0.0f, kInspectorControlSpacing});

            float timeMin = anim->minTime;
            float timeMax = anim->maxTime;
            float timeStep = anim->timeStep;
            SessionSceneTimelineSettings* sceneTimeline = &anim->sceneTimeline;
            ImGui_BeginDisabled(!animationEnabled);

            ImGui_SetNextItemWidth(inputWidth);
            if (ImGui_DragFloatEx(ICON_FA_TIMELINE " Time Min", &timeMin, 0.01f, 0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp)) {
                anim->minTime = timeMin;
                sessionSanitizeAnimationSettings(anim);
                frameCount = sessionComputeAnimationFrameCount(anim);
            }
            tooltipOnHover("Sequence start time.");

            ImGui_SetNextItemWidth(inputWidth);
            if (ImGui_DragFloatEx(ICON_FA_TIMELINE " Time Max", &timeMax, 0.01f, 0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp)) {
                anim->maxTime = timeMax;
                sessionSanitizeAnimationSettings(anim);
                frameCount = sessionComputeAnimationFrameCount(anim);
            }
            tooltipOnHover("Sequence end time.");

            ImGui_SetNextItemWidth(inputWidth);
            if (ImGui_DragFloatEx(ICON_FA_CLOCK " Step", &timeStep, 0.005f, 0.001f, 1000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp)) {
                anim->timeStep = timeStep;
                sessionSanitizeAnimationSettings(anim);
                frameCount = sessionComputeAnimationFrameCount(anim);
            }
            tooltipOnHover("Time increment per sequence frame.");

            ImGui_Text(ICON_FA_IMAGES " Frames: %u", frameCount);
            ImGui_Dummy((ImVec2){0.0f, kInspectorControlSpacing});

            if (ImGui_Button(ICON_FA_FOLDER_OPEN " Select Folder")) {
                sessionRequestRenderSequenceFolderDialog(session);
            }

            char folderPathBuffer[256] = {0};
            snprintf(folderPathBuffer, sizeof(folderPathBuffer), "%s", sequenceFolder);
            ImGui_SetNextItemWidth(folderWidth);
            ImGui_InputTextEx("Folder", folderPathBuffer, sizeof(folderPathBuffer), ImGuiInputTextFlags_ReadOnly, NULL, NULL);
            tooltipOnHover(sequenceFolder);

            ImGui_Dummy((ImVec2){0.0f, kInspectorControlSpacing * 1.25f});

            if (ImGui_CollapsingHeader("Timeline", ImGuiTreeNodeFlags_None)) {
                ImGui_Indent();
                bool timelineEnabled = sceneTimeline->enabled != 0;
                if (ImGui_Checkbox("Enabled##render_timeline_enabled", &timelineEnabled)) {
                    sceneTimeline->enabled = timelineEnabled ? 1 : 0;
                    sessionSanitizeAnimationSettings(anim);
                }
                tooltipOnHover("Step emission values at discrete points in source time.");
                ImGui_Dummy((ImVec2){0.0f, kInspectorControlSpacing});

                ImGui_BeginDisabled(!timelineEnabled);
                bool timelineEdited = false;
                bool timeActivelyEditing = false;

                if (ImGui_Button(ICON_FA_PLUS " Add")) {
                    if (sceneTimeline->keyframeCount < SESSION_SCENE_TIMELINE_KEYFRAME_CAPACITY) {
                        SessionSceneTimelineKeyframe newKey = {
                            .time = 0.0f,
                            .emissionScale = 1.0f,
                            .emissionTint = {1.0f, 1.0f, 1.0f},
                        };
                        if (sceneTimeline->keyframeCount > 0) {
                            newKey = sceneTimeline->keyframes[sceneTimeline->keyframeCount - 1];
                            newKey.time += SESSION_SCENE_TIMELINE_DEFAULT_INCREMENT;
                        }
                        sceneTimeline->keyframes[sceneTimeline->keyframeCount] = newKey;
                        sceneTimeline->keyframeCount++;
                        timelineEdited = true;
                    }
                }

                if (sceneTimeline->keyframeCount > 1) {
                    ImGui_SameLine();
                    if (ImGui_Button(ICON_FA_MINUS " Remove")) {
                        sceneTimeline->keyframeCount--;
                        timelineEdited = true;
                    }
                }

                for (uint32_t keyIndex = 0; keyIndex < sceneTimeline->keyframeCount; keyIndex++) {
                    SessionSceneTimelineKeyframe* key = &sceneTimeline->keyframes[keyIndex];
                    ImGui_PushIDInt((int)keyIndex);
                    ImGui_SeparatorText("Marker");

                    ImGui_SetNextItemWidth(inputWidth);
                    ImGui_DragFloatEx("Time", &key->time, 0.01f,
                        SESSION_SCENE_TIMELINE_TIME_MIN,
                        SESSION_SCENE_TIMELINE_TIME_MAX,
                        "%.3f",
                        ImGuiSliderFlags_AlwaysClamp);
                    timelineEdited |= ImGui_IsItemDeactivatedAfterEdit();
                    timeActivelyEditing |= ImGui_IsItemActive();

                    ImGui_SetNextItemWidth(inputWidth);
                    timelineEdited |= ImGui_DragFloatEx("Emission Scale", &key->emissionScale, 0.01f,
                        SESSION_SCENE_TIMELINE_EMISSION_SCALE_MIN,
                        SESSION_SCENE_TIMELINE_EMISSION_SCALE_MAX,
                        "%.3f", ImGuiSliderFlags_AlwaysClamp);

                    timelineEdited |= ImGui_ColorEdit3("Emission Tint", key->emissionTint, ImGuiColorEditFlags_Float);

                    ImGui_PopID();
                }

                if (timelineEdited && !timeActivelyEditing) {
                    sessionSanitizeAnimationSettings(anim);
                }
                ImGui_EndDisabled();
                ImGui_Unindent();
            }

            if (session->renderConfig.targetSamples == 0) {
                ImGui_TextDisabled("Sequence mode needs finite samples. `0` will be promoted to `1`.");
            }
            ImGui_EndDisabled();
            ImGui_Unindent();
        }

        ImGui_Dummy((ImVec2){0.0f, kInspectorControlSpacing * 1.5f});
        const char* startLabel = animationEnabled
            ? ICON_FA_CLAPPERBOARD " Start Sequence"
            : ICON_FA_CAMERA " Start Render";
        if (ImGui_Button(startLabel)) {
            uint32_t startSamples = session->renderConfig.targetSamples;
            if (animationEnabled && startSamples == 0) startSamples = 1;
            sessionQueueRenderStart(session,
                session->renderConfig.width,
                session->renderConfig.height,
                startSamples,
                anim);
        }
        return;
    }

    const SessionSequenceProgress* seq = &session->sequenceProgress;

    if (ImGui_CollapsingHeader("Progress", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui_Indent();
        ImGui_Text(ICON_FA_CAMERA_RETRO " Output: %ux%u", vkrt->runtime.renderExtent.width, vkrt->runtime.renderExtent.height);
        if (vkrt->state.renderTargetSamples > 0) {
            uint64_t shownSamples = vkrt->state.totalSamples;
            if (shownSamples > vkrt->state.renderTargetSamples) {
                shownSamples = vkrt->state.renderTargetSamples;
            }
            float progress = (float)shownSamples / (float)vkrt->state.renderTargetSamples;
            if (progress < 0.0f) progress = 0.0f;
            if (progress > 1.0f) progress = 1.0f;
            ImGui_Text("Progress: %llu / %u Samples (%.1f%%)",
                (unsigned long long)shownSamples,
                vkrt->state.renderTargetSamples,
                progress * 100.0f);
        } else {
            ImGui_Text("Progress: %llu Samples", (unsigned long long)vkrt->state.totalSamples);
        }
        ImGui_Text("%s", vkrt->state.renderModeFinished ? "Status: Complete" : "Status: Rendering");
        if (seq->active) {
            ImGui_Text(ICON_FA_TIMELINE " Sequence: %u / %u  (t = %.3f)",
                seq->frameIndex, seq->frameCount, seq->currentTime);
            if (seq->hasEstimatedRemaining) {
                char etaText[32] = {0};
                formatEstimatedDuration(seq->estimatedRemainingSeconds, etaText, sizeof(etaText));
                ImGui_Text(ICON_FA_CLOCK " Remaining: %s", etaText);
            }
        } else if (!vkrt->state.renderModeFinished &&
                   vkrt->state.renderTargetSamples > 0 &&
                   vkrt->state.totalSamples > 0) {
            static uint64_t sRenderStartTimeUs = 0;
            static uint64_t sLastTotalSamples = 0;
            if (vkrt->state.totalSamples < sLastTotalSamples) {
                sRenderStartTimeUs = getMicroseconds();
            }
            sLastTotalSamples = vkrt->state.totalSamples;
            uint64_t nowUs = getMicroseconds();
            float elapsedSeconds = (float)(nowUs - sRenderStartTimeUs) / 1000000.0f;
            if (elapsedSeconds > 0.5f) {
                float rate = (float)vkrt->state.totalSamples / elapsedSeconds;
                uint64_t remaining = vkrt->state.renderTargetSamples - vkrt->state.totalSamples;
                float etaSeconds = (float)remaining / rate;
                char etaText[32] = {0};
                formatEstimatedDuration(etaSeconds, etaText, sizeof(etaText));
                ImGui_Text(ICON_FA_CLOCK " Remaining: %s", etaText);
            }
        }
        ImGui_Dummy((ImVec2){0.0f, kInspectorControlSpacing});
        ImGui_Unindent();
    }

    if (ImGui_CollapsingHeader("Actions", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui_Indent();
        if (!vkrt->state.renderModeFinished) {
            if (seq->active) {
                if (ImGui_Button(ICON_FA_STOP " Stop Sequence")) {
                    sessionQueueRenderStop(session);
                }
            } else if (ImGui_Button(ICON_FA_STOP " Stop Render")) {
                VKRT_stopRenderSampling(vkrt);
            }
        } else {
            if (!seq->active) {
                if (ImGui_Button(ICON_FA_FLOPPY_DISK " Save Image")) {
                    sessionRequestRenderSaveDialog(session);
                }
                ImGui_SameLine();
            }
            if (ImGui_Button(ICON_FA_ARROW_RIGHT_FROM_BRACKET " Exit Render")) {
                sessionQueueRenderStop(session);
            }
        }
        ImGui_Dummy((ImVec2){0.0f, kInspectorControlSpacing});
        ImGui_Unindent();
    }
}

static void drawConfigSection(VKRT* vkrt, bool renderModeActive) {
    if (ImGui_CollapsingHeader("System", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui_Indent();
        ImGui_TextWrapped("Device: %s", vkrt->core.deviceName);
        char driverText[64] = {0};
        formatDriverVersionText(vkrt->core.vendorID, vkrt->core.driverVersion, driverText, sizeof(driverText));
        ImGui_Text("Driver: %s", driverText);
        ImGui_Text("Viewport: %ux%u", vkrt->runtime.displayViewportRect[2], vkrt->runtime.displayViewportRect[3]);
        ImGui_Dummy((ImVec2){0.0f, kInspectorControlSpacing});
        ImGui_Unindent();
    }

    if (!ImGui_CollapsingHeader("Display", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui_Indent();
    ImGui_BeginDisabled(renderModeActive);
    bool vsync = vkrt->runtime.vsync != 0;
    if (ImGui_Checkbox("V-Sync", &vsync)) {
        vkrt->runtime.vsync = vsync ? 1 : 0;
        vkrt->runtime.framebufferResized = VK_TRUE;
    }
    ImGui_EndDisabled();
    ImGui_Dummy((ImVec2){0.0f, kInspectorControlSpacing});
    ImGui_Unindent();
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
            sessionQueueMeshRemoval(session, meshIndex);
            ImGui_PopID();
            continue;
        }

        if (!mesh->ownsGeometry && mesh->geometrySource < meshCount) {
            ImGui_SameLine();
            ImGui_TextDisabled("-> %u", mesh->geometrySource);
            tooltipOnHover("This mesh instance reuses geometry from the shown source mesh index.");
        }

        if (!open) {
            ImGui_PopID();
            continue;
        }

        ImGui_Indent();
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

        bool renderBackfaces = meshInfo->renderBackfaces != 0;
        if (ImGui_Checkbox("Render Backfaces", &renderBackfaces)) {
            VKRT_setMeshRenderBackfaces(vkrt, meshIndex, renderBackfaces ? 1 : 0);
        }
        tooltipOnHover("Disable backface culling for this mesh instance.");

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

        ImGui_Unindent();
        ImGui_PopID();
    }
}

static void drawEditorSection(VKRT* vkrt, Session* session, bool renderModeActive) {
    if (ImGui_CollapsingHeader("Import", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui_Indent();
        ImGui_BeginDisabled(renderModeActive);
        if (ImGui_Button(ICON_FA_FOLDER_PLUS " Import mesh")) {
            sessionRequestMeshImportDialog(session);
        }
        ImGui_EndDisabled();
        ImGui_Dummy((ImVec2){0.0f, kInspectorControlSpacing});
        ImGui_Unindent();
    }

    if (!ImGui_CollapsingHeader("Meshes", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui_Indent();
    ImGui_BeginDisabled(renderModeActive);
    drawMeshInspector(vkrt, session);
    ImGui_EndDisabled();
    ImGui_Dummy((ImVec2){0.0f, kInspectorControlSpacing});
    ImGui_Unindent();
}

static void drawSceneInspectorWindow(VKRT* vkrt, Session* session) {
    bool renderModeActive = vkrt->state.renderModeActive != 0;
    const char* currentTabLabel = "Config";

    ImGuiWindowClass inspectorWindowClass = {0};
    inspectorWindowClass.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoTabBar |
                                                    ImGuiDockNodeFlags_NoWindowMenuButton |
                                                    ImGuiDockNodeFlags_NoCloseButton;
    ImGui_SetNextWindowClass(&inspectorWindowClass);

    ImGui_PushStyleColorImVec4(ImGuiCol_WindowBg, (ImVec4){0.08f, 0.08f, 0.08f, 1.00f});
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_WindowPadding, (ImVec2){0.0f, 8.0f});
    ImGui_Begin("Scene Inspector", NULL, 0);
    ImGui_PopStyleVar();
    ImGui_PopStyleColor();

    static int currentTab = INSPECTOR_TAB_MAIN;
    const VerticalIconTab kTabs[] = {
        { ICON_FA_GEAR, "Config" },
        { ICON_FA_CAMERA_RETRO, "Camera" },
        { ICON_FA_IMAGES, "Render" },
        { ICON_FA_FOLDER_PLUS, "Scene" },
    };
    ImVec2 defaultSpacing = ImGui_GetStyle()->ItemSpacing;
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_ItemSpacing, (ImVec2){0.0f, defaultSpacing.y});
    drawVerticalIconTabs(kTabs, IM_ARRAYSIZE(kTabs), &currentTab);
    syncInspectorDockWidthForTabState(currentTab);
    if (currentTab >= 0) {
        ImGui_SameLine();
        drawInspectorVerticalDivider();
        ImGui_SameLine();
        ImGui_Dummy((ImVec2){10.0f, 0.0f});
        ImGui_SameLine();
    }
    ImGui_PopStyleVar();

    if (currentTab >= 0) {
        float rightMargin = 10.0f;
        float pageWidth = ImGui_GetContentRegionAvail().x - rightMargin;
        if (pageWidth < 1.0f) pageWidth = 1.0f;
        float inspectorScrollbarSize = ImGui_GetStyle()->ScrollbarSize * 0.72f;
        if (inspectorScrollbarSize < 6.0f) inspectorScrollbarSize = 6.0f;

        ImGui_PushStyleVarImVec2(ImGuiStyleVar_WindowPadding, (ImVec2){7.0f, 5.0f});
        ImGui_PushStyleVar(ImGuiStyleVar_ScrollbarSize, inspectorScrollbarSize);
        ImGui_BeginChild("##inspector_page", (ImVec2){pageWidth, 0.0f},
            ImGuiChildFlags_None,
            ImGuiWindowFlags_NoBackground);

        switch ((InspectorTab)currentTab) {
        case INSPECTOR_TAB_MAIN:
            currentTabLabel = "Config";
            break;
        case INSPECTOR_TAB_CAMERA:
            currentTabLabel = "Camera";
            break;
        case INSPECTOR_TAB_RENDER:
            currentTabLabel = "Render";
            break;
        case INSPECTOR_TAB_SCENE:
            currentTabLabel = "Scene";
            break;
        default:
            currentTabLabel = "Config";
            break;
        }
        ImGui_SeparatorText(currentTabLabel);

        ImGui_PushStyleVar(ImGuiStyleVar_IndentSpacing, kInspectorSectionIndent);
        switch ((InspectorTab)currentTab) {
        case INSPECTOR_TAB_MAIN:
            drawConfigSection(vkrt, renderModeActive);
            drawPerformanceSection(vkrt, renderModeActive);
            break;
        case INSPECTOR_TAB_CAMERA:
            drawCameraSection(vkrt, renderModeActive);
            drawCameraEffectsSection(vkrt, renderModeActive);
            break;
        case INSPECTOR_TAB_RENDER:
            drawRenderSection(vkrt, session);
            break;
        case INSPECTOR_TAB_SCENE:
            drawEditorSection(vkrt, session, renderModeActive);
            break;
        default:
            drawConfigSection(vkrt, renderModeActive);
            drawPerformanceSection(vkrt, renderModeActive);
            break;
        }
        ImGui_PopStyleVar();
        ImGui_EndChild();
        ImGui_PopStyleVarEx(2);
    }

    ImGui_End();
}

static void applyEditorCameraInput(VKRT* vkrt, bool viewportHovered) {
    ImGuiIO* io = ImGui_GetIO();
    if (vkrt->state.renderModeActive) {
        if (!viewportHovered) return;

        if (io->MouseWheel != 0.0f) {
            float zoom = vkrt->state.renderViewZoom * powf(kRenderViewWheelStep, io->MouseWheel);
            vkrt->state.renderViewZoom = clampRenderViewValue(zoom, VKRT_RENDER_VIEW_ZOOM_MIN, VKRT_RENDER_VIEW_ZOOM_MAX);
            clampRenderViewPan(vkrt);
        }

        bool panning = ImGui_IsMouseDragging(ImGuiMouseButton_Left, -1.0f) ||
                       ImGui_IsMouseDragging(ImGuiMouseButton_Middle, -1.0f) ||
                       ImGui_IsMouseDragging(ImGuiMouseButton_Right, -1.0f);
        if (panning) {
            float scaleX = io->DisplayFramebufferScale.x > 0.0f ? io->DisplayFramebufferScale.x : 1.0f;
            float scaleY = io->DisplayFramebufferScale.y > 0.0f ? io->DisplayFramebufferScale.y : 1.0f;
            float viewWidth = (float)(vkrt->runtime.displayViewportRect[2] > 0 ? vkrt->runtime.displayViewportRect[2] : 1) / scaleX;
            float viewHeight = (float)(vkrt->runtime.displayViewportRect[3] > 0 ? vkrt->runtime.displayViewportRect[3] : 1) / scaleY;
            float cropWidth = 1.0f;
            float cropHeight = 1.0f;
            queryRenderViewCrop(vkrt, vkrt->state.renderViewZoom, &cropWidth, &cropHeight);

            vkrt->state.renderViewPanX -= (io->MouseDelta.x * cropWidth) / viewWidth;
            vkrt->state.renderViewPanY -= (io->MouseDelta.y * cropHeight) / viewHeight;
            clampRenderViewPan(vkrt);
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
    VKRT_applyCameraInput(vkrt, &cameraInput);
}

void editorUIProcessDialogs(Session* session) {
    if (!session) return;

    if (sessionTakeMeshImportDialogRequest(session)) {
        char* selectedPath = openMeshImportDialog();
        if (selectedPath && selectedPath[0]) {
            sessionQueueMeshImport(session, selectedPath);
        }
        if (selectedPath) NFD_FreePath(selectedPath);
    }

    if (sessionTakeRenderSaveDialogRequest(session)) {
        char* selectedPath = openRenderSaveDialog();
        if (selectedPath && selectedPath[0]) {
            sessionQueueRenderSave(session, selectedPath);
        }
        if (selectedPath) NFD_FreePath(selectedPath);
    }

    if (sessionTakeRenderSequenceFolderDialogRequest(session)) {
        char* selectedPath = openRenderSequenceFolderDialog(session);
        if (selectedPath && selectedPath[0]) {
            sessionSetRenderSequenceFolder(session, selectedPath);
        }
        if (selectedPath) NFD_FreePath(selectedPath);
    }
}

void editorUIInitialize(VKRT* vkrt, void* userData) {
    (void)userData;
    NFD_Init();
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
    NFD_Quit();
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
