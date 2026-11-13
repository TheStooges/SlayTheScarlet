#include <string.h>
#include "pf.h"
#include "sounds.h"

/* The game's folder for each character, in character order (data/se/<folder>/). */
static const char *const g_folders[STS_NUM_CHARACTERS] = {
    "reimu", "marisa", "sakuya", "alice", "patchouli", "youmu", "remilia", "yuyuko", "yukari", "suika",
    "udonge", "aya", "komachi", "iku", "tenshi", "sanae", "chirno", "meirin", "utsuho", "suwako"
};

const char *sts_sound_folder_name(int folder)
{
    if (folder == STS_SOUND_COMMON)
        return "common";
    if (folder >= 0 && folder < STS_NUM_CHARACTERS)
        return g_folders[folder];
    return NULL;
}

/* Digits only, from text up to end, at most max. */
static int parse_number(const char *text, const char *end, int max, int *out)
{
    int value = 0;

    if (text >= end)
        return 0;
    for (; text < end; text++) {
        if (*text < '0' || *text > '9')
            return 0;
        value = value * 10 + (*text - '0');
        if (value > max)
            return 0;
    }
    *out = value;
    return 1;
}

static int folder_of(const char *text, size_t length, int *out)
{
    int wau;

    if (length == 6 && memcmp(text, "common", 6) == 0) {
        *out = STS_SOUND_COMMON;
        return 1;
    }
    for (wau = 0; wau < STS_NUM_CHARACTERS; wau++) {
        if (strlen(g_folders[wau]) == length && memcmp(text, g_folders[wau], length) == 0) {
            *out = wau;
            return 1;
        }
    }
    return 0;
}

static int owner_of(const char *text, int *out)
{
    int wau;

    if (strcmp(text, "system") == 0) {
        *out = STS_SOUND_SYSTEM;
        return 1;
    }
    for (wau = 0; wau < STS_NUM_CHARACTERS; wau++) {
        if (strcmp(text, sts_character_name(wau)) == 0) {
            *out = wau;
            return 1;
        }
    }
    return 0;
}

/* One play, "folder/number@frame", from text up to end. */
static int parse_play(const char *text, const char *end, sts_sound_play *out)
{
    const char *slash = memchr(text, '/', (size_t)(end - text));
    const char *at = slash ? memchr(slash, '@', (size_t)(end - slash)) : NULL;
    int folder, number, frame;

    if (!slash || !at || !folder_of(text, (size_t)(slash - text), &folder)
        || !parse_number(slash + 1, at, 999, &number) || !parse_number(at + 1, end, 65535, &frame))
        return 0;
    out->folder = (int8_t)folder;
    out->number = (uint16_t)number;
    out->frame = (uint16_t)frame;
    return 1;
}

static sts_sounds_status parse_line(char *line, sts_sound_table *table)
{
    char *first_comma = strchr(line, ',');
    char *second_comma = first_comma ? strchr(first_comma + 1, ',') : NULL;
    char *text, *end;
    int owner, id;
    sts_sound_row *row;

    if (!second_comma || strchr(second_comma + 1, ','))
        return STS_SOUNDS_BAD_LINE;
    *first_comma = 0;
    if (!owner_of(line, &owner) || !parse_number(first_comma + 1, second_comma, 65535, &id))
        return STS_SOUNDS_BAD_LINE;
    if (sts_sounds_find(table, owner, (uint16_t)id))
        return STS_SOUNDS_DUPLICATE;
    if (table->row_count >= STS_SOUNDS_MAX_ROWS)
        return STS_SOUNDS_OVERFLOW;
    row = &table->rows[table->row_count];
    memset(row, 0, sizeof *row);
    row->owner = (int8_t)owner;
    row->id = (uint16_t)id;
    row->outcomes = 1;
    row->first[0] = (uint16_t)table->play_count;

    text = second_comma + 1;
    end = text + strlen(text);
    while (text < end) {
        char *play_end;

        while (text < end && *text == ' ')
            text++;
        if (text >= end)
            break;
        if (*text == '>') {
            /* The outcome before it must have had a sound. */
            if (row->count[row->outcomes - 1] == 0)
                return STS_SOUNDS_BAD_LINE;
            if (row->outcomes >= STS_SOUNDS_MAX_OUTCOMES)
                return STS_SOUNDS_OVERFLOW;
            row->first[row->outcomes] = (uint16_t)table->play_count;
            row->outcomes++;
            text++;
            continue;
        }
        play_end = text;
        while (play_end < end && *play_end != ' ' && *play_end != '>')
            play_end++;
        if (table->play_count >= STS_SOUNDS_MAX_PLAYS)
            return STS_SOUNDS_OVERFLOW;
        if (!parse_play(text, play_end, &table->plays[table->play_count]))
            return STS_SOUNDS_BAD_LINE;
        table->play_count++;
        row->count[row->outcomes - 1]++;
        text = play_end;
    }
    if (row->count[row->outcomes - 1] == 0)
        return STS_SOUNDS_BAD_LINE;
    table->row_count++;
    return STS_SOUNDS_OK;
}

sts_sounds_status sts_sounds_parse(const char *text, size_t len, sts_sound_table *out, int *err_line)
{
    static char line[STS_SOUNDS_MAX_LINE];
    size_t pos = 0;
    int line_no = 0;

    memset(out, 0, sizeof *out);
    if (err_line)
        *err_line = 0;
    while (pos < len) {
        size_t start = pos, length;
        sts_sounds_status status;

        while (pos < len && text[pos] != '\n')
            pos++;
        length = pos - start;
        pos++;              /* past the newline */
        line_no++;
        if (length > 0 && text[start + length - 1] == '\r')
            length--;
        if (length == 0 || text[start] == '#')
            continue;
        if (length >= STS_SOUNDS_MAX_LINE)
            status = STS_SOUNDS_OVERFLOW;
        else {
            memcpy(line, text + start, length);
            line[length] = 0;
            status = parse_line(line, out);
        }
        if (status != STS_SOUNDS_OK) {
            if (err_line)
                *err_line = line_no;
            return status;
        }
    }
    return STS_SOUNDS_OK;
}

const char *sts_sounds_status_str(sts_sounds_status status)
{
    switch (status) {
    case STS_SOUNDS_OK:        return "ok";
    case STS_SOUNDS_BAD_LINE:  return "bad line";
    case STS_SOUNDS_DUPLICATE: return "the same card twice";
    case STS_SOUNDS_OVERFLOW:  return "too many sounds";
    }
    return "unknown";
}

const sts_sound_row *sts_sounds_find(const sts_sound_table *table, int owner, uint16_t id)
{
    int wau;

    for (wau = 0; wau < table->row_count; wau++)
        if (table->rows[wau].owner == owner && table->rows[wau].id == id)
            return &table->rows[wau];
    return NULL;
}
