/* Each player's deck changes between matches, online: sent by the players' mods (peers.h), and shown on
 * a spectator's screen. Draws only, and sends nothing but the mod's own packets, so the match cannot
 * change. */
#ifndef STS_SPECTATOR_H
#define STS_SPECTATOR_H

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

/* Every frame of an online player's scenes: sends the run deck's change once a reward has made one. */
void spectator_player_frame(void);

/* Every frame of the spectator's loading screen (loading 1) or battle (loading 0), after the game has
 * drawn it. */
void spectator_render(int loading);

#endif
