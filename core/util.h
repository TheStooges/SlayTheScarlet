/* Small helpers shared by the file formats in core/. */
#ifndef STS_UTIL_H
#define STS_UTIL_H

#include <stddef.h>
#include <stdint.h>

/* Reads a whole file. Returns a malloc'd, NUL-terminated buffer the caller must
 * free, and sets *len to the size without the terminator. NULL on any failure. */
char *sts_read_file(const char *path, size_t *len);

/* FNV-1a. Start from STS_FNV_START; chain calls to hash several pieces. */
#define STS_FNV_START 2166136261u
uint32_t sts_fnv1a(uint32_t hash, const void *data, size_t len);

#endif
