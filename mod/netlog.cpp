/* See netlog.h. The log never holds addresses or raw packet bytes, because some packets carry
 * IPs: only packet types, lengths and the decks in GAME_MATCH. */
#include <SokuLib.hpp>
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "netlog.h"
#include "screen.h"

/* The game's import table entries (v1.10a), found by the OnlinePractice mod's log. */
#define IAT_SENDTO   0x00857290u
#define IAT_RECVFROM 0x00857280u

typedef int (__stdcall *sendto_fn)(UINT_PTR, const char *, int, int, const void *, int);
typedef int (__stdcall *recvfrom_fn)(UINT_PTR, char *, int, int, void *, int *);

static sendto_fn g_og_sendto = NULL;
static recvfrom_fn g_og_recvfrom = NULL;

/* Packets are handled on more than one thread. */
static CRITICAL_SECTION g_lock;

static int compare_ids(const void *a, const void *b)
{
    return (int)*(const unsigned short *)a - (int)*(const unsigned short *)b;
}

/* Same cards, in any order? Reads game memory, so a bad read counts as no. */
static int profile_slot_matches(int profile, int character, int slot, const unsigned short *ids, int count)
{
    unsigned short sorted[256], other[256];
    int i;

    if (character < 0 || character >= 20 || slot < 0 || slot >= 4 || count > 256)
        return 0;
    memcpy(sorted, ids, (size_t)count * 2);
    qsort(sorted, (size_t)count, 2, compare_ids);
    __try {
        auto &dq = (profile == 1 ? SokuLib::profile1 : SokuLib::profile2).cards[character][slot];

        if (dq.size != count)
            return 0;
        for (i = 0; i < count; i++)
            other[i] = dq[i];
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    qsort(other, (size_t)count, 2, compare_ids);
    return memcmp(sorted, other, (size_t)count * 2) == 0;
}

/* Logs one player's part of GAME_MATCH. Returns the offset after it, or -1 if it does not fit. */
static int log_match_player(const char *dir, const char *who, const unsigned char *p, int len, int o)
{
    unsigned short ids[256];
    char text[1100];
    size_t used = 0;
    int character, skin, slot, count, i;

    if (o + 4 > len)
        return -1;
    character = p[o];
    skin = p[o + 1];
    slot = p[o + 2];
    count = p[o + 3];
    o += 4;
    if (o + count * 2 + 1 > len)
        return -1;
    text[0] = 0;
    for (i = 0; i < count; i++) {
        ids[i] = (unsigned short)(p[o + i * 2] | (p[o + i * 2 + 1] << 8));
        used += (size_t)sprintf(text + used, "%u ", (unsigned)ids[i]);
    }
    slog("%s GAME_MATCH %s: character %d skin %d deck %d, %d cards: %s", dir, who, character, skin, slot, count, text);
    slog("   in profile1 slot: %s. in profile2 slot: %s. the run deck in use: %s",
         profile_slot_matches(1, character, slot, ids, count) ? "yes" : "no",
         profile_slot_matches(2, character, slot, ids, count) ? "yes" : "no",
         deck_matches_active(character, ids, count) ? "yes" : "no");
    return o + count * 2 + 1;
}

static uint32_t hash_bytes(const unsigned char *p, int len)
{
    uint32_t h = 2166136261u;
    int i;

    for (i = 0; i < len; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

static void analyze(const char *dir, const unsigned char *p, int len)
{
    static unsigned seen[128];
    static int seen_count = 0;
    static uint32_t last_match[2] = {0, 0};
    int sending = dir[0] == 'S';
    int type, inner = -1, i;
    unsigned key;

    if (len < 1)
        return;
    type = p[0];
    if ((type == SokuLib::HOST_GAME || type == SokuLib::CLIENT_GAME) && len >= 2)
        inner = p[1];

    EnterCriticalSection(&g_lock);

    /* The first of each kind of packet: which kinds exist, and which are free for a mod to use. */
    key = (unsigned)sending << 16 | (unsigned)type << 8 | (unsigned)(inner & 0xFF);
    for (i = 0; i < seen_count && seen[i] != key; i++) {}
    if (i == seen_count && seen_count < 128) {
        seen[seen_count++] = key;
        if (inner >= 0)
            slog("%s first packet of type 0x%02X, event 0x%02X, length %d", dir, type, inner, len);
        else
            slog("%s first packet of type 0x%02X, length %d", dir, type, len);
    }

    /* GAME_MATCH is resent until acknowledged, so only log it when it changes. */
    if (inner == SokuLib::GAME_MATCH) {
        uint32_t h = hash_bytes(p, len);

        if (h != last_match[sending]) {
            int o = 2;

            last_match[sending] = h;
            o = log_match_player(dir, "host", p, len, o);
            if (o >= 0)
                o = log_match_player(dir, "client", p, len, o);
            if (o >= 0 && o + 7 <= len)
                slog("   stage %d, music %d, match %d", p[o], p[o + 1], p[o + 6]);
            else if (o < 0)
                slog("   did not fit the expected layout, length %d", len);
        }
    }
    LeaveCriticalSection(&g_lock);
}

static int __stdcall hook_sendto(UINT_PTR s, const char *buf, int len, int flags, const void *to, int tolen)
{
    __try {
        if (buf && len > 0)
            analyze("SEND", (const unsigned char *)buf, len);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return g_og_sendto(s, buf, len, flags, to, tolen);
}

static int __stdcall hook_recvfrom(UINT_PTR s, char *buf, int len, int flags, void *from, int *fromlen)
{
    int r = g_og_recvfrom(s, buf, len, flags, from, fromlen);

    __try {
        if (r > 0 && buf)
            analyze("RECV", (const unsigned char *)buf, r);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return r;
}

void netlog_install(void)
{
    InitializeCriticalSection(&g_lock);
    g_og_sendto = *(sendto_fn *)IAT_SENDTO;
    g_og_recvfrom = *(recvfrom_fn *)IAT_RECVFROM;
    *(sendto_fn *)IAT_SENDTO = hook_sendto;
    *(recvfrom_fn *)IAT_RECVFROM = hook_recvfrom;
}

void netlog_scene(const char *name)
{
    static const char *last = NULL;

    if (name == last)
        return;
    /* Online the screen after a match runs on top of the battle scene, so they alternate
     * every frame. Only the change to that screen is worth a line. */
    if (last && strstr(last, "after a match") && strstr(name, "battle"))
        return;
    last = name;
    slog("now in: %s (main mode %d, submode %d). left: character %d deck %d. right: character %d deck %d",
         name, (int)SokuLib::mainMode, (int)SokuLib::subMode,
         (int)SokuLib::leftPlayerInfo.character, (int)SokuLib::leftPlayerInfo.deck,
         (int)SokuLib::rightPlayerInfo.character, (int)SokuLib::rightPlayerInfo.deck);
}
