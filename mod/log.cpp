/* sts.log, next to the DLL. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "internal.h"

/* The log is also written from the threads that handle packets. */
CRITICAL_SECTION g_log_lock;

/* sts.log next to the DLL, restarted each launch. Holds no IPs or player names. */
void slog(const char *fmt, ...)
{
    static FILE *file = NULL;
    va_list args;

    EnterCriticalSection(&g_log_lock);
    if (!file) {
        char path[MAX_PATH + 32];
        sprintf(path, "%ssts.log", g_dir);
        file = fopen(path, "w");
    }
    if (file) {
        fprintf(file, "[%lu] ", (unsigned long)GetTickCount());
        va_start(args, fmt);
        vfprintf(file, fmt, args);
        va_end(args);
        fputc('\n', file);
        fflush(file);
    }
    LeaveCriticalSection(&g_log_lock);
}
