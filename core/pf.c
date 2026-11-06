#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pf.h"

static const char *const g_names[STS_NUM_CHARACTERS] = {
    "Reimu", "Marisa", "Sakuya", "Alice", "Patchouli", "Youmu", "Remilia",
    "Yuyuko", "Yukari", "Suika", "Reisen", "Aya", "Komachi", "Iku", "Tenshi",
    "Sanae", "Cirno", "Meiling", "Utsuho", "Suwako"
};

const char *sts_character_name(int index)
{
    if (index < 0 || index >= STS_NUM_CHARACTERS)
        return "?";
    return g_names[index];
}

sts_pf_status sts_pf_parse(const uint8_t *data, size_t len, sts_profile *out, size_t *consumed)
{
    size_t pos = STS_PF_HEADER_SIZE;
    int c, s, i;

    if (len < STS_PF_HEADER_SIZE)
        return STS_PF_TRUNCATED;
    memcpy(out->header, data, STS_PF_HEADER_SIZE);

    for (c = 0; c < STS_NUM_CHARACTERS; c++) {
        for (s = 0; s < STS_NUM_SLOTS; s++) {
            sts_deck *d = &out->decks[c][s];
            int n;
            if (pos >= len)
                return STS_PF_TRUNCATED;
            n = data[pos++];
            if (len - pos < (size_t)n * 2)
                return STS_PF_TRUNCATED;
            d->count = n;
            for (i = 0; i < n; i++) {
                d->ids[i] = (uint16_t)(data[pos] | (data[pos + 1] << 8));
                pos += 2;
            }
        }
    }
    if (consumed)
        *consumed = pos;
    return STS_PF_OK;
}

size_t sts_pf_size(const sts_profile *p)
{
    size_t n = STS_PF_HEADER_SIZE;
    int c, s;
    for (c = 0; c < STS_NUM_CHARACTERS; c++)
        for (s = 0; s < STS_NUM_SLOTS; s++)
            n += 1 + (size_t)p->decks[c][s].count * 2;
    return n;
}

size_t sts_pf_serialize(const sts_profile *p, uint8_t *buf, size_t cap)
{
    size_t pos = STS_PF_HEADER_SIZE;
    int c, s, i;

    if (cap < sts_pf_size(p))
        return 0;
    memcpy(buf, p->header, STS_PF_HEADER_SIZE);

    for (c = 0; c < STS_NUM_CHARACTERS; c++) {
        for (s = 0; s < STS_NUM_SLOTS; s++) {
            sts_deck tmp = p->decks[c][s];
            sts_deck_sort(&tmp);
            buf[pos++] = (uint8_t)tmp.count;
            for (i = 0; i < tmp.count; i++) {
                buf[pos++] = (uint8_t)(tmp.ids[i] & 0xFF);
                buf[pos++] = (uint8_t)(tmp.ids[i] >> 8);
            }
        }
    }
    return pos;
}

sts_pf_status sts_pf_load(const char *path, sts_profile *out, size_t *consumed)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf;
    long size;
    sts_pf_status st;

    if (!f)
        return STS_PF_IO;
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return STS_PF_IO;
    }
    buf = (uint8_t *)malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return STS_PF_IO;
    }
    if (fread(buf, 1, (size_t)size, f) != (size_t)size) {
        free(buf);
        fclose(f);
        return STS_PF_IO;
    }
    fclose(f);
    st = sts_pf_parse(buf, (size_t)size, out, consumed);
    free(buf);
    return st;
}

sts_pf_status sts_pf_save(const char *path, const sts_profile *p)
{
    size_t n = sts_pf_size(p);
    uint8_t *buf = (uint8_t *)malloc(n);
    FILE *f;
    sts_pf_status st = STS_PF_OK;

    if (!buf)
        return STS_PF_IO;
    if (sts_pf_serialize(p, buf, n) != n) {
        free(buf);
        return STS_PF_IO;
    }
    f = fopen(path, "wb");
    if (!f || fwrite(buf, 1, n, f) != n)
        st = STS_PF_IO;
    if (f)
        fclose(f);
    free(buf);
    return st;
}
