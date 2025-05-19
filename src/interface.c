#include "interface.h"
#include "device.h"

#include "dcimgui.h"
#include "dcimgui_impl_glfw.h"
#include "dcimgui_impl_vulkan.h"
#include "dcimgui_internal.h"

void setupImGui(VKRT* vkrt) {
    vkrt->imguiContext = ImGui_CreateContext(NULL);

    float width = (float)vkrt->swapChainExtent.width;
    float height = (float)vkrt->swapChainExtent.height;

    ImGuiIO* io = ImGui_GetIO();
    io->DisplaySize = (ImVec2){width, height};
    io->DisplayFramebufferScale = (ImVec2){1.0f, 1.0f};
    io->ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    setDarkTheme();

    ImGuiStyle* style = ImGui_GetStyle();
    style->WindowRounding = 2;
    style->GrabRounding = 2;
    style->FrameRounding = 2;
    style->FrameBorderSize = 0;
    style->WindowBorderSize = 0;
    style->DockingSeparatorSize = 1;
    style->WindowPadding = (ImVec2){8, 5};

    cImGui_ImplGlfw_InitForVulkan(vkrt->window, true);
    ImGui_ImplVulkan_InitInfo imGuiVulkanInitInfo = {0};
    imGuiVulkanInitInfo.Instance = vkrt->instance;
    imGuiVulkanInitInfo.PhysicalDevice = vkrt->physicalDevice;
    imGuiVulkanInitInfo.Device = vkrt->device;
    imGuiVulkanInitInfo.Queue = vkrt->graphicsQueue;
    imGuiVulkanInitInfo.QueueFamily = findQueueFamilies(vkrt).graphics;
    imGuiVulkanInitInfo.PipelineCache = VK_NULL_HANDLE;
    imGuiVulkanInitInfo.DescriptorPool = vkrt->descriptorPool;
    imGuiVulkanInitInfo.Allocator = VK_NULL_HANDLE;
    imGuiVulkanInitInfo.MinImageCount = vkrt->swapChainImageCount - 1;
    imGuiVulkanInitInfo.ImageCount = vkrt->swapChainImageCount;
    imGuiVulkanInitInfo.CheckVkResultFn = VK_NULL_HANDLE;
    imGuiVulkanInitInfo.RenderPass = vkrt->renderPass;

    cImGui_ImplVulkan_Init(&imGuiVulkanInitInfo);
    cImGui_ImplVulkan_CreateFontsTexture();
}

void deinitImGui(VKRT* vkrt) {
    cImGui_ImplVulkan_Shutdown();
    cImGui_ImplGlfw_Shutdown();

    ImGui_DestroyContext(vkrt->imguiContext);
}

void drawInterface(VKRT* vkrt) {
    cImGui_ImplVulkan_NewFrame();
    ImGui_NewFrame();

    bool open = true;
    ImGui_Begin("Statistics", &open, ImGuiWindowFlags_NoTitleBar);

    ImGui_Text("Frame rate:%10d FPS", vkrt->averageFPS);
    ImGui_Text("Frame time:%10.3f ms", vkrt->averageFrametime);

    ImGui_End();

    ImGui_Render();
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
    colors[ImGuiCol_WindowBg] = almostBlack;
    colors[ImGuiCol_ChildBg] = darkGray;
    colors[ImGuiCol_PopupBg] = almostBlack;
    colors[ImGuiCol_Border] = midGray;
    colors[ImGuiCol_FrameBg] = midGray;
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