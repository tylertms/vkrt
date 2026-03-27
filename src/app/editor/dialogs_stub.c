#include "GLFW/glfw3.h"
#include "debug.h"
#include "editor.h"
#include "editor_internal.h"
#include "session.h"

void editorUIInitializeDialogs(Session* session, GLFWwindow* window) {
    (void)session;
    (void)window;
    LOG_TRACE("File dialogs not available (built without NFD support)");
}

void editorUIShutdownDialogs(Session* session) {
    (void)session;
}

void editorUIProcessDialogs(Session* session) {
    (void)session;
}
