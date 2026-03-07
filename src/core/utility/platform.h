#pragma once

#include <stdint.h>

#if defined(_WIN32)
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4005)
#endif
#include <windows.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

typedef CRITICAL_SECTION VKRT_Mutex;
typedef CONDITION_VARIABLE VKRT_Cond;
typedef HANDLE VKRT_Thread;
typedef int (*VKRT_ThreadFunc)(void* userData);

#if defined(MAX_PATH)
#define VKRT_PATH_MAX MAX_PATH
#else
#define VKRT_PATH_MAX 4096
#endif

#else
#include <limits.h>
#include <pthread.h>

typedef pthread_mutex_t VKRT_Mutex;
typedef pthread_cond_t VKRT_Cond;
typedef pthread_t VKRT_Thread;
typedef int (*VKRT_ThreadFunc)(void* userData);

#if defined(PATH_MAX)
#define VKRT_PATH_MAX PATH_MAX
#else
#define VKRT_PATH_MAX 4096
#endif

#endif

enum {
    VKRT_THREAD_SUCCESS = 0,
    VKRT_THREAD_ERROR = 1,
    VKRT_MUTEX_PLAIN = 0,
};

int vkrtMutexInit(VKRT_Mutex* mutex, int type);
void vkrtMutexDestroy(VKRT_Mutex* mutex);
int vkrtMutexLock(VKRT_Mutex* mutex);
int vkrtMutexUnlock(VKRT_Mutex* mutex);

int vkrtCondInit(VKRT_Cond* condition);
void vkrtCondDestroy(VKRT_Cond* condition);
int vkrtCondWait(VKRT_Cond* condition, VKRT_Mutex* mutex);
int vkrtCondSignal(VKRT_Cond* condition);
int vkrtCondBroadcast(VKRT_Cond* condition);

int vkrtThreadCreate(VKRT_Thread* thread, VKRT_ThreadFunc function, void* argument);
int vkrtThreadJoin(VKRT_Thread thread, int* result);
