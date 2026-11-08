/* See netlog.h. The log never holds addresses or raw packet bytes, because some packets carry
 * IPs: only packet types, lengths and the decks in GAME_MATCH. */
#include <SokuLib.hpp>
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "netlog.h"
#include "log.h"
#include "run_deck.h"

/* The game's import table entries (v1.10a), found by the OnlinePractice mod's log. */
#define IAT_SENDTO   0x00857290u
#define IAT_RECVFROM 0x00857280u

typedef int (__stdcall *sendto_fn)(UINT_PTR, const char *, int, int, const void *, int);
typedef int (__stdcall *recvfrom_fn)(UINT_PTR, char *, int, int, void *, int *);

static sendto_fn g_og_sendto = NULL;
static recvfrom_fn g_og_recvfrom = NULL;

/* Packets are handled on more than one thread. */
static CRITICAL_SECTION g_lock;

static int compare_ids(const void *first, const void *second)
{
    return (int)*(const unsigned short *)first - (int)*(const unsigned short *)second;
}

/* Which deck of a profile holds these cards, in any order. The packet's own character and deck bytes
 * cannot be trusted (a client's own part carries junk there), so every deck is searched. Writes the
 * first match to character and slot and returns 1, or returns 0. Reads game memory, so a bad read
 * counts as no. */
static int find_in_profile(int profile, const unsigned short *ids, int count, int *character, int *slot)
{
    unsigned short sorted[256], other[256];
    int deck, wau;

    if (count > 256)
        return 0;
    memcpy(sorted, ids, (size_t)count * 2);
    qsort(sorted, (size_t)count, 2, compare_ids);
    for (deck = 0; deck < 20 * 4; deck++) {
        __try {
            auto &deque = (profile == 1 ? SokuLib::profile1 : SokuLib::profile2).cards[deck / 4][deck % 4];

            if (deque.size != count)
                continue;
            for (wau = 0; wau < count; wau++)
                other[wau] = deque[wau];
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
        qsort(other, (size_t)count, 2, compare_ids);
        if (memcmp(sorted, other, (size_t)count * 2) == 0) {
            *character = deck / 4;
            *slot = deck % 4;
            return 1;
        }
    }
    return 0;
}

static void profile_text(char *out, int profile, const unsigned short *ids, int count)
{
    int character, slot;

    if (count > 0 && find_in_profile(profile, ids, count, &character, &slot))
        sprintf(out, "character %d deck %d", character, slot);
    else
        strcpy(out, "no");
}

/* Logs one player's part of GAME_MATCH. Returns the offset after it, or -1 if it does not fit. */
static int log_match_player(const char *dir, const char *who, const unsigned char *packet, int len, int offset)
{
    unsigned short ids[256];
    char text[1100], in_profile1[40], in_profile2[40];
    size_t used = 0;
    int character, skin, slot, count, wau;

    if (offset + 4 > len)
        return -1;
    character = packet[offset];
    skin = packet[offset + 1];
    slot = packet[offset + 2];
    count = packet[offset + 3];
    offset += 4;
    if (offset + count * 2 + 1 > len)
        return -1;
    text[0] = 0;
    for (wau = 0; wau < count; wau++) {
        ids[wau] = (unsigned short)(packet[offset + wau * 2] | (packet[offset + wau * 2 + 1] << 8));
        used += (size_t)sprintf(text + used, "%u ", (unsigned)ids[wau]);
    }
    slog("%s GAME_MATCH %s: character %d skin %d deck %d, %d cards: %s", dir, who, character, skin, slot, count, text);
    profile_text(in_profile1, 1, ids, count);
    profile_text(in_profile2, 2, ids, count);
    slog("   in profile1: %s. in profile2: %s. the run deck in use: %s",
         in_profile1, in_profile2, count > 0 && deck_is_active(ids, count) ? "yes" : "no");
    return offset + count * 2 + 1;
}

static uint32_t hash_bytes(const unsigned char *bytes, int len)
{
    uint32_t hash = 2166136261u;
    int wau;

    for (wau = 0; wau < len; wau++) {
        hash ^= bytes[wau];
        hash *= 16777619u;
    }
    return hash;
}

static void analyze(const char *dir, const unsigned char *packet, int len)
{
    static unsigned seen[128];
    static int seen_count = 0;
    static uint32_t last_match[2] = {0, 0};
    int sending = dir[0] == 'S';
    int type, inner = -1, wau;
    unsigned key;

    if (len < 1)
        return;
    type = packet[0];
    if ((type == SokuLib::HOST_GAME || type == SokuLib::CLIENT_GAME) && len >= 2)
        inner = packet[1];

    EnterCriticalSection(&g_lock);

    /* The first of each kind of packet: which kinds exist, and which are free for a mod to use. */
    key = (unsigned)sending << 16 | (unsigned)type << 8 | (unsigned)(inner & 0xFF);
    for (wau = 0; wau < seen_count && seen[wau] != key; wau++) {}
    if (wau == seen_count && seen_count < 128) {
        seen[seen_count++] = key;
        if (inner >= 0)
            slog("%s first packet of type 0x%02X, event 0x%02X, length %d", dir, type, inner, len);
        else
            slog("%s first packet of type 0x%02X, length %d", dir, type, len);
    }

    /* GAME_MATCH is resent until acknowledged, so only log it when it changes. */
    if (inner == SokuLib::GAME_MATCH) {
        uint32_t hash = hash_bytes(packet, len);

        if (hash != last_match[sending]) {
            int offset = 2;

            last_match[sending] = hash;
            offset = log_match_player(dir, "host", packet, len, offset);
            if (offset >= 0)
                offset = log_match_player(dir, "client", packet, len, offset);
            if (offset >= 0 && offset + 7 <= len)
                slog("   stage %d, music %d, match %d", packet[offset], packet[offset + 1], packet[offset + 6]);
            else if (offset < 0)
                slog("   did not fit the expected layout, length %d", len);
        }
    }
    LeaveCriticalSection(&g_lock);
}

static int __stdcall hook_sendto(UINT_PTR socket_handle, const char *buf, int len, int flags, const void *to, int tolen)
{
    __try {
        if (buf && len > 0)
            analyze("SEND", (const unsigned char *)buf, len);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return g_og_sendto(socket_handle, buf, len, flags, to, tolen);
}

static int __stdcall hook_recvfrom(UINT_PTR socket_handle, char *buf, int len, int flags, void *from, int *fromlen)
{
    int received = g_og_recvfrom(socket_handle, buf, len, flags, from, fromlen);

    __try {
        if (received > 0 && buf)
            analyze("RECV", (const unsigned char *)buf, received);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return received;
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
