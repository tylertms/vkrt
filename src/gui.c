#include "gui.h"

// Called after all of Vulkan has been initialized
// ImGUI must be initialized here, some defaults
// are provided, such as setDefaultStyle
void initGUI(VKRT* vkrt) {
    vkrt->imguiContext = ImGui_CreateContext(NULL);

    ImGuiIO* io = ImGui_GetIO();
    io->ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // A default style with generally rounded corners
    // and a dark color scheme.
    VKRT_setDefaultStyle();

    cImGui_ImplGlfw_InitForVulkan(vkrt->window, true);

    ImGui_ImplVulkan_InitInfo imGuiVulkanInitInfo = {0};
    VKRT_getImGuiVulkanInitInfo(vkrt, &imGuiVulkanInitInfo);
    cImGui_ImplVulkan_Init(&imGuiVulkanInitInfo);

    cImGui_ImplVulkan_CreateFontsTexture();
}

// Called at the beginning of Vulkan shutdown after vkDeviceWaitIdle
void deinitGUI(VKRT* vkrt) {
    cImGui_ImplVulkan_Shutdown();
    cImGui_ImplGlfw_Shutdown();
    ImGui_DestroyContext(vkrt->imguiContext);
}

// Called on each frame after the ray tracing pipeline
// has been dispatched. Renders overtop on a secondary
// render pass.
void drawGUI(VKRT* vkrt) {
    cImGui_ImplGlfw_NewFrame();
    cImGui_ImplVulkan_NewFrame();
    ImGui_NewFrame();

    // Automatically updates VKRT's camera system
    // for orbit/pan/zoom. Position, target, FOV, etc.
    // are stored on the host device memory, then the
    // inverse view and projection matrices are passed
    // to the raygen shader.
    VKRT_pollCameraMovement(vkrt);

    // Create an example Statistics window
    // The VKRT struct holds several auto-updating statistics,
    // including FPS, render time (GPU), display time (total), etc.
    {
        ImGui_PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
        ImGui_PushStyleVar(ImGuiStyleVar_GrabRounding, 8.0f);
        ImGui_PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

        bool open = true;
        ImGui_Begin("Statistics", &open, ImGuiWindowFlags_NoTitleBar);

        ImGui_Text("Device: %s", vkrt->deviceName);
        ImGui_Text("Resolution: %dx%d", vkrt->camera.width, vkrt->camera.height);

        if (ImGui_Checkbox("V-Sync", (bool*)&vkrt->vsync)) {
            vkrt->framebufferResized = VK_TRUE;
        }

        ImGui_NewLine();
        ImGui_Text("FPS:                %6d", vkrt->framesPerSecond);
        ImGui_Text("Render time:        %6.3f ms", vkrt->renderTimeMs);
        ImGui_Text("Frame time:         %6.3f ms", vkrt->displayTimeMs);
        ImGui_Text("Average frame time: %6.3f ms", vkrt->averageFrametime);
        ImGui_NewLine();

        ImGui_PlotLinesEx("##", vkrt->frametimes, COUNT_OF(vkrt->frametimes), (int)vkrt->frametimeStartIndex, "", 0.0f, 2 * vkrt->averageFrametime, (ImVec2){160.0f, 40.0f}, sizeof(float));

        ImGui_PopStyleVarEx(3);
    }

    ImGui_End();
    ImGui_Render();
}