#include "editor.h"

#include "nfd.h"

static char* openMeshImportDialog(void) {
    nfdchar_t* outPath = NULL;
    nfdfilteritem_t filters[] = {{"glTF 2.0", "glb,gltf"}};
    if (NFD_OpenDialog(&outPath, filters, 1, "assets/models") != NFD_OKAY) return NULL;
    return outPath;
}

static char* openRenderSaveDialog(void) {
    nfdchar_t* outPath = NULL;
    nfdfilteritem_t filters[] = {{"PNG image", "png"}};
    if (NFD_SaveDialog(&outPath, filters, 1, "captures", "render.png") != NFD_OKAY) return NULL;
    return outPath;
}

static char* openRenderSequenceFolderDialog(const Session* session) {
    const char* defaultPath = sessionGetRenderSequenceFolder(session);
    if (!defaultPath || !defaultPath[0]) defaultPath = "captures/sequence";
    nfdchar_t* outPath = NULL;
    if (NFD_PickFolder(&outPath, defaultPath) != NFD_OKAY) return NULL;
    return outPath;
}

void editorUIProcessDialogs(Session* session) {
    if (!session) return;

    if (sessionTakeMeshImportDialogRequest(session)) {
        char* selectedPath = openMeshImportDialog();
        if (selectedPath && selectedPath[0]) {
            sessionQueueMeshImport(session, selectedPath);
        }
        if (selectedPath) NFD_FreePath(selectedPath);
    }

    if (sessionTakeRenderSaveDialogRequest(session)) {
        char* selectedPath = openRenderSaveDialog();
        if (selectedPath && selectedPath[0]) {
            sessionQueueRenderSave(session, selectedPath);
        }
        if (selectedPath) NFD_FreePath(selectedPath);
    }

    if (sessionTakeRenderSequenceFolderDialogRequest(session)) {
        char* selectedPath = openRenderSequenceFolderDialog(session);
        if (selectedPath && selectedPath[0]) {
            sessionSetRenderSequenceFolder(session, selectedPath);
        }
        if (selectedPath) NFD_FreePath(selectedPath);
    }
}
