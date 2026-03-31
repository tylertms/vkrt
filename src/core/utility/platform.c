#include "platform.h"

#include "debug.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef __APPLE__
#include <mach/mach_time.h>
#endif

#ifdef _WIN32
#include <handleapi.h>
#include <minwindef.h>
#include <process.h>
#include <processthreadsapi.h>
#include <profileapi.h>
#include <synchapi.h>
#include <windows.h>
#include <winnt.h>

static int gVkrtInfoLoggingEnabled = 1;

int vkrtInfoLoggingEnabled(void) {
    return gVkrtInfoLoggingEnabled;
}

void vkrtSetInfoLoggingEnabled(int enabled) {
    gVkrtInfoLoggingEnabled = enabled ? 1 : 0;
}

uint64_t getMicroseconds(void) {
    static LARGE_INTEGER frequency = {0};
    LARGE_INTEGER counter;

    if (frequency.QuadPart == 0) {
        QueryPerformanceFrequency(&frequency);
    }

    QueryPerformanceCounter(&counter);
    return (uint64_t)(counter.QuadPart * 1000000 / frequency.QuadPart);
}

typedef struct ThreadStartContext {
    VKRT_ThreadFunc function;
    void* argument;
} ThreadStartContext;

static unsigned __stdcall vkrtThreadTrampoline(void* rawContext) {
    ThreadStartContext* context = (ThreadStartContext*)rawContext;
    VKRT_ThreadFunc function = context->function;
    void* argument = context->argument;
    free(context);
    return (unsigned)function(argument);
}

int vkrtMutexInit(VKRT_Mutex* mutex, int type) {
    (void)type;
    if (!mutex) return VKRT_THREAD_ERROR;
    InitializeCriticalSection(mutex);
    return VKRT_THREAD_SUCCESS;
}

void vkrtMutexDestroy(VKRT_Mutex* mutex) {
    if (!mutex) return;
    DeleteCriticalSection(mutex);
}

int vkrtMutexLock(VKRT_Mutex* mutex) {
    if (!mutex) return VKRT_THREAD_ERROR;
    EnterCriticalSection(mutex);
    return VKRT_THREAD_SUCCESS;
}

int vkrtMutexUnlock(VKRT_Mutex* mutex) {
    if (!mutex) return VKRT_THREAD_ERROR;
    LeaveCriticalSection(mutex);
    return VKRT_THREAD_SUCCESS;
}

int vkrtCondInit(VKRT_Cond* condition) {
    if (!condition) return VKRT_THREAD_ERROR;
    InitializeConditionVariable(condition);
    return VKRT_THREAD_SUCCESS;
}

void vkrtCondDestroy(VKRT_Cond* condition) {
    (void)condition;
}

int vkrtCondWait(VKRT_Cond* condition, VKRT_Mutex* mutex) {
    if (!condition || !mutex) return VKRT_THREAD_ERROR;
    return SleepConditionVariableCS(condition, mutex, INFINITE) ? VKRT_THREAD_SUCCESS : VKRT_THREAD_ERROR;
}

int vkrtCondSignal(VKRT_Cond* condition) {
    if (!condition) return VKRT_THREAD_ERROR;
    WakeConditionVariable(condition);
    return VKRT_THREAD_SUCCESS;
}

int vkrtCondBroadcast(VKRT_Cond* condition) {
    if (!condition) return VKRT_THREAD_ERROR;
    WakeAllConditionVariable(condition);
    return VKRT_THREAD_SUCCESS;
}

int vkrtThreadCreate(VKRT_Thread* thread, VKRT_ThreadFunc function, void* argument) {
    if (!thread || !function) return VKRT_THREAD_ERROR;

    ThreadStartContext* context = (ThreadStartContext*)malloc(sizeof(*context));
    if (!context) return VKRT_THREAD_ERROR;

    context->function = function;
    context->argument = argument;

    uintptr_t handle = _beginthreadex(NULL, 0, vkrtThreadTrampoline, context, 0, NULL);
    if (handle == 0) {
        free(context);
        return VKRT_THREAD_ERROR;
    }

    *thread = (HANDLE)handle;
    return VKRT_THREAD_SUCCESS;
}

int vkrtThreadJoin(VKRT_Thread thread, int* result) {
    if (!thread) return VKRT_THREAD_ERROR;
    if (WaitForSingleObject(thread, INFINITE) != WAIT_OBJECT_0) {
        CloseHandle(thread);
        return VKRT_THREAD_ERROR;
    }

    if (result) {
        DWORD exitCode = 0;
        if (!GetExitCodeThread(thread, &exitCode)) {
            CloseHandle(thread);
            return VKRT_THREAD_ERROR;
        }
        *result = (int)exitCode;
    }

    CloseHandle(thread);
    return VKRT_THREAD_SUCCESS;
}

#else

#include <pthread.h>

static int gVkrtInfoLoggingEnabled = 1;

int vkrtInfoLoggingEnabled(void) {
    return gVkrtInfoLoggingEnabled;
}

void vkrtSetInfoLoggingEnabled(int enabled) {
    gVkrtInfoLoggingEnabled = enabled ? 1 : 0;
}

typedef struct ThreadStartContext {
    VKRT_ThreadFunc function;
    void* argument;
} ThreadStartContext;

uint64_t getMicroseconds(void) {
#ifdef __APPLE__
    static mach_timebase_info_data_t timebase = {0};
    if (timebase.denom == 0u) {
        (void)mach_timebase_info(&timebase);
    }

    uint64_t ticks = mach_absolute_time();
    uint64_t nanoseconds = ticks;
    if (timebase.denom != 0u) {
        nanoseconds = (ticks * timebase.numer) / timebase.denom;
    }
    return nanoseconds / 1000u;
#else
    struct timespec timeSpec;
    clock_gettime(CLOCK_MONOTONIC, &timeSpec);
    return ((uint64_t)timeSpec.tv_sec * 1000000) + (timeSpec.tv_nsec / 1000);
#endif
}

static void* vkrtThreadTrampoline(void* rawContext) {
    ThreadStartContext* context = (ThreadStartContext*)rawContext;
    VKRT_ThreadFunc function = context->function;
    void* argument = context->argument;
    int* result = (int*)malloc(sizeof(*result));
    free(context);
    if (!result) return NULL;
    *result = function(argument);
    return result;
}

int vkrtMutexInit(VKRT_Mutex* mutex, int type) {
    (void)type;
    return pthread_mutex_init(mutex, NULL) == 0 ? VKRT_THREAD_SUCCESS : VKRT_THREAD_ERROR;
}

void vkrtMutexDestroy(VKRT_Mutex* mutex) {
    if (!mutex) return;
    pthread_mutex_destroy(mutex);
}

int vkrtMutexLock(VKRT_Mutex* mutex) {
    return pthread_mutex_lock(mutex) == 0 ? VKRT_THREAD_SUCCESS : VKRT_THREAD_ERROR;
}

int vkrtMutexUnlock(VKRT_Mutex* mutex) {
    return pthread_mutex_unlock(mutex) == 0 ? VKRT_THREAD_SUCCESS : VKRT_THREAD_ERROR;
}

int vkrtCondInit(VKRT_Cond* condition) {
    return pthread_cond_init(condition, NULL) == 0 ? VKRT_THREAD_SUCCESS : VKRT_THREAD_ERROR;
}

void vkrtCondDestroy(VKRT_Cond* condition) {
    if (!condition) return;
    pthread_cond_destroy(condition);
}

int vkrtCondWait(VKRT_Cond* condition, VKRT_Mutex* mutex) {
    return pthread_cond_wait(condition, mutex) == 0 ? VKRT_THREAD_SUCCESS : VKRT_THREAD_ERROR;
}

int vkrtCondSignal(VKRT_Cond* condition) {
    return pthread_cond_signal(condition) == 0 ? VKRT_THREAD_SUCCESS : VKRT_THREAD_ERROR;
}

int vkrtCondBroadcast(VKRT_Cond* condition) {
    return pthread_cond_broadcast(condition) == 0 ? VKRT_THREAD_SUCCESS : VKRT_THREAD_ERROR;
}

int vkrtThreadCreate(VKRT_Thread* thread, VKRT_ThreadFunc function, void* argument) {
    if (!thread || !function) return VKRT_THREAD_ERROR;

    ThreadStartContext* context = (ThreadStartContext*)malloc(sizeof(*context));
    if (!context) return VKRT_THREAD_ERROR;

    context->function = function;
    context->argument = argument;
    if (pthread_create(thread, NULL, vkrtThreadTrampoline, context) != 0) {
        free(context);
        return VKRT_THREAD_ERROR;
    }

    return VKRT_THREAD_SUCCESS;
}

int vkrtThreadJoin(VKRT_Thread thread, int* result) {
    void* threadResult = NULL;
    int* threadResultPtr = NULL;

    if (pthread_join(thread, &threadResult) != 0) return VKRT_THREAD_ERROR;
    threadResultPtr = (int*)threadResult;
    if (result) {
        *result = threadResultPtr ? *threadResultPtr : 0;
    }
    free(threadResultPtr);
    return VKRT_THREAD_SUCCESS;
}

#endif
