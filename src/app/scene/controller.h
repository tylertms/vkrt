#pragma once

#include "session.h"
#include "vkrt.h"

int sceneControllerLoadDefaultScene(VKRT* vkrt, Session* session);
int sceneControllerLoadSceneFromPath(VKRT* vkrt, Session* session, const char* path);
int sceneControllerLoadStartupScene(
    VKRT* vkrt,
    Session* session,
    const char* startupScenePath,
    uint8_t loadDefaultScene
);
void sceneControllerApplySessionActions(VKRT* vkrt, Session* session);
