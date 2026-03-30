#pragma once

#include "session.h"
#include "vkrt.h"

void renderControllerApplySessionActions(VKRT* vkrt, Session* session);
int renderControllerRunInteractiveLoop(VKRT* vkrt, Session* session);
