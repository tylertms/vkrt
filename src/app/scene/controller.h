#pragma once

#include "session.h"
#include "vkrt.h"

int sceneControllerLoadDefaultScene(VKRT* vkrt, Session* session);
void sceneControllerApplySessionActions(VKRT* vkrt, Session* session);
