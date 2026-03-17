#include "io.h"
#include "debug.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#include <direct.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <unistd.h>
#else
#include <unistd.h>
#endif

enum {
    kExeRelativePathCapacity = 4096,
};

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

char* stringDuplicate(const char* value) {
    if (!value) return NULL;

    size_t length = strlen(value);
    char* copy = (char*)malloc(length + 1);
    if (!copy) return NULL;

    memcpy(copy, value, length + 1);
    return copy;
}

char* pathTrimTrailingSeparators(char* path) {
    if (!path) return NULL;

    size_t length = strlen(path);
    while (length > 1 && (path[length - 1] == '/' || path[length - 1] == '\\')) {
        path[--length] = '\0';
    }
    return path;
}

static char* pathFindLastSeparator(char* path) {
    if (!path) return NULL;

    char* slash = strrchr(path, '/');
    char* backslash = strrchr(path, '\\');
    if (!slash) return backslash;
    if (!backslash) return slash;
    return slash > backslash ? slash : backslash;
}

const char* pathBasename(const char* path) {
    if (!path || !path[0]) return "";

    const char* slash = strrchr(path, '/');
    const char* backslash = strrchr(path, '\\');
    if (!slash) return backslash ? backslash + 1 : path;
    if (!backslash) return slash + 1;
    return slash > backslash ? slash + 1 : backslash + 1;
}

static FILE* fopen_exe_relative(const char* relpath, const char* mode) {
    char buf[kExeRelativePathCapacity];
    if (get_exe_dir(buf, sizeof buf) < 0) {
        LOG_ERROR("Failed to determine executable path");
        return NULL;
    }

    size_t dirlen = strlen(buf);
    snprintf(buf + dirlen, sizeof buf - dirlen, "/%s", relpath);

#if defined(_WIN32)
    FILE* file = NULL;
    return fopen_s(&file, buf, mode) == 0 ? file : NULL;
#else
    return fopen(buf, mode);
#endif
}

static int pathExists(const char* path) {
    if (!path || !path[0]) return 0;
#if defined(_WIN32)
    DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES;
#else
    return access(path, F_OK) == 0;
#endif
}

static int copyPathString(char* out, size_t outSize, const char* value) {
    if (!out || outSize == 0 || !value || !value[0]) return -1;
    int written = snprintf(out, outSize, "%s", value);
    return (written > 0 && (size_t)written < outSize) ? 0 : -1;
}

static int canonicalizePath(const char* path, char* outPath, size_t outPathSize) {
    if (!path || !path[0] || !outPath || outPathSize == 0) return -1;

#if defined(_WIN32)
    return _fullpath(outPath, path, outPathSize) ? 0 : -1;
#else
    if (outPathSize < VKRT_PATH_MAX) {
        char resolved[VKRT_PATH_MAX];
        if (!realpath(path, resolved)) return -1;
        return copyPathString(outPath, outPathSize, resolved);
    }
    return realpath(path, outPath) ? 0 : -1;
#endif
}

static int joinPath(char* out, size_t outSize, const char* base, const char* value) {
    if (!out || outSize == 0 || !base || !base[0] || !value || !value[0]) return -1;

    size_t baseLength = strlen(base);
    const char* separator = (baseLength > 0 && (base[baseLength - 1] == '/' || base[baseLength - 1] == '\\'))
        ? ""
        : "/";
    int written = snprintf(out, outSize, "%s%s%s", base, separator, value);
    return (written > 0 && (size_t)written < outSize) ? 0 : -1;
}

static int resolveExistingCandidate(const char* candidate, char* outPath, size_t outPathSize) {
    if (!candidate || !candidate[0] || !pathExists(candidate)) return -1;
    if (canonicalizePath(candidate, outPath, outPathSize) == 0) return 0;
    return copyPathString(outPath, outPathSize, candidate);
}

int resolveExistingPath(const char* path, char* outPath, size_t outPathSize) {
    if (!path || !path[0] || !outPath || outPathSize == 0) return -1;

    if (resolveExistingCandidate(path, outPath, outPathSize) == 0) {
        return 0;
    }

    char executableDir[VKRT_PATH_MAX];
    if (get_exe_dir(executableDir, sizeof(executableDir)) != 0) {
        return -1;
    }

    char candidate[VKRT_PATH_MAX];
    if (joinPath(candidate, sizeof(candidate), executableDir, path) == 0 &&
        resolveExistingCandidate(candidate, outPath, outPathSize) == 0) {
        return 0;
    }

    char* lastSeparator = strrchr(executableDir, '/');
#if defined(_WIN32)
    char* lastBackslash = strrchr(executableDir, '\\');
    if (!lastSeparator || (lastBackslash && lastBackslash > lastSeparator)) {
        lastSeparator = lastBackslash;
    }
#endif
    if (!lastSeparator || lastSeparator == executableDir) {
        return -1;
    }

    *lastSeparator = '\0';
    if (joinPath(candidate, sizeof(candidate), executableDir, path) == 0 &&
        resolveExistingCandidate(candidate, outPath, outPathSize) == 0) {
        return 0;
    }

    return -1;
}

int resolveExistingParentPath(const char* preferredPath, const char* fallbackPath, char* outPath, size_t outPathSize) {
    if (!outPath || outPathSize == 0) return -1;

    if (preferredPath && preferredPath[0]) {
        char candidate[VKRT_PATH_MAX];
        if (copyPathString(candidate, sizeof(candidate), preferredPath) == 0) {
            while (candidate[0]) {
                pathTrimTrailingSeparators(candidate);
                if (resolveExistingPath(candidate, outPath, outPathSize) == 0) {
                    return 0;
                }

                char* separator = pathFindLastSeparator(candidate);
                if (!separator) break;
                if (separator == candidate) {
                    candidate[1] = '\0';
                } else {
                    *separator = '\0';
                }
            }
        }
    }

    if (fallbackPath && fallbackPath[0] &&
        resolveExistingPath(fallbackPath, outPath, outPathSize) == 0) {
        return 0;
    }

    return resolveExistingPath(".", outPath, outPathSize);
}

const char* readFile(const char* filename, size_t* fileSize) {
    if (!filename || !fileSize) {
        LOG_ERROR("Invalid readFile arguments");
        return NULL;
    }

    FILE* file = fopen_exe_relative(filename, "rb");
    if (!file) {
        LOG_ERROR("Failed to open file: %s", filename);
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    long fileLength = ftell(file);
    if (fileLength < 0) {
        LOG_ERROR("Failed to determine file size: %s", filename);
        fclose(file);
        return NULL;
    }
    *fileSize = (size_t)fileLength;
    fseek(file, 0, SEEK_SET);

    size_t allocSize = *fileSize > 0 ? *fileSize : 1;
    char* buffer = (char*)malloc(allocSize);
    if (!buffer) {
        LOG_ERROR("Failed to allocate file buffer for %s", filename);
        fclose(file);
        return NULL;
    }

    size_t bytesRead = fread(buffer, 1, *fileSize, file);
    fclose(file);
    if (bytesRead != *fileSize) {
        free(buffer);
        LOG_ERROR("Failed to read complete file: %s", filename);
        return NULL;
    }

    return buffer;
}
