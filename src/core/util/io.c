#include "io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <limits.h>
#include <mach-o/dyld.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

static int get_exe_dir(char* out, size_t sz) {
#if defined(_WIN32)
    DWORD len = GetModuleFileNameA(NULL, out, (DWORD)sz);
    if (len == 0 || len == sz)
        return -1;

    while (len && out[len] != '\\')
        --len;
    out[len] = '\0';
    return 0;
#elif defined(__APPLE__)
    uint32_t len = (uint32_t)sz;
    if (_NSGetExecutablePath(out, &len) != 0)
        return -1;
    char* dir = strrchr(out, '/');
    if (!dir)
        return -1;
    *dir = '\0';
    return 0;
#else
    ssize_t len = readlink("/proc/self/exe", out, sz - 1);
    if (len <= 0)
        return -1;
    out[len] = '\0';
    while (len && out[len] != '/')
        --len;
    out[len] = '\0';
    return 0;
#endif
}

static FILE* fopen_exe_relative(const char* relpath, const char* mode) {
    char buf[4096];
    if (get_exe_dir(buf, sizeof buf) < 0) {
        perror("[ERROR]: cannot get exe path");
        return NULL;
    }

    size_t dirlen = strlen(buf);
    snprintf(buf + dirlen, sizeof buf - dirlen, "/%s", relpath);

    return fopen(buf, mode);
}

const char* readFile(const char* filename, size_t* fileSize) {
    FILE* file = fopen_exe_relative(filename, "rb");
    if (!file) {
        perror("[ERROR]: Failed to open file");
        exit(EXIT_FAILURE);
    }
    fseek(file, 0, SEEK_END);
    *fileSize = (size_t)ftell(file);
    fseek(file, 0, SEEK_SET);

    char* buffer = malloc(*fileSize);
    if (!buffer) {
        perror("[ERROR]: Failed to allocate memory!");
        fclose(file);
        exit(EXIT_FAILURE);
    }
    fread(buffer, 1, *fileSize, file);
    fclose(file);
    return buffer;
}
