/* A spectator's notice of each player's deck changes between matches. Draws only: nothing is sent,
 * and no packet or game state changes. */
#ifndef STS_SPECTATOR_H
#define STS_SPECTATOR_H

/* Call once, before the network hooks go in. */
void spectator_install(void);

/* A received GAME_MATCH packet, whole. Only a spectator's (both players' decks) is used. Called from
 * the network thread. */
void spectator_on_match(const unsigned char *packet, int len);

/* The match number in a replay request this spectator sent: the match it is watching. */
void spectator_on_replay_request(int match);

/* Every frame of the spectator's battle scene, after the game has drawn it. */
void spectator_render(void);

#endif
