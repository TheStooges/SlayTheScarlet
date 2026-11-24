/* Each player's deck changes between matches, online: sent by the players' mods (peers.h), and shown on
 * a spectator's screen. Draws only, and sends nothing but the mod's own packets, so the match cannot
 * change. */
#ifndef STS_SPECTATOR_H
#define STS_SPECTATOR_H

#include <stddef.h>
#include <windows.h>
#include "deck.h"

/* Call once, before the network hooks go in. */
void spectator_install(void);

/* A received GAME_MATCH packet, whole. Only a spectator's (both players' decks) is used. Called from
 * the network thread. */
void spectator_on_match(const unsigned char *packet, int len);

/* A GAME_MATCH this machine sent. A player's holds its own deck, the starting point of its next change.
 * Called from the network thread. */
void spectator_on_sent_match(const unsigned char *packet, int len);

/* The match number in a replay request this spectator sent: the match it is watching. */
void spectator_on_replay_request(int match);

/* Every frame of an online player's scenes: sends the run deck's change once a reward has made one. At
 * character select (at_select), also says this player is finished with the screen after the match. */
void spectator_player_frame(int at_select);

/* Players seeing each other's deck changes after a match: 1 if this player and the opponent both
 * share them (and the opponent is still there). */
int share_active(void);

/* Tells the opponent this player is done with the screen after the match, with the whole change. Once
 * a match. */
void share_send_finished(void);

/* 1 once the opponent is done with the screen after the match. */
int share_opponent_finished(void);

/* The opponent left the screen after a match (their game sent 0x6E). Called from the network thread. */
void share_note_opponent_left(void);

/* 1 if the opponent left that screen at or after since (GetTickCount), at least grace ms ago. */
int share_opponent_left_since(DWORD since, DWORD grace);

/* Which side this player is on: 0 the host (player 1, on the left), 1 the client. */
int share_own_side(void);

/* A side's profile name online, as the game shows it, or empty. */
void share_profile_name(int side, char *name, size_t size);

/* This player's own change since the last match. Returns 0 if their deck did not change. */
int share_own_change(int *character, sts_deck *added, sts_deck *removed);

/* The opponent's change since the last match. Returns 0 if their deck did not change. */
int share_opponent_change(int *character, sts_deck *added, sts_deck *removed);

/* Every frame of the spectator's loading screen (loading 1) or battle (loading 0), after the game has
 * drawn it. */
void spectator_render(int loading);

#endif
