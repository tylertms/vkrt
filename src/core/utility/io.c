#include "io.h"

#include "debug.h"
#include "platform.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <fileapi.h>
#include <libloaderapi.h>
#include <minwindef.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

enum {
    K_EXE_RELATIVE_PATH_CAPACITY = 4096,
};

static char* pathFindLastSeparator(char* path);
static int copyPathString(char* out, size_t outSize, const char* value);
static int joinPath(char* out, size_t outSize, const char* base, const char* value);

static int getExeDir(char* out, size_t outSize) {
#ifdef _WIN32
    DWORD len = GetModuleFileNameA(NULL, out, (DWORD)outSize);
    if (len == 0 || len == outSize) return -1;

    while (len && out[len] != '\\') {
        --len;
    }
    out[len] = '\0';
    return 0;
#elif defined(__APPLE__)
    uint32_t pathSize = (uint32_t)outSize;
    if (_NSGetExecutablePath(out, &pathSize) != 0) {
        return -1;
    }

    out[outSize - 1u] = '\0';
    size_t len = strlen(out);
    while (len && out[len] != '/') {
        --len;
    }
    out[len] = '\0';
    return 0;
#else
    long pathLength = (long)readlink("/proc/self/exe", out, outSize - 1);
    size_t len = 0u;
    if (pathLength <= 0) {
        return -1;
    }
    len = (size_t)pathLength;
    out[len] = '\0';
    while (len && out[len] != '/') {
        --len;
    }
    out[len] = '\0';
    return 0;
#endif
}

static int resolveExistingPreferredParentPath(const char* preferredPath, char* outPath, size_t outPathSize) {
    char candidate[VKRT_PATH_MAX];

    if (!preferredPath || !preferredPath[0]) return -1;
    if (copyPathString(candidate, sizeof(candidate), preferredPath) != 0) return -1;

    while (candidate[0]) {
        char* separator = NULL;

        pathTrimTrailingSeparators(candidate);
        if (resolveExistingPath(candidate, outPath, outPathSize) == 0) {
            return 0;
        }

        separator = pathFindLastSeparator(candidate);
        if (!separator) break;
        if (separator == candidate) {
            candidate[1] = '\0';
        } else {
            *separator = '\0';
        }
    }

    return -1;
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

int copyParentDirectoryPath(const char* path, char* outDirectory, size_t outDirectorySize) {
    if (!path || !path[0] || !outDirectory || outDirectorySize == 0u) return -1;
    if (copyPathString(outDirectory, outDirectorySize, path) != 0) return -1;

    char* separator = pathFindLastSeparator(outDirectory);
    if (!separator) {
        if (outDirectorySize < 2u) return -1;
        outDirectory[0] = '.';
        outDirectory[1] = '\0';
        return 0;
    }

    if (separator == outDirectory) {
        if (outDirectorySize < 2u) return -1;
        separator[1] = '\0';
    } else {
        *separator = '\0';
    }

    return 0;
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

static FILE* fopenExeRelative(const char* relpath, const char* mode) {
    char buf[K_EXE_RELATIVE_PATH_CAPACITY];
    if (getExeDir(buf, sizeof buf) < 0) {
        LOG_ERROR("Failed to determine executable path");
        return NULL;
    }

    size_t dirlen = strlen(buf);
    (void)snprintf(buf + dirlen, sizeof buf - dirlen, "/%s", relpath);

#ifdef _WIN32
    FILE* file = NULL;
    return fopen_s(&file, buf, mode) == 0 ? file : NULL;
#else
    return fopen(buf, mode);
#endif
}

static int pathExists(const char* path) {
    if (!path || !path[0]) return 0;
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES;
#else
    struct stat status = {0};
    return stat(path, &status) == 0;
#endif
}

static int copyPathString(char* out, size_t outSize, const char* value) {
    if (!out || outSize == 0 || !value || !value[0]) return -1;
    int written = snprintf(out, outSize, "%s", value);
    return (written > 0 && (size_t)written < outSize) ? 0 : -1;
}

static int canonicalizePath(const char* path, char* outPath, size_t outPathSize) {
    if (!path || !path[0] || !outPath || outPathSize == 0) return -1;

#ifdef _WIN32
    return _fullpath(outPath, path, outPathSize) ? 0 : -1;
#else
    if (path[0] == '/') {
        return copyPathString(outPath, outPathSize, path);
    }

    char currentDirectory[VKRT_PATH_MAX];
    if (!getcwd(currentDirectory, sizeof(currentDirectory))) return -1;
    return joinPath(outPath, outPathSize, currentDirectory, path);
#endif
}

static int joinPath(char* out, size_t outSize, const char* base, const char* value) {
    if (!out || outSize == 0 || !base || !base[0] || !value || !value[0]) return -1;

    size_t baseLength = strlen(base);
    const char* separator =
        (baseLength > 0 && (base[baseLength - 1] == '/' || base[baseLength - 1] == '\\')) ? "" : "/";
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
    if (getExeDir(executableDir, sizeof(executableDir)) != 0) {
        return -1;
    }

    char candidate[VKRT_PATH_MAX];
    if (joinPath(candidate, sizeof(candidate), executableDir, path) == 0 &&
        resolveExistingCandidate(candidate, outPath, outPathSize) == 0) {
        return 0;
    }

    char* lastSeparator = strrchr(executableDir, '/');
#ifdef _WIN32
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

    if (resolveExistingPreferredParentPath(preferredPath, outPath, outPathSize) == 0) {
        return 0;
    }

    if (fallbackPath && fallbackPath[0] && resolveExistingPath(fallbackPath, outPath, outPathSize) == 0) {
        return 0;
    }

    return resolveExistingPath(".", outPath, outPathSize);
}

const char* readFile(const char* filename, size_t* fileSize) {
    if (!filename || !fileSize) {
        LOG_ERROR("Invalid readFile arguments");
        return NULL;
    }

    FILE* file = fopenExeRelative(filename, "rb");
    if (!file) {
        LOG_ERROR("Failed to open file: %s", filename);
        return NULL;
    }
    (void)fseek(file, 0, SEEK_END);
    long fileLength = ftell(file);
    if (fileLength < 0) {
        LOG_ERROR("Failed to determine file size: %s", filename);
        (void)fclose(file);
        return NULL;
    }
    *fileSize = (size_t)fileLength;
    (void)fseek(file, 0, SEEK_SET);

    size_t allocSize = *fileSize > 0 ? *fileSize : 1;
    char* buffer = (char*)malloc(allocSize);
    if (!buffer) {
        LOG_ERROR("Failed to allocate file buffer for %s", filename);
        (void)fclose(file);
        return NULL;
    }

    size_t bytesRead = fread(buffer, 1, *fileSize, file);
    (void)fclose(file);
    if (bytesRead != *fileSize) {
        free(buffer);
        LOG_ERROR("Failed to read complete file: %s", filename);
        return NULL;
    }

    return buffer;
}
