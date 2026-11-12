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

size_t sts_state_serialize(const sts_state *state, char *buf, size_t cap)
{
    size_t offset = 0;
    int wau;

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
#undef PUT
    return offset;
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
    free(copy);
    return 1;

bad:
    free(copy);
    sts_state_init(out);
    return 0;
}
