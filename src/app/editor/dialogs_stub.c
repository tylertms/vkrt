#include "editor.h"
#include "debug.h"

void editorUIInitializeDialogs(GLFWwindow* window) {
    (void)window;
    LOG_TRACE("File dialogs not available (built without NFD support)");
}

void editorUIShutdownDialogs(void) {
}

void editorUIProcessDialogs(Session* session) {
    (void)session;
}
