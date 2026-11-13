#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "state.h"

#define STATE_FORMAT_VERSION 1
#define CHARACTER_LIMIT 64   /* well past the game's characters, for a file someone edited */

void sts_state_init(sts_state *state)
{
    memset(state, 0, sizeof *state);
    state->active = -1;
}

static int find_count(const sts_state *state, int character, uint32_t rules_hash)
{
    int wau;

    for (wau = 0; wau < state->count; wau++)
        if (state->counts[wau].character == character && state->counts[wau].rules_hash == rules_hash)
            return wau;
    return -1;
}

int sts_state_restarts(const sts_state *state, int character, uint32_t rules_hash)
{
    int found = find_count(state, character, rules_hash);

    return found >= 0 ? state->counts[found].restarts : 0;
}

void sts_state_add_restart(sts_state *state, int character, uint32_t rules_hash)
{
    int found = find_count(state, character, rules_hash);
    sts_restart_count entry;

    entry.character = character;
    entry.rules_hash = rules_hash;
    entry.restarts = 0;
    if (found >= 0) {
        entry = state->counts[found];
        memmove(state->counts + found, state->counts + found + 1, sizeof state->counts[0] * (size_t)(state->count - found - 1));
        state->count--;
    } else if (state->count == STS_STATE_COUNTS_MAX) {
        memmove(state->counts, state->counts + 1, sizeof state->counts[0] * (STS_STATE_COUNTS_MAX - 1));
        state->count--;
    }
    /* The most recent goes last, so the oldest is the one forgotten. */
    entry.restarts++;
    state->counts[state->count++] = entry;
}

static int found_set_ok(int owner, unsigned id)
{
    return owner >= 0 && owner < STS_STATE_FOUND_SETS && id < STS_STATE_CARD_IDS;
}

int sts_state_found(const sts_state *state, int owner, unsigned id)
{
    return found_set_ok(owner, id) && (state->found[owner][id / 8] >> (id % 8)) & 1;
}

int sts_state_add_found(sts_state *state, int owner, unsigned id)
{
    if (!found_set_ok(owner, id) || sts_state_found(state, owner, id))
        return 0;
    state->found[owner][id / 8] |= (uint8_t)(1u << (id % 8));
    return 1;
}

static int set_has_found(const sts_state *state, int owner)
{
    unsigned id;

    for (id = 0; id < STS_STATE_CARD_IDS; id++)
        if (sts_state_found(state, owner, id))
            return 1;
    return 0;
}

size_t sts_state_serialize(const sts_state *state, char *buf, size_t cap)
{
    size_t offset = 0;
    int sets = 0, owner, wau;
    unsigned id;

#define PUT(...) do { \
        int written = snprintf(buf + offset, cap - offset, __VA_ARGS__); \
        if (written < 0 || (size_t)written >= cap - offset) return 0; \
        offset += (size_t)written; \
    } while (0)

    if (cap == 0)
        return 0;
    PUT("sts_state %d\n", STATE_FORMAT_VERSION);
    PUT("active %d\n", state->active);
    /* One line each: character, rules fingerprint, runs started over. */
    PUT("restarts %d\n", state->count);
    for (wau = 0; wau < state->count; wau++)
        PUT("%d %08x %d\n", state->counts[wau].character, (unsigned)state->counts[wau].rules_hash,
            state->counts[wau].restarts);
    /* One line for each set of cards found that has any: the owner, then the IDs. */
    for (owner = 0; owner < STS_STATE_FOUND_SETS; owner++)
        sets += set_has_found(state, owner);
    PUT("found %d\n", sets);
    for (owner = 0; owner < STS_STATE_FOUND_SETS; owner++) {
        if (!set_has_found(state, owner))
            continue;
        PUT("%d", owner);
        for (id = 0; id < STS_STATE_CARD_IDS; id++)
            if (sts_state_found(state, owner, id))
                PUT(" %u", id);
        PUT("\n");
    }
    PUT("gallery_unlocked %d\n", state->gallery_unlocked ? 1 : 0);
#undef PUT
    return offset;
}

/* One line of cards found: the owner, then its card IDs. Returns where the next line starts, or NULL
 * if the line is damaged. */
static char *parse_found_line(char *cursor, sts_state *out)
{
    char *line_end = strchr(cursor, '\n');
    char *number_end;
    long owner, id;

    if (!line_end)
        line_end = cursor + strlen(cursor);
    owner = strtol(cursor, &number_end, 10);
    if (number_end == cursor || owner < 0 || owner >= STS_STATE_FOUND_SETS)
        return NULL;
    cursor = number_end;
    while (cursor < line_end) {
        id = strtol(cursor, &number_end, 10);
        if (number_end == cursor || number_end > line_end || id < 0 || id >= STS_STATE_CARD_IDS)
            return NULL;
        sts_state_add_found(out, (int)owner, (unsigned)id);
        cursor = number_end;
        while (cursor < line_end && *cursor == ' ')
            cursor++;
    }
    return *cursor == '\n' ? cursor + 1 : cursor;
}

int sts_state_parse(const char *text, size_t len, sts_state *out)
{
    char *copy = (char *)malloc(len + 1);
    char *cursor;
    int version = 0, count = 0, consumed = 0, wau;

    sts_state_init(out);
    if (!copy)
        return 0;
    memcpy(copy, text, len);
    copy[len] = 0;
    cursor = copy;
    if (sscanf(cursor, "sts_state %d\nactive %d\nrestarts %d\n%n", &version, &out->active, &count, &consumed) != 3
        || version != STATE_FORMAT_VERSION || out->active < -1 || out->active >= CHARACTER_LIMIT
        || count < 0 || count > STS_STATE_COUNTS_MAX)
        goto bad;
    cursor += consumed;
    for (wau = 0; wau < count; wau++) {
        unsigned rules_hash;
        sts_restart_count *entry = &out->counts[wau];

        consumed = 0;
        if (sscanf(cursor, "%d %8x %d\n%n", &entry->character, &rules_hash, &entry->restarts, &consumed) != 3
            || consumed == 0 || entry->character < 0 || entry->character >= CHARACTER_LIMIT || entry->restarts < 0)
            goto bad;
        entry->rules_hash = (uint32_t)rules_hash;
        cursor += consumed;
    }
    out->count = count;

    /* The cards found, if the file has them. */
    consumed = 0;
    if (sscanf(cursor, "found %d\n%n", &count, &consumed) == 1 && consumed > 0) {
        if (count < 0 || count > STS_STATE_FOUND_SETS)
            goto bad;
        cursor += consumed;
        for (wau = 0; wau < count; wau++)
            if (!(cursor = parse_found_line(cursor, out)))
                goto bad;
        consumed = 0;
        if (sscanf(cursor, "gallery_unlocked %d\n%n", &out->gallery_unlocked, &consumed) == 1 && consumed > 0
            && out->gallery_unlocked != 0 && out->gallery_unlocked != 1)
            goto bad;
    }
    free(copy);
    return 1;

bad:
    free(copy);
    sts_state_init(out);
    return 0;
}
