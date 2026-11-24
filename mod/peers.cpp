/* See peers.h. Addresses are kept only in memory and never logged: peers are named by their relation.
 *
 * The mod's packets all start: u8 type (0x77), "STS", u8 protocol, u8 kind. A hello then has u8 flags
 * (bit 0: I have heard from you; bit 1: I am a spectator; bit 3: I share deck changes with my opponent)
 * and u32 the hash of my settings, for checking later that both players have the same rules (bit 2 says
 * it is known: the settings loaded).
 *
 * A deck change: u8 side (0 the host's deck, 1 the client's), u8 character, u8 serial, u8 flags (bit 0:
 * the sender lets the opponent see it; bit 1: the sender has finished with the screen after the match,
 * so this is all the change there will be), then u8 count and u16 card IDs for the cards in, and the same
 * for the cards out. Its acknowledgement: u8 side, u8 serial. It carries no match number: between the
 * players, GAME_MATCH's match byte does not count up (seen as 229 and 36 in every match), so a change
 * is for the next match after it arrives.
 *
 * Changes are acknowledged hop by hop, as the game does with GAME_MATCH: sent once to each peer with
 * the mod, sent again only while that peer has not acknowledged it, at a growing gap, and no more once
 * a GAME_MATCH goes by (the next match is starting, and its decks carry the change). A change is passed on to every peer with the mod except the one it
 * came from, so the host's reaches the client and the client sends it on to its spectators. */
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
#define HELLO_SHARES 8
#define KIND_CHANGE 2
#define KIND_CHANGE_ACK 3
#define CHANGE_ACK_LENGTH 8
#define CHANGE_OPPONENT_MAY_SEE 1
#define CHANGE_FINISHED 2
/* No reward brings more than 4 copies, but owed rewards add up, so leave room. */
#define CHANGE_MAX_CARDS 40
#define CHANGE_MAX_LENGTH (12 + 4 * CHANGE_MAX_CARDS)
#define CHANGE_FIRST_GAP_MS 500
#define CHANGE_MAX_GAP_MS 4000
#define CHANGE_MAX_SENDS 12

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
    int shares;          /* shares deck changes with its opponent */
    uint32_t settings_hash;
} peer;

static CRITICAL_SECTION g_lock;
static peer g_peers[MAX_PEERS];
static sendto_fn g_send = NULL;
static int g_send_nothing = 0;
/* This machine's settings hash, set by the game's thread (the settings are not read from this one). */
static volatile LONG g_own_hash = 0;
static volatile LONG g_own_hash_known = 0;
static volatile LONG g_own_shares = 0;

/* The latest change of each side's deck still being delivered, and how far each peer has got with it. */
typedef struct {
    int used;
    peer_deck_change change;
    unsigned char packet[CHANGE_MAX_LENGTH];
    int length;
    int from_peer;              /* the peer it came from, or -1 for this machine's own */
    int acknowledged[MAX_PEERS];
    int sends[MAX_PEERS];
    DWORD last_sent[MAX_PEERS];
    DWORD gap[MAX_PEERS];
} change_in_flight;

static change_in_flight g_flights[2];
/* The newest change seen of each side, kept after delivery stops, so a late copy is not taken as new. */
static int g_seen[2];
static int g_seen_serial[2];

static void on_change(peer *from, const unsigned char *packet, int len);
static void on_change_ack(peer *from, const unsigned char *packet, int len);
static int is_game_match(const unsigned char *packet, int len);
static void stop_changes(void);
static void pump_changes(DWORD now);
static void write_header(unsigned char *packet, int kind);

/* A peer slot is starting over (a new peer, or the same one again): it has had none of the changes. */
static void forget_peer_in_flights(int index)
{
    int side;

    for (side = 0; side < 2; side++) {
        g_flights[side].acknowledged[index] = 0;
        g_flights[side].sends[index] = 0;
        g_flights[side].gap[index] = 0;
        if (g_flights[side].from_peer == index)
            g_flights[side].from_peer = -2;
    }
}

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
    forget_peer_in_flights((int)(free_slot - g_peers));
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
    slog("peers: the %s has the mod (%s, %s%s)", relation_name(known->relation),
         known->spectator ? "a spectator" : "a player", rules, known->shares ? ", shares deck changes" : "");
}

static void stop_changes(void);

/* No peer left: the connection is over. A change still being sent belongs to it, and so does the newest
 * serial seen of each side; the next connection starts afresh (seen 2026-11-24: an undelivered change
 * went to the next opponent on arrival). */
static void end_connection(void)
{
    int wau;

    for (wau = 0; wau < MAX_PEERS; wau++)
        if (g_peers[wau].used)
            return;
    stop_changes();
    g_seen[0] = g_seen[1] = 0;
    changes_on_connection_end();
}

static void forget_peer(peer *gone, const char *why)
{
    if (gone->heard_from)
        slog("peers: the %s with the mod %s", relation_name(gone->relation), why);
    gone->used = 0;
    end_connection();
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

    write_header(hello, KIND_HELLO);
    hello[6] = (unsigned char)((to->heard_from ? HELLO_HEARD_YOU : 0)
                               | (SokuLib::mainMode == SokuLib::BATTLE_MODE_VSWATCH ? HELLO_SPECTATOR : 0)
                               | (g_own_hash_known ? HELLO_SETTINGS_KNOWN : 0)
                               | (g_own_shares ? HELLO_SHARES : 0));
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
    if (is_game_match(packet, len))
        stop_changes();
    pump_changes(now);
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
    from->shares = (packet[6] & HELLO_SHARES) != 0;
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
        else if (own && packet[5] == KIND_CHANGE)
            on_change(known, packet, len);
        else if (own && packet[5] == KIND_CHANGE_ACK)
            on_change_ack(known, packet, len);
        else
            maybe_announce(known);
    }
    if (is_game_match(packet, len))
        stop_changes();
    pump_changes(now);
    LeaveCriticalSection(&g_lock);
    return own;
}

void peers_set_settings(uint32_t hash, int known, int shares)
{
    InterlockedExchange(&g_own_hash, (LONG)hash);
    InterlockedExchange(&g_own_hash_known, known);
    InterlockedExchange(&g_own_shares, shares);
}

/* The opponent: the client for the host (its child that is not a spectator), the host for the client.
 * Returns the peer, or NULL. Call with the lock held. */
static peer *opponent_peer(void)
{
    peer_relation relation;
    int wau;

    if (SokuLib::mainMode == SokuLib::BATTLE_MODE_VSCLIENT)
        relation = PEER_CHILD;
    else if (SokuLib::mainMode == SokuLib::BATTLE_MODE_VSSERVER)
        relation = PEER_PARENT;
    else
        return NULL;
    for (wau = 0; wau < MAX_PEERS; wau++)
        if (g_peers[wau].used && g_peers[wau].relation == relation && g_peers[wau].heard_from
            && g_peers[wau].same_protocol && !g_peers[wau].spectator)
            return &g_peers[wau];
    return NULL;
}

int peers_opponent_rules_differ(void)
{
    peer *opponent;
    int differ;

    EnterCriticalSection(&g_lock);
    opponent = opponent_peer();
    differ = opponent && opponent->settings_known && g_own_hash_known
        && opponent->settings_hash != (uint32_t)g_own_hash;
    LeaveCriticalSection(&g_lock);
    return differ;
}

int peers_opponent_shares(void)
{
    peer *opponent;
    int found;

    EnterCriticalSection(&g_lock);
    opponent = opponent_peer();
    found = opponent && opponent->shares;
    LeaveCriticalSection(&g_lock);
    return found;
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

static void write_header(unsigned char *packet, int kind)
{
    packet[0] = MOD_PACKET_TYPE;
    packet[1] = 'S';
    packet[2] = 'T';
    packet[3] = 'S';
    packet[4] = MOD_PROTOCOL;
    packet[5] = (unsigned char)kind;
}

static int write_cards(unsigned char *packet, int offset, const sts_deck *cards)
{
    int count = cards->count < CHANGE_MAX_CARDS ? cards->count : CHANGE_MAX_CARDS, wau;

    packet[offset++] = (unsigned char)count;
    for (wau = 0; wau < count; wau++) {
        packet[offset++] = (unsigned char)cards->ids[wau];
        packet[offset++] = (unsigned char)(cards->ids[wau] >> 8);
    }
    return offset;
}

/* Returns the offset after the cards, or -1 if they do not fit. */
static int read_cards(const unsigned char *packet, int len, int offset, sts_deck *cards)
{
    int count, wau;

    if (offset >= len)
        return -1;
    count = packet[offset++];
    if (count > CHANGE_MAX_CARDS || offset + count * 2 > len)
        return -1;
    sts_deck_clear(cards);
    for (wau = 0; wau < count; wau++)
        sts_deck_add(cards, (uint16_t)(packet[offset + wau * 2] | packet[offset + wau * 2 + 1] << 8));
    return offset + count * 2;
}

static int may_hear_changes(const peer *known)
{
    return known->used && known->heard_from && known->same_protocol && !g_send_nothing;
}

/* Sends each change to each peer that still needs it, once its gap has passed. */
static void pump_changes(DWORD now)
{
    int side, wau;

    for (side = 0; side < 2; side++) {
        change_in_flight *flight = &g_flights[side];

        if (!flight->used)
            continue;
        for (wau = 0; wau < MAX_PEERS; wau++) {
            peer *to = &g_peers[wau];

            if (!may_hear_changes(to) || wau == flight->from_peer || flight->acknowledged[wau]
                || flight->sends[wau] >= CHANGE_MAX_SENDS)
                continue;
            if (flight->sends[wau] && now - flight->last_sent[wau] < flight->gap[wau])
                continue;
            flight->gap[wau] = flight->sends[wau] ? flight->gap[wau] * 2 : CHANGE_FIRST_GAP_MS;
            if (flight->gap[wau] > CHANGE_MAX_GAP_MS)
                flight->gap[wau] = CHANGE_MAX_GAP_MS;
            flight->sends[wau]++;
            flight->last_sent[wau] = now;
            g_send(to->socket_handle, (const char *)flight->packet, flight->length, 0, to->address,
                   to->address_length);
            if (flight->sends[wau] == CHANGE_MAX_SENDS)
                slog("peers: the %s never acknowledged the change of the %s's deck", relation_name(to->relation),
                     side ? "client" : "host");
        }
    }
}

/* Starts delivering a change, from this machine (from_peer -1) or from that peer. */
static void start_flight(const peer_deck_change *change, int from_peer)
{
    change_in_flight *flight = &g_flights[change->side];
    int offset;

    memset(flight, 0, sizeof(*flight));
    flight->used = 1;
    flight->change = *change;
    flight->from_peer = from_peer;
    write_header(flight->packet, KIND_CHANGE);
    flight->packet[6] = (unsigned char)change->side;
    flight->packet[7] = (unsigned char)change->character;
    flight->packet[8] = (unsigned char)change->serial;
    flight->packet[9] = (unsigned char)((change->opponent_may_see ? CHANGE_OPPONENT_MAY_SEE : 0)
                                        | (change->finished ? CHANGE_FINISHED : 0));
    offset = write_cards(flight->packet, 10, &change->added);
    flight->length = write_cards(flight->packet, offset, &change->removed);
    g_seen[change->side] = 1;
    g_seen_serial[change->side] = change->serial;
    pump_changes(GetTickCount());
}

void peers_send_change(const peer_deck_change *change)
{
    if (!g_send || g_send_nothing || change->side < 0 || change->side > 1)
        return;
    EnterCriticalSection(&g_lock);
    start_flight(change, -1);
    LeaveCriticalSection(&g_lock);
}

static void on_change(peer *from, const unsigned char *packet, int len)
{
    unsigned char ack[CHANGE_ACK_LENGTH];
    peer_deck_change change;
    int offset, side;

    if (len < 12 || !from->same_protocol)
        return;
    memset(&change, 0, sizeof change);
    change.side = side = packet[6] & 1;
    change.character = packet[7];
    change.serial = packet[8];
    change.opponent_may_see = (packet[9] & CHANGE_OPPONENT_MAY_SEE) != 0;
    change.finished = (packet[9] & CHANGE_FINISHED) != 0;
    offset = read_cards(packet, len, 10, &change.added);
    if (offset < 0 || read_cards(packet, len, offset, &change.removed) < 0)
        return;

    /* Every copy is acknowledged, so a lost acknowledgement is answered by the next one. */
    write_header(ack, KIND_CHANGE_ACK);
    ack[6] = (unsigned char)side;
    ack[7] = (unsigned char)change.serial;
    if (!g_send_nothing)
        g_send(from->socket_handle, (const char *)ack, CHANGE_ACK_LENGTH, 0, from->address, from->address_length);

    /* A sender only resends its newest change, so any other serial is a new one. */
    if (g_seen[side] && change.serial == g_seen_serial[side])
        return;
    slog("peers: the %s sent change %d of the %s's deck (%d in, %d out%s)", relation_name(from->relation),
         change.serial, side ? "client" : "host", change.added.count, change.removed.count,
         change.finished ? ", finished" : "");
    start_flight(&change, (int)(from - g_peers));
    changes_on_received(&change);
}

static void on_change_ack(peer *from, const unsigned char *packet, int len)
{
    change_in_flight *flight;

    if (len < CHANGE_ACK_LENGTH)
        return;
    flight = &g_flights[packet[6] & 1];
    if (flight->used && flight->change.serial == packet[7]
        && !flight->acknowledged[from - g_peers]) {
        flight->acknowledged[from - g_peers] = 1;
        slog("peers: the %s acknowledged the change of the %s's deck (sent %d times)", relation_name(from->relation),
             (packet[6] & 1) ? "client" : "host", flight->sends[from - g_peers]);
    }
}

static int is_game_match(const unsigned char *packet, int len)
{
    return len >= 2 && (packet[0] == SokuLib::HOST_GAME || packet[0] == SokuLib::CLIENT_GAME)
        && packet[1] == SokuLib::GAME_MATCH;
}

/* A change is made after a match ends, so the next GAME_MATCH is the next match starting: its decks
 * carry the change, and delivery stops. */
static void stop_changes(void)
{
    g_flights[0].used = 0;
    g_flights[1].used = 0;
}
