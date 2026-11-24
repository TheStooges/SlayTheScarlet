/* Which of the machines this one talks to online have the mod: the handshake that starts full mode.
 *
 * Each machine sends a short hello to the peers it exchanges game packets with: its parent (the host,
 * for the client; the client or another spectator, for a spectator) and its children (the client, for
 * the host; spectators). A peer with the mod answers, and only such peers are ever sent anything else.
 * The hello's packet type is in the range Giuroll drops (0x6F-0x80), so a peer without the mod never
 * sees it, and a peer gives up on hellos after a few seconds without an answer. */
#ifndef STS_PEERS_H
#define STS_PEERS_H

#include <stdint.h>
#include "deck.h"

/* A change of one player's deck since the last match, as sent between machines with the mod. It goes
 * with the next match after it arrives. */
typedef struct {
    int side;              /* 0 the host's deck, 1 the client's */
    int character;
    int serial;            /* counts up with each change the sender makes, 0-255 */
    int opponent_may_see;  /* the sender lets the other player see it */
    int finished;          /* the sender is done with the screen after the match: this is the whole change */
    sts_deck added, removed;
} peer_deck_change;

/* The relation of a peer to this machine, as the game's own packets show it. */
typedef enum {
    PEER_UNKNOWN,
    PEER_PARENT,  /* sends us HOST_GAME packets */
    PEER_CHILD    /* sends us CLIENT_GAME packets */
} peer_relation;

/* Call once, before the network hooks go in, with the game's own sendto. With send_nothing set (the
 * log-only observer), the handshake is left out and this machine behaves as one without the mod. */
void peers_install(int send_nothing, void *original_sendto);

/* Every packet the game sends, before it is sent. to is the game's address structure. */
void peers_on_send(UINT_PTR socket_handle, const unsigned char *packet, int len, const void *to, int tolen);

/* Every packet received. Returns 1 if it was the mod's own, so the caller hides it from the game. */
int peers_on_receive(UINT_PTR socket_handle, const unsigned char *packet, int len, const void *from, int fromlen);

/* This machine's settings hash, for the hello, whether the settings have loaded, and whether this
 * player shares deck changes with the opponent. Called by the game's thread whenever they may have
 * changed. */
void peers_set_settings(uint32_t hash, int known, int shares);

/* 1 if this machine is a player and its opponent has the mod and shares deck changes. Stops being 1
 * when the opponent leaves. */
int peers_opponent_shares(void);

/* Delivers this machine's own deck change to every peer with the mod. A newer change of the same side
 * replaces the one being delivered. Called by the game's thread. */
void peers_send_change(const peer_deck_change *change);

/* Each change of a deck that arrives from a peer, newer than any seen of that side, once it is being
 * passed on. Called from the network thread with the peers' lock held: it must not call into peers. */
void changes_on_received(const peer_deck_change *change);

/* The connection is over (no peer left). Called with the peers' lock held: it must not call into peers. */
void changes_on_connection_end(void);

/* 1 if the peer with this relation has the mod and speaks the same protocol (the first such peer,
 * for children). */
int peers_has_mod(peer_relation relation);

#endif
