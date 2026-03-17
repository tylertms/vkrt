#pragma once

#include "vkrt_overlay.h"

typedef struct Session Session;

void editorUIInitializeDialogs(Session* session, GLFWwindow* window);
void editorUIShutdownDialogs(Session* session);
