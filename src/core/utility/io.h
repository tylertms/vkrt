#pragma once

#include "platform.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

char* stringDuplicate(const char* value);
const char* pathBasename(const char* path);
char* pathTrimTrailingSeparators(char* path);
int copyParentDirectoryPath(const char* path, char* outDirectory, size_t outDirectorySize);
int resolveExistingParentPath(const char* preferredPath, const char* fallbackPath, char* outPath, size_t outPathSize);
const char* readFile(const char* filename, size_t* fileSize);
int resolveExistingPath(const char* path, char* outPath, size_t outPathSize);

#ifdef __cplusplus
}
#endif
