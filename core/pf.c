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
    int character, slot, i;

    if (len < STS_PF_HEADER_SIZE)
        return STS_PF_TRUNCATED;
    memcpy(out->header, data, STS_PF_HEADER_SIZE);

    for (character = 0; character < STS_NUM_CHARACTERS; character++) {
        for (slot = 0; slot < STS_NUM_SLOTS; slot++) {
            sts_deck *deck = &out->decks[character][slot];
            int count;
            if (pos >= len)
                return STS_PF_TRUNCATED;
            count = data[pos++];
            if (len - pos < (size_t)count * 2)
                return STS_PF_TRUNCATED;
            deck->count = count;
            for (i = 0; i < count; i++) {
                deck->ids[i] = (uint16_t)(data[pos] | (data[pos + 1] << 8));
                pos += 2;
            }
        }
    }
    if (consumed)
        *consumed = pos;
    return STS_PF_OK;
}

size_t sts_pf_size(const sts_profile *profile)
{
    size_t count = STS_PF_HEADER_SIZE;
    int character, slot;
    for (character = 0; character < STS_NUM_CHARACTERS; character++)
        for (slot = 0; slot < STS_NUM_SLOTS; slot++)
            count += 1 + (size_t)profile->decks[character][slot].count * 2;
    return count;
}

size_t sts_pf_serialize(const sts_profile *profile, uint8_t *buf, size_t cap)
{
    size_t pos = STS_PF_HEADER_SIZE;
    int character, slot, i;

    if (cap < sts_pf_size(profile))
        return 0;
    memcpy(buf, profile->header, STS_PF_HEADER_SIZE);

    for (character = 0; character < STS_NUM_CHARACTERS; character++) {
        for (slot = 0; slot < STS_NUM_SLOTS; slot++) {
            sts_deck tmp = profile->decks[character][slot];
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
    FILE *file = fopen(path, "rb");
    uint8_t *buf;
    long size;
    sts_pf_status status;

    if (!file)
        return STS_PF_IO;
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return STS_PF_IO;
    }
    buf = (uint8_t *)malloc((size_t)size + 1);
    if (!buf) {
        fclose(file);
        return STS_PF_IO;
    }
    if (fread(buf, 1, (size_t)size, file) != (size_t)size) {
        free(buf);
        fclose(file);
        return STS_PF_IO;
    }
    fclose(file);
    status = sts_pf_parse(buf, (size_t)size, out, consumed);
    free(buf);
    return status;
}

sts_pf_status sts_pf_save(const char *path, const sts_profile *profile)
{
    size_t count = sts_pf_size(profile);
    uint8_t *buf = (uint8_t *)malloc(count);
    FILE *file;
    sts_pf_status status = STS_PF_OK;

    if (!buf)
        return STS_PF_IO;
    if (sts_pf_serialize(profile, buf, count) != count) {
        free(buf);
        return STS_PF_IO;
    }
    file = fopen(path, "wb");
    if (!file || fwrite(buf, 1, count, file) != count)
        status = STS_PF_IO;
    if (file)
        fclose(file);
    free(buf);
    return status;
}
