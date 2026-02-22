#include "session.h"
#include "editor.h"
#include "controller.h"
#include "vkrt.h"
#include "debug.h"

#include <stdio.h>
#include <stdlib.h>

int main(void) {
    VKRT vkrt = {0};
    Session session = {0};
    sessionInit(&session);

    VKRT_AppHooks hooks = {
        .init = editorUIInitialize,
        .deinit = editorUIShutdown,
        .drawOverlay = editorUIDraw,
        .userData = &session,
    };
    VKRT_registerAppHooks(&vkrt, hooks);

    VKRT_CreateInfo createInfo = {0};
    VKRT_defaultCreateInfo(&createInfo);
    createInfo.title = "vkspt";
    createInfo.shaders.rgenPath = "./shaders/rgen.spv";
    createInfo.shaders.rmissPath = "./shaders/rmiss.spv";
    createInfo.shaders.rchitPath = "./shaders/rchit.spv";

    if (VKRT_initWithCreateInfo(&vkrt, &createInfo) != VK_SUCCESS) {
        LOG_ERROR("Failed to initialize VKRT runtime");
        VKRT_deinit(&vkrt);
        sessionDeinit(&session);
        return EXIT_FAILURE;
    }

    meshControllerLoadDefaultAssets(&vkrt, &session);

    while (!VKRT_shouldDeinit(&vkrt)) {
        VKRT_poll(&vkrt);
        meshControllerApplyPendingActions(&vkrt, &session);
        VKRT_draw(&vkrt);
    }

    VKRT_deinit(&vkrt);
    sessionDeinit(&session);
    return EXIT_SUCCESS;
}
