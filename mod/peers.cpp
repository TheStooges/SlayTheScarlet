/* See peers.h. Addresses are kept only in memory and never logged: peers are named by their relation.
 *
 * The mod's packets all start: u8 type (0x77), "STS", u8 protocol, u8 kind. A hello then has u8 flags
 * (bit 0: I have heard from you; bit 1: I am a spectator) and u32 the hash of my settings, for checking
 * later that both players have the same rules (bit 2 says it is known: the settings loaded). */
#include <SokuLib.hpp>
#include <windows.h>
#include <string.h>
#include "internal.h"
#include "peers.h"

/* In the range Giuroll drops (0x6F-0x80), so machines without the mod never see it, and clear of the
 * bytes Giuroll uses itself (0x6B-0x6E). The three letters after it keep it apart from any other mod
 * that picks the same byte. */
#define MOD_PACKET_TYPE 0x77
#define MOD_PROTOCOL 1
#define KIND_HELLO 1
#define HELLO_LENGTH 11
#define HELLO_HEARD_YOU 1
#define HELLO_SPECTATOR 2
#define HELLO_SETTINGS_KNOWN 4

/* Hellos go out with the game's own packets to that peer, at most this often, and stop after this many
 * without an answer: that peer does not have the mod. Its own hello still gets an answer later. */
#define HELLO_INTERVAL_MS 500
#define HELLO_TRIES 10
/* A peer not heard from for this long has gone (the game's CHAIN packets come every 0.5 s). */
#define PEER_TIMEOUT_MS 15000
#define MAX_PEERS 8

typedef int (__stdcall *sendto_fn)(UINT_PTR, const char *, int, int, const void *, int);

typedef struct {
    int used;
    unsigned char address[16];
    int address_length;
    UINT_PTR socket_handle;
    peer_relation relation;
    DWORD last_heard;
    DWORD last_hello;
    int hellos_sent;
    int gave_up;
    int heard_from;      /* a hello came from it */
    int heard_us;        /* its last hello said it has heard from us */
    int announced;       /* logged as having the mod */
    int same_protocol;
    int spectator;
    int settings_known;
    uint32_t settings_hash;
} peer;

static CRITICAL_SECTION g_lock;
static peer g_peers[MAX_PEERS];
static sendto_fn g_send = NULL;
static int g_send_nothing = 0;
/* This machine's settings hash, set by the game's thread (the settings are not read from this one). */
static volatile LONG g_own_hash = 0;
static volatile LONG g_own_hash_known = 0;

static const char *relation_name(peer_relation relation)
{
    if (relation == PEER_PARENT)
        return "parent";
    if (relation == PEER_CHILD)
        return "child";
    return "peer";
}

/* An IPv4 address: the family, port, and address are its first 8 bytes. */
static int same_address(const peer *known, const void *address, int length)
{
    return length >= 8 && known->address_length >= 8 && memcmp(known->address, address, 8) == 0;
}

static peer *find_peer(const void *address, int length, int create)
{
    int wau;
    peer *free_slot = NULL;

    if (!address || length < 8 || length > 16)
        return NULL;
    for (wau = 0; wau < MAX_PEERS; wau++) {
        if (g_peers[wau].used && same_address(&g_peers[wau], address, length))
            return &g_peers[wau];
        if (!g_peers[wau].used && !free_slot)
            free_slot = &g_peers[wau];
    }
    if (!create || !free_slot)
        return NULL;
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->used = 1;
    memcpy(free_slot->address, address, (size_t)length);
    free_slot->address_length = length;
    free_slot->last_heard = GetTickCount();
    return free_slot;
}

/* Logs a peer with the mod once its relation is known: a hello can come before the game's packets. */
static void maybe_announce(peer *known)
{
    const char *rules;

    if (!known->heard_from || known->announced || known->relation == PEER_UNKNOWN)
        return;
    known->announced = 1;
    if (!known->same_protocol) {
        slog("peers: the %s has the mod with another protocol, so nothing more is sent to it",
             relation_name(known->relation));
        return;
    }
    if (!known->settings_known || !g_own_hash_known)
        rules = "rules not known";
    else if (known->settings_hash == (uint32_t)g_own_hash)
        rules = "the same rules";
    else
        rules = "different rules";
    slog("peers: the %s has the mod (%s, %s)", relation_name(known->relation),
         known->spectator ? "a spectator" : "a player", rules);
}

static void forget_peer(peer *gone, const char *why)
{
    if (gone->heard_from)
        slog("peers: the %s with the mod %s", relation_name(gone->relation), why);
    gone->used = 0;
}

static void forget_quiet_peers(DWORD now)
{
    int wau;

    for (wau = 0; wau < MAX_PEERS; wau++)
        if (g_peers[wau].used && now - g_peers[wau].last_heard > PEER_TIMEOUT_MS)
            forget_peer(&g_peers[wau], "has gone quiet");
}

static void send_hello(peer *to)
{
    unsigned char hello[HELLO_LENGTH];
    uint32_t hash = (uint32_t)g_own_hash;

    hello[0] = MOD_PACKET_TYPE;
    hello[1] = 'S';
    hello[2] = 'T';
    hello[3] = 'S';
    hello[4] = MOD_PROTOCOL;
    hello[5] = KIND_HELLO;
    hello[6] = (unsigned char)((to->heard_from ? HELLO_HEARD_YOU : 0)
                               | (SokuLib::mainMode == SokuLib::BATTLE_MODE_VSWATCH ? HELLO_SPECTATOR : 0)
                               | (g_own_hash_known ? HELLO_SETTINGS_KNOWN : 0));
    hello[7] = (unsigned char)hash;
    hello[8] = (unsigned char)(hash >> 8);
    hello[9] = (unsigned char)(hash >> 16);
    hello[10] = (unsigned char)(hash >> 24);
    to->last_hello = GetTickCount();
    g_send(to->socket_handle, (const char *)hello, HELLO_LENGTH, 0, to->address, to->address_length);
}

/* Sends a hello if this peer still needs one: it has not answered yet, or has not said it heard us. */
static void maybe_send_hello(peer *to, DWORD now)
{
    if (g_send_nothing || to->relation == PEER_UNKNOWN || to->gave_up)
        return;
    if (to->heard_from && to->heard_us)
        return;
    if (to->hellos_sent && now - to->last_hello < HELLO_INTERVAL_MS)
        return;
    if (!to->heard_from && to->hellos_sent >= HELLO_TRIES) {
        to->gave_up = 1;
        slog("peers: the %s did not answer, so it does not have the mod", relation_name(to->relation));
        return;
    }
    to->hellos_sent++;
    send_hello(to);
}

void peers_install(int send_nothing, void *original_sendto)
{
    InitializeCriticalSection(&g_lock);
    g_send_nothing = send_nothing;
    g_send = (sendto_fn)original_sendto;
}

void peers_on_send(UINT_PTR socket_handle, const unsigned char *packet, int len, const void *to, int tolen)
{
    DWORD now = GetTickCount();
    peer *known;
    int type;

    if (len < 1 || !g_send)
        return;
    type = packet[0];
    EnterCriticalSection(&g_lock);
    forget_quiet_peers(now);
    /* Only peers the game exchanges game packets with: not the connection attempts before. */
    known = find_peer(to, tolen, type == SokuLib::HOST_GAME || type == SokuLib::CLIENT_GAME);
    if (known) {
        known->socket_handle = socket_handle;
        if (type == SokuLib::HOST_GAME)
            known->relation = PEER_CHILD;
        else if (type == SokuLib::CLIENT_GAME)
            known->relation = PEER_PARENT;
        if (type == SokuLib::QUIT) {
            forget_peer(known, "was left");
        } else {
            maybe_announce(known);
            maybe_send_hello(known, now);
        }
    }
    LeaveCriticalSection(&g_lock);
}

static void on_hello(UINT_PTR socket_handle, peer *from, const unsigned char *packet, int len)
{
    uint32_t hash;

    if (len < HELLO_LENGTH)
        return;
    hash = (uint32_t)packet[7] | (uint32_t)packet[8] << 8 | (uint32_t)packet[9] << 16 | (uint32_t)packet[10] << 24;
    from->socket_handle = socket_handle;
    from->heard_from = 1;
    from->gave_up = 0;
    from->heard_us = (packet[6] & HELLO_HEARD_YOU) != 0;
    from->spectator = (packet[6] & HELLO_SPECTATOR) != 0;
    from->same_protocol = packet[4] == MOD_PROTOCOL;
    from->settings_known = (packet[6] & HELLO_SETTINGS_KNOWN) != 0;
    from->settings_hash = hash;
    maybe_announce(from);
    /* Answer at once while it has not heard from us, so neither side stops before the other knows. */
    if (!g_send_nothing && !(packet[6] & HELLO_HEARD_YOU))
        send_hello(from);
}

int peers_on_receive(UINT_PTR socket_handle, const unsigned char *packet, int len, const void *from, int fromlen)
{
    DWORD now = GetTickCount();
    peer *known;
    int type, own;

    if (len < 1 || !g_send)
        return 0;
    type = packet[0];
    own = type == MOD_PACKET_TYPE && len >= 6 && packet[1] == 'S' && packet[2] == 'T' && packet[3] == 'S';
    EnterCriticalSection(&g_lock);
    forget_quiet_peers(now);
    known = find_peer(from, fromlen, own || type == SokuLib::HOST_GAME || type == SokuLib::CLIENT_GAME);
    if (known) {
        known->last_heard = now;
        known->socket_handle = socket_handle;
        if (type == SokuLib::HOST_GAME)
            known->relation = PEER_PARENT;
        else if (type == SokuLib::CLIENT_GAME)
            known->relation = PEER_CHILD;
        if (type == SokuLib::QUIT)
            forget_peer(known, "has left");
        else if (own && packet[5] == KIND_HELLO)
            on_hello(socket_handle, known, packet, len);
        else
            maybe_announce(known);
    }
    LeaveCriticalSection(&g_lock);
    return own;
}

void peers_set_settings(uint32_t hash, int known)
{
    InterlockedExchange(&g_own_hash, (LONG)hash);
    InterlockedExchange(&g_own_hash_known, known);
}

int peers_has_mod(peer_relation relation)
{
    int wau, found = 0;

    EnterCriticalSection(&g_lock);
    for (wau = 0; wau < MAX_PEERS && !found; wau++)
        found = g_peers[wau].used && g_peers[wau].relation == relation && g_peers[wau].heard_from
            && g_peers[wau].same_protocol;
    LeaveCriticalSection(&g_lock);
    return found;
}
