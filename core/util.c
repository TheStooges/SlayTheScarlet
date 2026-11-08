#include <stdio.h>
#include <stdlib.h>
#include "util.h"

char *sts_read_file(const char *path, size_t *len)
{
    FILE *file = fopen(path, "rb");
    char *buf;
    long size;

    if (!file)
        return NULL;
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        fclose(file);
        return NULL;
    }
    if (fread(buf, 1, (size_t)size, file) != (size_t)size) {
        free(buf);
        fclose(file);
        return NULL;
    }
    fclose(file);
    buf[size] = 0;
    *len = (size_t)size;
    return buf;
}

uint32_t sts_fnv1a(uint32_t hash, const void *data, size_t len)
{
    const unsigned char *bytes = (const unsigned char *)data;
    size_t wau;

    for (wau = 0; wau < len; wau++) {
        hash ^= bytes[wau];
        hash *= 16777619u;
    }
    return hash;
}
