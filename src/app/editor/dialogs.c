#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#else
#define GLFW_EXPOSE_NATIVE_X11
#define GLFW_EXPOSE_NATIVE_WAYLAND
#endif

#include "editor_internal.h"
#include "session.h"

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
    DIALOG_KIND_IMPORT_TEXTURE,
    DIALOG_KIND_IMPORT_ENVIRONMENT,
    DIALOG_KIND_SAVE_RENDER,
    DIALOG_KIND_PICK_SEQUENCE_FOLDER,
} DialogKind;

enum { kDialogDefaultNameCapacity = 256 };

typedef struct DialogRequest {
    DialogKind kind;
    uint32_t materialIndex;
    uint32_t textureSlot;
    char defaultPath[VKRT_PATH_MAX];
    char defaultName[kDialogDefaultNameCapacity];
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
    DialogRequest response;
    char* responsePath;
} DialogState;

static DialogState* getDialogState(Session* session) {
    if (!session) return NULL;
    return session->editor.dialogState;
}

static const char* queryDialogKindLabel(DialogKind kind) {
    switch (kind) {
        case DIALOG_KIND_IMPORT_MESH:           return "mesh import";
        case DIALOG_KIND_IMPORT_TEXTURE:        return "texture import";
        case DIALOG_KIND_IMPORT_ENVIRONMENT:    return "environment import";
        case DIALOG_KIND_SAVE_RENDER:           return "render save";
        case DIALOG_KIND_PICK_SEQUENCE_FOLDER:  return "sequence folder";
        default: return "unknown";
    }
}

static int dialogBackendAvailable(const DialogState* state) {
    if (!state) return 0;
    return state->workerAvailable || state->mainThreadAvailable;
}

static int prepareDialogRequest(
    DialogState* state,
    DialogKind kind,
    const char* defaultPath,
    const char* defaultName,
    DialogRequest* outRequest
) {
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

    if (state && state->window) {
        NFD_GetNativeWindowFromGLFWWindow(state->window, &request.parentWindow);
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
            nfdu8filteritem_t filters[] = {{"glTF mesh", "glb,gltf"}};
            nfdopendialogu8args_t args = {
                .filterList = filters,
                .filterCount = 1,
                .defaultPath = defaultPath,
                .parentWindow = request->parentWindow,
            };
            dialogResult = NFD_OpenDialogU8_With(&selectedPath, &args);
            break;
        }
        case DIALOG_KIND_IMPORT_TEXTURE: {
            nfdu8filteritem_t filters[] = {{"Image", "png,jpg,jpeg,bmp,tga,tif,tiff"}};
            nfdopendialogu8args_t args = {
                .filterList = filters,
                .filterCount = 1,
                .defaultPath = defaultPath,
                .parentWindow = request->parentWindow,
            };
            dialogResult = NFD_OpenDialogU8_With(&selectedPath, &args);
            break;
        }
        case DIALOG_KIND_IMPORT_ENVIRONMENT: {
            nfdu8filteritem_t filters[] = {{"Image", "exr,hdr,pic,png,jpg,jpeg,bmp,tga,tif,tiff"}};
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
            nfdu8filteritem_t filters[] = {
                {"PNG image", "png"},
                {"JPEG image", "jpg,jpeg"},
                {"BMP image", "bmp"},
                {"TGA image", "tga"},
            };
            nfdsavedialogu8args_t args = {
                .filterList = filters,
                .filterCount = 4,
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
    DialogState* state = (DialogState*)userData;
    if (!state) return 0;

    nfdresult_t initResult = NFD_Init();
    if (initResult == NFD_OKAY) {
        if (!NFD_SetDisplayPropertiesFromGLFW()) {
            LOG_TRACE("File dialog display properties were not updated from GLFW");
        }
    }

    vkrtMutexLock(&state->mutex);
    state->workerAvailable = initResult == NFD_OKAY ? 1u : 0u;
    state->workerStartupComplete = 1u;
    vkrtCondBroadcast(&state->condition);
    vkrtMutexUnlock(&state->mutex);

    if (initResult != NFD_OKAY) {
        const char* errorMessage = NFD_GetError();
        if (!errorMessage || !errorMessage[0]) errorMessage = "unknown error";
        LOG_ERROR("Failed to initialize file dialog worker: %s", errorMessage);
        return 0;
    }

    for (;;) {
        DialogRequest request = {0};

        vkrtMutexLock(&state->mutex);
        while (state->running && !state->requestPending) {
            vkrtCondWait(&state->condition, &state->mutex);
        }

        if (!state->running) {
            vkrtMutexUnlock(&state->mutex);
            break;
        }

        request = state->request;
        state->requestPending = 0u;
        state->requestActive = 1u;
        vkrtMutexUnlock(&state->mutex);

        char* selectedPath = runDialogRequest(&request);

        vkrtMutexLock(&state->mutex);
        state->response = request;
        state->responsePath = selectedPath;
        state->responsePending = 1u;
        state->requestActive = 0u;
        vkrtCondBroadcast(&state->condition);
        vkrtMutexUnlock(&state->mutex);
    }

    NFD_Quit();
    return 0;
}

static void shutdownDialogWorker(DialogState* state) {
    if (!state || !state->workerStarted) return;

    vkrtMutexLock(&state->mutex);
    state->running = 0u;
    vkrtCondBroadcast(&state->condition);
    vkrtMutexUnlock(&state->mutex);

    vkrtThreadJoin(state->worker, NULL);
    state->workerStarted = 0u;
    state->workerAvailable = 0u;
}

void editorUIInitializeDialogs(Session* session, GLFWwindow* window) {
    if (!session || session->editor.dialogState) return;

    DialogState* state = (DialogState*)calloc(1, sizeof(*state));
    if (!state) {
        LOG_ERROR("Failed to allocate dialog state");
        return;
    }

    state->window = window;

    int mutexResult = vkrtMutexInit(&state->mutex, VKRT_MUTEX_PLAIN);
    int conditionResult = mutexResult == VKRT_THREAD_SUCCESS
        ? vkrtCondInit(&state->condition)
        : VKRT_THREAD_ERROR;
    if (mutexResult != VKRT_THREAD_SUCCESS || conditionResult != VKRT_THREAD_SUCCESS) {
        if (mutexResult == VKRT_THREAD_SUCCESS) {
            vkrtMutexDestroy(&state->mutex);
        }
        state->mainThreadAvailable = NFD_Init() == NFD_OKAY ? 1u : 0u;
        if (state->mainThreadAvailable) {
            NFD_SetDisplayPropertiesFromGLFW();
        }
        session->editor.dialogState = state;
        return;
    }

    state->syncPrimitivesInitialized = 1u;
    state->running = 1u;
    if (vkrtThreadCreate(&state->worker, dialogWorkerMain, state) == VKRT_THREAD_SUCCESS) {
        state->workerStarted = 1u;
        vkrtMutexLock(&state->mutex);
        while (!state->workerStartupComplete) {
            vkrtCondWait(&state->condition, &state->mutex);
        }
        uint8_t workerAvailable = state->workerAvailable;
        vkrtMutexUnlock(&state->mutex);

        session->editor.dialogState = state;

        if (workerAvailable) {
            return;
        }

        shutdownDialogWorker(state);
    }

    state->mainThreadAvailable = NFD_Init() == NFD_OKAY ? 1u : 0u;
    if (state->mainThreadAvailable) {
        NFD_SetDisplayPropertiesFromGLFW();
    } else {
        LOG_ERROR("Failed to initialize file dialogs on the main thread");
    }

    session->editor.dialogState = state;
}

void editorUIShutdownDialogs(Session* session) {
    DialogState* state = getDialogState(session);
    if (!state) return;

    shutdownDialogWorker(state);

    if (state->mainThreadAvailable) {
        NFD_Quit();
        state->mainThreadAvailable = 0u;
    }

    if (state->responsePath) {
        free(state->responsePath);
        state->responsePath = NULL;
    }

    if (state->syncPrimitivesInitialized) {
        vkrtCondDestroy(&state->condition);
        vkrtMutexDestroy(&state->mutex);
    }

    free(state);
    session->editor.dialogState = NULL;
}

static void applyDialogResponse(Session* session, const DialogRequest* request, char* selectedPath) {
    if (!session) {
        free(selectedPath);
        return;
    }

    if (selectedPath && selectedPath[0]) {
        switch (request ? request->kind : DIALOG_KIND_NONE) {
            case DIALOG_KIND_IMPORT_MESH:
                sessionQueueMeshImport(session, selectedPath);
                break;
            case DIALOG_KIND_IMPORT_TEXTURE:
                sessionQueueTextureImport(
                    session,
                    request->materialIndex,
                    request->textureSlot,
                    selectedPath
                );
                break;
            case DIALOG_KIND_IMPORT_ENVIRONMENT:
                sessionQueueEnvironmentImport(session, selectedPath);
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
    DialogState* state = getDialogState(session);
    if (!state || !state->workerStarted) return;

    vkrtMutexLock(&state->mutex);
    if (!state->responsePending) {
        vkrtMutexUnlock(&state->mutex);
        return;
    }

    DialogRequest response = state->response;
    char* selectedPath = state->responsePath;
    state->response = (DialogRequest){0};
    state->responsePath = NULL;
    state->responsePending = 0u;
    vkrtMutexUnlock(&state->mutex);

    applyDialogResponse(session, &response, selectedPath);
}

static int queueDialogRequest(DialogState* state, const DialogRequest* request) {
    if (!state || !request || !state->workerStarted) return 0;

    vkrtMutexLock(&state->mutex);
    if (!state->workerAvailable ||
        state->requestPending ||
        state->requestActive ||
        state->responsePending) {
        vkrtMutexUnlock(&state->mutex);
        return 0;
    }

    state->request = *request;
    state->requestPending = 1u;
    vkrtCondSignal(&state->condition);
    vkrtMutexUnlock(&state->mutex);
    return 1;
}

static void executeDialogSynchronously(DialogState* state, Session* session, const DialogRequest* request) {
    if (!state || !request) return;
    if (!state->mainThreadAvailable) {
        LOG_ERROR("File dialog backend unavailable for %s request", queryDialogKindLabel(request->kind));
        return;
    }
    char* selectedPath = runDialogRequest(request);
    applyDialogResponse(session, request, selectedPath);
}

static int dispatchPreparedDialogRequest(Session* session, const DialogRequest* request) {
    DialogState* state = getDialogState(session);
    if (!state) return 1;
    if (!request) return 0;

    if (queueDialogRequest(state, request)) return 1;
    if (!dialogBackendAvailable(state)) {
        LOG_ERROR("File dialog backend unavailable for %s request", queryDialogKindLabel(request->kind));
        return 1;
    }
    executeDialogSynchronously(state, session, request);
    return 1;
}

static int tryScheduleImportMeshDialog(Session* session) {
    if (!sessionTakeMeshImportDialogRequest(session)) return 0;
    DialogState* state = getDialogState(session);
    if (!state) return 1;

    char defaultPath[VKRT_PATH_MAX];
    defaultPath[0] = '\0';
    resolveExistingParentPath("assets/models", NULL, defaultPath, sizeof(defaultPath));

    DialogRequest request = {0};
    if (!prepareDialogRequest(state, DIALOG_KIND_IMPORT_MESH, defaultPath, NULL, &request)) {
        return 1;
    }
    return dispatchPreparedDialogRequest(session, &request);
}

static int tryScheduleImportTextureDialog(Session* session) {
    uint32_t materialIndex = VKRT_INVALID_INDEX;
    uint32_t textureSlot = VKRT_INVALID_INDEX;
    if (!sessionTakeTextureImportDialogRequest(session, &materialIndex, &textureSlot)) return 0;

    DialogState* state = getDialogState(session);
    if (!state) return 1;

    char defaultPath[VKRT_PATH_MAX];
    defaultPath[0] = '\0';
    resolveExistingParentPath("assets", NULL, defaultPath, sizeof(defaultPath));

    DialogRequest request = {0};
    if (!prepareDialogRequest(state, DIALOG_KIND_IMPORT_TEXTURE, defaultPath, NULL, &request)) {
        return 1;
    }
    request.materialIndex = materialIndex;
    request.textureSlot = textureSlot;

    return dispatchPreparedDialogRequest(session, &request);
}

static int tryScheduleImportEnvironmentDialog(Session* session) {
    if (!sessionTakeEnvironmentImportDialogRequest(session)) return 0;
    DialogState* state = getDialogState(session);
    if (!state) return 1;

    char defaultPath[VKRT_PATH_MAX];
    defaultPath[0] = '\0';
    resolveExistingParentPath("assets/environments", NULL, defaultPath, sizeof(defaultPath));

    DialogRequest request = {0};
    if (!prepareDialogRequest(state, DIALOG_KIND_IMPORT_ENVIRONMENT, defaultPath, NULL, &request)) {
        return 1;
    }

    return dispatchPreparedDialogRequest(session, &request);
}

static int tryScheduleRenderSaveDialog(Session* session) {
    if (!sessionTakeRenderSaveDialogRequest(session)) return 0;
    DialogState* state = getDialogState(session);
    if (!state) return 1;

    char defaultPath[VKRT_PATH_MAX];
    defaultPath[0] = '\0';
    resolveExistingParentPath("captures", NULL, defaultPath, sizeof(defaultPath));

    DialogRequest request = {0};
    if (!prepareDialogRequest(state, DIALOG_KIND_SAVE_RENDER, defaultPath, "render.png", &request)) {
        return 1;
    }

    return dispatchPreparedDialogRequest(session, &request);
}

static int tryScheduleSequenceFolderDialog(Session* session) {
    if (!sessionTakeRenderSequenceFolderDialogRequest(session)) return 0;
    DialogState* state = getDialogState(session);
    if (!state) return 1;

    char defaultPath[VKRT_PATH_MAX];
    defaultPath[0] = '\0';
    resolveExistingParentPath(sessionGetRenderSequenceFolder(session), "captures", defaultPath, sizeof(defaultPath));

    DialogRequest request = {0};
    if (!prepareDialogRequest(state, DIALOG_KIND_PICK_SEQUENCE_FOLDER, defaultPath, NULL, &request)) {
        return 1;
    }

    return dispatchPreparedDialogRequest(session, &request);
}

void editorUIProcessDialogs(Session* session) {
    if (!session) return;

    drainDialogResponse(session);

    DialogState* state = getDialogState(session);
    if (state && state->workerStarted) {
        vkrtMutexLock(&state->mutex);
        uint8_t workerBusy = state->requestPending || state->requestActive || state->responsePending;
        vkrtMutexUnlock(&state->mutex);
        if (workerBusy) return;
    }

    if (tryScheduleImportMeshDialog(session)) return;
    if (tryScheduleImportTextureDialog(session)) return;
    if (tryScheduleImportEnvironmentDialog(session)) return;
    if (tryScheduleRenderSaveDialog(session)) return;
    (void)tryScheduleSequenceFolderDialog(session);
}
