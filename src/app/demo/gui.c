#include "gui.h"

#include "dcimgui.h"
#include "dcimgui_impl_glfw.h"
#include "dcimgui_impl_vulkan.h"
#include "dcimgui_internal.h"

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
    (void)userData;

    cImGui_ImplGlfw_NewFrame();
    cImGui_ImplVulkan_NewFrame();
    ImGui_NewFrame();

    ImGuiIO* io = ImGui_GetIO();
    VKRT_CameraInput cameraInput = {
        .orbitDx = io->MouseDelta.x,
        .orbitDy = io->MouseDelta.y,
        .panDx = io->MouseDelta.x,
        .panDy = io->MouseDelta.y,
        .scroll = io->MouseWheel,
        .orbiting = ImGui_IsMouseDragging(ImGuiMouseButton_Left, -1.0f),
        .panning = ImGui_IsMouseDragging(ImGuiMouseButton_Right, -1.0f),
        .captureMouse = io->WantCaptureMouse,
    };
    VKRT_applyCameraInput(vkrt, &cameraInput);

    {
        ImGui_PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
        ImGui_PushStyleVar(ImGuiStyleVar_GrabRounding, 8.0f);
        ImGui_PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

        bool open = true;
        ImGui_Begin("Statistics", &open, ImGuiWindowFlags_NoTitleBar);

        ImGui_Text("Device: %s", vkrt->core.deviceName);
        ImGui_Text("Resolution: %dx%d", vkrt->state.camera.width, vkrt->state.camera.height);

        if (ImGui_Checkbox("V-Sync", (bool*)&vkrt->runtime.vsync)) {
            vkrt->runtime.framebufferResized = VK_TRUE;
        }

        ImGui_NewLine();
        ImGui_Text("FPS:                %6d", vkrt->state.framesPerSecond);
        ImGui_Text("Render time:        %6.3f ms", vkrt->state.renderTimeMs);
        ImGui_Text("Frame time:         %6.3f ms", vkrt->state.displayTimeMs);
        ImGui_Text("Average frame time: %6.3f ms", vkrt->state.averageFrametime);
        ImGui_NewLine();

        ImGui_PlotLinesEx("##", vkrt->state.frametimes, COUNT_OF(vkrt->state.frametimes), (int)vkrt->state.frametimeStartIndex, "", 0.0f, 2 * vkrt->state.averageFrametime, (ImVec2){160.0f, 40.0f}, sizeof(float));

        ImGui_PopStyleVarEx(3);
    }

    ImGui_End();
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
