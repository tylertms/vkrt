#include "debug.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

void vkrtLogLine(FILE* stream, const char* level, const char* format, ...) {
    if (!stream || !level || !format) return;

    va_list args;
    (void)fputs(level, stream);
    (void)fputs(": ", stream);
    va_start(args, format);
    (void)vfprintf(stream, format, args);
    va_end(args);
    (void)fputc('\n', stream);
}
