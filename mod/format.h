/* Writing formatted text into a buffer without ever going past its end. Text that does not fit is
 * cut short; the buffer always ends in a 0. Use these instead of sprintf, strcpy and strcat: a long
 * translation, profile name, card name or install path must never overrun a fixed buffer. */
#ifndef STS_FORMAT_H
#define STS_FORMAT_H

#include <sal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Into a buffer of a known size. Returns the length of the text written. */
static inline int format_bounded(_Out_writes_z_(size) char *out, size_t size, _Printf_format_string_ const char *format, ...)
{
    va_list args;

    if (!out || size == 0)
        return 0;
    va_start(args, format);
    _vsnprintf(out, size - 1, format, args);
    va_end(args);
    out[size - 1] = 0;
    return (int)strlen(out);
}

/* Into an array, whose size the compiler knows: a pointer does not compile, so the size is always
 * right. Use format_bounded, with the size, for a pointer. The annotations tell MSVC's analyser
 * (/analyze) that the text always ends in a 0, and to check the format against its arguments. */
template <size_t SIZE>
static inline int format_text(_Out_writes_z_(SIZE) char (&out)[SIZE], _Printf_format_string_ const char *format, ...)
{
    va_list args;

    va_start(args, format);
    _vsnprintf(out, SIZE - 1, format, args);
    va_end(args);
    out[SIZE - 1] = 0;
    return (int)strlen(out);
}

#endif
