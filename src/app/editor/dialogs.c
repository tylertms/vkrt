#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#else
#define GLFW_EXPOSE_NATIVE_X11
#define GLFW_EXPOSE_NATIVE_WAYLAND
#endif

#include "editor.h"

#include "debug.h"
#include "io.h"
#include "nfd.h"
#include "nfd_glfw3.h"

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef enum DialogKind {
    DIALOG_KIND_NONE = 0,
    DIALOG_KIND_IMPORT_MESH,
    DIALOG_KIND_SAVE_RENDER,
    DIALOG_KIND_PICK_SEQUENCE_FOLDER,
} DialogKind;

typedef struct DialogRequest {
    DialogKind kind;
    char defaultPath[VKRT_PATH_MAX];
    char defaultName[256];
    nfdwindowhandle_t parentWindow;
} DialogRequest;

typedef struct DialogState {
    GLFWwindow* window;
    VKRT_Mutex mutex;
    VKRT_Cond condition;
    uint8_t syncPrimitivesInitialized;
    VKRT_Thread worker;
    uint8_t workerStarted;
    uint8_t workerAvailable;
    uint8_t workerStartupComplete;
    uint8_t mainThreadAvailable;
    uint8_t running;
    uint8_t requestPending;
    uint8_t requestActive;
    uint8_t responsePending;
    DialogRequest request;
    DialogKind responseKind;
    char* responsePath;
} DialogState;

static DialogState gDialogState = {0};

static int prepareDialogRequest(DialogKind kind, const char* defaultPath, const char* defaultName, DialogRequest* outRequest) {
    if (!outRequest) return 0;

    DialogRequest request = {0};
    request.kind = kind;
    if (defaultPath && defaultPath[0]) {
        if (snprintf(request.defaultPath, sizeof(request.defaultPath), "%s", defaultPath) >= (int)sizeof(request.defaultPath)) {
            return 0;
        }
    }
    if (defaultName && defaultName[0]) {
        if (snprintf(request.defaultName, sizeof(request.defaultName), "%s", defaultName) >= (int)sizeof(request.defaultName)) {
            return 0;
        }
    }

    if (gDialogState.window) {
        NFD_GetNativeWindowFromGLFWWindow(gDialogState.window, &request.parentWindow);
    }

    *outRequest = request;
    return 1;
}

static char* runDialogRequest(const DialogRequest* request) {
    if (!request) return NULL;

    const char* defaultPath = request->defaultPath[0] ? request->defaultPath : NULL;
    nfdu8char_t* selectedPath = NULL;
    nfdresult_t dialogResult = NFD_CANCEL;

    switch (request->kind) {
        case DIALOG_KIND_IMPORT_MESH: {
            nfdu8filteritem_t filters[] = {{"glTF 2.0", "glb,gltf"}};
            nfdopendialogu8args_t args = {
                .filterList = filters,
                .filterCount = 1,
                .defaultPath = defaultPath,
                .parentWindow = request->parentWindow,
            };
            dialogResult = NFD_OpenDialogU8_With(&selectedPath, &args);
            break;
        }
        case DIALOG_KIND_SAVE_RENDER: {
            nfdu8filteritem_t filters[] = {{"PNG image", "png"}};
            nfdsavedialogu8args_t args = {
                .filterList = filters,
                .filterCount = 1,
                .defaultPath = defaultPath,
                .defaultName = request->defaultName[0] ? request->defaultName : "render.png",
                .parentWindow = request->parentWindow,
            };
            dialogResult = NFD_SaveDialogU8_With(&selectedPath, &args);
            break;
        }
        case DIALOG_KIND_PICK_SEQUENCE_FOLDER: {
            nfdpickfolderu8args_t args = {
                .defaultPath = defaultPath,
                .parentWindow = request->parentWindow,
            };
            dialogResult = NFD_PickFolderU8_With(&selectedPath, &args);
            break;
        }
        default:
            return NULL;
    }

    if (dialogResult == NFD_CANCEL) {
        return NULL;
    }

    if (dialogResult != NFD_OKAY) {
        const char* errorMessage = NFD_GetError();
        if (!errorMessage || !errorMessage[0]) errorMessage = "unknown error";
        LOG_ERROR("File dialog failed (%d): %s", (int)request->kind, errorMessage);
        return NULL;
    }

    char* copiedPath = stringDuplicate(selectedPath);
    NFD_FreePathU8(selectedPath);
    return copiedPath;
}

static int dialogWorkerMain(void* userData) {
    (void)userData;

    nfdresult_t initResult = NFD_Init();
    if (initResult == NFD_OKAY) {
        if (!NFD_SetDisplayPropertiesFromGLFW()) {
            LOG_TRACE("File dialog display properties were not updated from GLFW");
        }
    }

    vkrtMutexLock(&gDialogState.mutex);
    gDialogState.workerAvailable = initResult == NFD_OKAY ? 1u : 0u;
    gDialogState.workerStartupComplete = 1u;
    vkrtCondBroadcast(&gDialogState.condition);
    vkrtMutexUnlock(&gDialogState.mutex);

    if (initResult != NFD_OKAY) {
        const char* errorMessage = NFD_GetError();
        if (!errorMessage || !errorMessage[0]) errorMessage = "unknown error";
        LOG_ERROR("Failed to initialize file dialog worker: %s", errorMessage);
        return 0;
    }

    for (;;) {
        DialogRequest request = {0};

        vkrtMutexLock(&gDialogState.mutex);
        while (gDialogState.running && !gDialogState.requestPending) {
            vkrtCondWait(&gDialogState.condition, &gDialogState.mutex);
        }

        if (!gDialogState.running) {
            vkrtMutexUnlock(&gDialogState.mutex);
            break;
        }

        request = gDialogState.request;
        gDialogState.requestPending = 0u;
        gDialogState.requestActive = 1u;
        vkrtMutexUnlock(&gDialogState.mutex);

        char* selectedPath = runDialogRequest(&request);

        vkrtMutexLock(&gDialogState.mutex);
        gDialogState.responseKind = request.kind;
        gDialogState.responsePath = selectedPath;
        gDialogState.responsePending = 1u;
        gDialogState.requestActive = 0u;
        vkrtCondBroadcast(&gDialogState.condition);
        vkrtMutexUnlock(&gDialogState.mutex);
    }

    NFD_Quit();
    return 0;
}

static void shutdownDialogWorker(void) {
    if (!gDialogState.workerStarted) return;

    vkrtMutexLock(&gDialogState.mutex);
    gDialogState.running = 0u;
    vkrtCondBroadcast(&gDialogState.condition);
    vkrtMutexUnlock(&gDialogState.mutex);

    vkrtThreadJoin(gDialogState.worker, NULL);
    gDialogState.workerStarted = 0u;
    gDialogState.workerAvailable = 0u;
}

void editorUIInitializeDialogs(GLFWwindow* window) {
    gDialogState.window = window;

    int mutexResult = vkrtMutexInit(&gDialogState.mutex, VKRT_MUTEX_PLAIN);
    int conditionResult = mutexResult == VKRT_THREAD_SUCCESS
        ? vkrtCondInit(&gDialogState.condition)
        : VKRT_THREAD_ERROR;
    if (mutexResult != VKRT_THREAD_SUCCESS || conditionResult != VKRT_THREAD_SUCCESS) {
        if (mutexResult == VKRT_THREAD_SUCCESS) {
            vkrtMutexDestroy(&gDialogState.mutex);
        }
        gDialogState.mainThreadAvailable = NFD_Init() == NFD_OKAY ? 1u : 0u;
        if (gDialogState.mainThreadAvailable) {
            NFD_SetDisplayPropertiesFromGLFW();
        }
        return;
    }

    gDialogState.syncPrimitivesInitialized = 1u;
    gDialogState.running = 1u;
    if (vkrtThreadCreate(&gDialogState.worker, dialogWorkerMain, NULL) == VKRT_THREAD_SUCCESS) {
        gDialogState.workerStarted = 1u;
        vkrtMutexLock(&gDialogState.mutex);
        while (!gDialogState.workerStartupComplete) {
            vkrtCondWait(&gDialogState.condition, &gDialogState.mutex);
        }
        uint8_t workerAvailable = gDialogState.workerAvailable;
        vkrtMutexUnlock(&gDialogState.mutex);

        if (workerAvailable) {
            return;
        }

        shutdownDialogWorker();
    }

    gDialogState.mainThreadAvailable = NFD_Init() == NFD_OKAY ? 1u : 0u;
    if (gDialogState.mainThreadAvailable) {
        NFD_SetDisplayPropertiesFromGLFW();
    } else {
        LOG_ERROR("Failed to initialize file dialogs on the main thread");
    }
}

void editorUIShutdownDialogs(void) {
    shutdownDialogWorker();

    if (gDialogState.mainThreadAvailable) {
        NFD_Quit();
        gDialogState.mainThreadAvailable = 0u;
    }

    if (gDialogState.responsePath) {
        free(gDialogState.responsePath);
        gDialogState.responsePath = NULL;
    }

    if (gDialogState.syncPrimitivesInitialized) {
        vkrtCondDestroy(&gDialogState.condition);
        vkrtMutexDestroy(&gDialogState.mutex);
    }

    memset(&gDialogState, 0, sizeof(gDialogState));
}

static void applyDialogResponse(Session* session, DialogKind kind, char* selectedPath) {
    if (!session) {
        free(selectedPath);
        return;
    }

    if (selectedPath && selectedPath[0]) {
        switch (kind) {
            case DIALOG_KIND_IMPORT_MESH:
                sessionQueueMeshImport(session, selectedPath);
                break;
            case DIALOG_KIND_SAVE_RENDER:
                sessionQueueRenderSave(session, selectedPath);
                break;
            case DIALOG_KIND_PICK_SEQUENCE_FOLDER:
                sessionSetRenderSequenceFolder(session, selectedPath);
                break;
            default:
                break;
        }
    }

    free(selectedPath);
}

static void drainDialogResponse(Session* session) {
    if (!gDialogState.workerStarted) return;

    vkrtMutexLock(&gDialogState.mutex);
    if (!gDialogState.responsePending) {
        vkrtMutexUnlock(&gDialogState.mutex);
        return;
    }

    DialogKind kind = gDialogState.responseKind;
    char* selectedPath = gDialogState.responsePath;
    gDialogState.responseKind = DIALOG_KIND_NONE;
    gDialogState.responsePath = NULL;
    gDialogState.responsePending = 0u;
    vkrtMutexUnlock(&gDialogState.mutex);

    applyDialogResponse(session, kind, selectedPath);
}

static int queueDialogRequest(const DialogRequest* request) {
    if (!request || !gDialogState.workerStarted) return 0;

    vkrtMutexLock(&gDialogState.mutex);
    if (!gDialogState.workerAvailable ||
        gDialogState.requestPending ||
        gDialogState.requestActive ||
        gDialogState.responsePending) {
        vkrtMutexUnlock(&gDialogState.mutex);
        return 0;
    }

    gDialogState.request = *request;
    gDialogState.requestPending = 1u;
    vkrtCondSignal(&gDialogState.condition);
    vkrtMutexUnlock(&gDialogState.mutex);
    return 1;
}

static void executeDialogSynchronously(Session* session, const DialogRequest* request) {
    if (!gDialogState.mainThreadAvailable || !request) return;
    char* selectedPath = runDialogRequest(request);
    applyDialogResponse(session, request->kind, selectedPath);
}

static int tryScheduleImportMeshDialog(Session* session) {
    if (!sessionTakeMeshImportDialogRequest(session)) return 0;

    char defaultPath[VKRT_PATH_MAX] = {0};
    resolveExistingParentPath("assets/models", NULL, defaultPath, sizeof(defaultPath));

    DialogRequest request = {0};
    if (!prepareDialogRequest(DIALOG_KIND_IMPORT_MESH, defaultPath, NULL, &request)) {
        return 1;
    }

    if (queueDialogRequest(&request)) return 1;
    executeDialogSynchronously(session, &request);
    return 1;
}

static int tryScheduleRenderSaveDialog(Session* session) {
    if (!sessionTakeRenderSaveDialogRequest(session)) return 0;

    char defaultPath[VKRT_PATH_MAX] = {0};
    resolveExistingParentPath("captures", NULL, defaultPath, sizeof(defaultPath));

    DialogRequest request = {0};
    if (!prepareDialogRequest(DIALOG_KIND_SAVE_RENDER, defaultPath, "render.png", &request)) {
        return 1;
    }

    if (queueDialogRequest(&request)) return 1;
    executeDialogSynchronously(session, &request);
    return 1;
}

static int tryScheduleSequenceFolderDialog(Session* session) {
    if (!sessionTakeRenderSequenceFolderDialogRequest(session)) return 0;

    char defaultPath[VKRT_PATH_MAX] = {0};
    resolveExistingParentPath(sessionGetRenderSequenceFolder(session), "captures", defaultPath, sizeof(defaultPath));

    DialogRequest request = {0};
    if (!prepareDialogRequest(DIALOG_KIND_PICK_SEQUENCE_FOLDER, defaultPath, NULL, &request)) {
        return 1;
    }

    if (queueDialogRequest(&request)) return 1;
    executeDialogSynchronously(session, &request);
    return 1;
}

void editorUIProcessDialogs(Session* session) {
    if (!session) return;

    drainDialogResponse(session);

    if (gDialogState.workerStarted) {
        vkrtMutexLock(&gDialogState.mutex);
        uint8_t workerBusy = gDialogState.requestPending || gDialogState.requestActive || gDialogState.responsePending;
        vkrtMutexUnlock(&gDialogState.mutex);
        if (workerBusy) return;
    }

    if (tryScheduleImportMeshDialog(session)) return;
    if (tryScheduleRenderSaveDialog(session)) return;
    (void)tryScheduleSequenceFolderDialog(session);
}
