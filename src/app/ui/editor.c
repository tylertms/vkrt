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
static const float kRenderViewWheelStep = 1.12f;
static void tooltipOnHover(const char* text);

static uint32_t absDiffU32(uint32_t a, uint32_t b) {
    return (a > b) ? (a - b) : (b - a);
}

static const char* openMeshImportDialog(void) {
    const char* filters[] = {"*.glb", "*.gltf"};
    return tinyfd_openFileDialog("Import mesh", "assets/models", 2, filters, "glTF 2.0 (.glb/.gltf)", 0);
}

static const char* openRenderSaveDialog(void) {
    const char* filters[] = {"*.png"};
    return tinyfd_saveFileDialog("Save Image", "captures/render.png", 1, filters, "PNG image");
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
    ImGui_DockBuilderDockWindow("Viewport###ViewWindow", viewportDockID);
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
    const char* viewportWindowLabel = vkrt->state.renderModeActive
        ? "Render###ViewWindow"
        : "Viewport###ViewWindow";
    ImGui_Begin(viewportWindowLabel, NULL, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
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
    ImGui_Separator();
    ImGui_Text("FPS:          %8u", vkrt->state.framesPerSecond);
    ImGui_Text("Frames:       %8u", vkrt->state.accumulationFrame);
    ImGui_Text("Samples:  %12llu", (unsigned long long)vkrt->state.totalSamples);
    ImGui_Text("Samples / px: %8u", vkrt->state.samplesPerPixel);
    ImGui_Text("Frame (ms):   %8.3f ms", vkrt->state.displayFrameTimeMs);
    ImGui_Text("Render (ms):  %8.3f ms", vkrt->state.displayRenderTimeMs);

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

    int maxBounces = (int)vkrt->state.maxBounces;
    if (ImGui_SliderIntEx("Max Bounces", &maxBounces, 1, 64, "%d", ImGuiSliderFlags_AlwaysClamp)) {
        vkrt->state.maxBounces = (uint32_t)maxBounces;
        VKRT_invalidateAccumulation(vkrt);
    }

    bool timeMaxEnabled = vkrt->state.timeBase >= 0.0f;
    if (ImGui_Checkbox("Time Max", &timeMaxEnabled)) {
        if (timeMaxEnabled) {
            VKRT_setTimeRange(vkrt, 0.0f, 0.5f);
        } else {
            VKRT_setTimeRange(vkrt, -1.0f, vkrt->state.timeStep);
        }
    }
    tooltipOnHover("Enable a fixed upper time bound for temporal evaluation.");
    if (timeMaxEnabled) {
        float timeMax = vkrt->state.timeStep;
        if (ImGui_DragFloatEx("Time Max Value", &timeMax, 0.01f, 0.001f, 1000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp)) {
            VKRT_setTimeRange(vkrt, 0.0f, timeMax);
        }
        tooltipOnHover("Upper bound value used when Time Max is enabled.");
    }

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
}

static uint32_t clampRenderDimension(int value) {
    if (value < 1) return 1;
    if (value > 16384) return 16384;
    return (uint32_t)value;
}

static float clampRenderViewValue(float value, float minValue, float maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

static void tooltipOnHover(const char* text) {
    if (!text) return;
    if (ImGui_IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
        ImGui_SetTooltip("%s", text);
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
}

static void drawRenderSection(VKRT* vkrt, Session* session) {
    initializeRenderConfig(session);
    ImGui_Separator();
    ImGui_Text("Render");

    if (!vkrt->state.renderModeActive) {
        int outputWidth = (int)session->renderConfig.width;
        int outputHeight = (int)session->renderConfig.height;
        int targetSamples = (int)session->renderConfig.targetSamples;

        const float inputWidth = 140.0f;

        ImGui_SetNextItemWidth(inputWidth);
        if (ImGui_DragIntEx("Width", &outputWidth, 1.0f, 1, 16384, "%d", ImGuiSliderFlags_AlwaysClamp)) {
            session->renderConfig.width = clampRenderDimension(outputWidth);
        }

        ImGui_SetNextItemWidth(inputWidth);
        if (ImGui_DragIntEx("Height", &outputHeight, 1.0f, 1, 16384, "%d", ImGuiSliderFlags_AlwaysClamp)) {
            session->renderConfig.height = clampRenderDimension(outputHeight);
        }

        ImGui_SetNextItemWidth(inputWidth);
        if (ImGui_DragIntEx("Samples", &targetSamples, 1.0f, 0, INT_MAX, "%d", ImGuiSliderFlags_AlwaysClamp)) {
            if (targetSamples < 0) targetSamples = 0;
            session->renderConfig.targetSamples = (uint32_t)targetSamples;
        }
        tooltipOnHover("Total samples to render. Set to 0 for manual stop.");

        if (ImGui_Button("Start Render")) {
            sessionQueueRenderStart(session,
                session->renderConfig.width,
                session->renderConfig.height,
                session->renderConfig.targetSamples);
        }
        return;
    }

    ImGui_Text("Output: %ux%u", vkrt->runtime.renderExtent.width, vkrt->runtime.renderExtent.height);
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

    if (!vkrt->state.renderModeFinished) {
        if (ImGui_Button("Stop Render")) {
            VKRT_stopRenderSampling(vkrt);
        }
    } else {
        if (ImGui_Button("Save Image")) {
            sessionRequestRenderSaveDialog(session);
        }
        ImGui_SameLine();
        if (ImGui_Button("Exit Render")) {
            sessionQueueRenderStop(session);
        }
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
    bool renderModeActive = vkrt->state.renderModeActive != 0;

    ImGui_PushStyleColorImVec4(ImGuiCol_WindowBg, (ImVec4){0.08f, 0.08f, 0.08f, 1.00f});
    ImGui_Begin("Scene Inspector", NULL, 0);
    ImGui_PopStyleColor();

    ImGui_Text("Device: %s", vkrt->core.deviceName);
    ImGui_Text("Viewport: %ux%u", vkrt->runtime.displayViewportRect[2], vkrt->runtime.displayViewportRect[3]);

    ImGui_BeginDisabled(renderModeActive);
    bool vsync = vkrt->runtime.vsync != 0;
    if (ImGui_Checkbox("V-Sync", &vsync)) {
        vkrt->runtime.vsync = vsync ? 1 : 0;
        vkrt->runtime.framebufferResized = VK_TRUE;
    }
    ImGui_EndDisabled();

    if (!renderModeActive) {
        if (ImGui_Button(ICON_FA_CAMERA " Save Screenshot")) {
            sessionRequestRenderSaveDialog(session);
        }
    }

    drawRenderSection(vkrt, session);
    drawPerformanceSection(vkrt, renderModeActive);
    ImGui_Separator();

    ImGui_BeginDisabled(renderModeActive);
    if (ImGui_Button("Import mesh")) {
        sessionRequestMeshImportDialog(session);
    }

    drawMeshInspector(vkrt, session);
    ImGui_EndDisabled();
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
        const char* selectedPath = openMeshImportDialog();
        if (selectedPath && selectedPath[0]) {
            sessionQueueMeshImport(session, selectedPath);
        }
    }

    if (sessionTakeRenderSaveDialogRequest(session)) {
        const char* selectedPath = openRenderSaveDialog();
        if (selectedPath && selectedPath[0]) {
            sessionQueueRenderSave(session, selectedPath);
        }
    }
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
