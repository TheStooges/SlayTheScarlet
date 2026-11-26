/* Logging of netplay, to learn what reaches the match online, and the hooks the mod's own packets go
 * through (peers.h). No packet of the game's is changed: the mod's own are hidden from the game. */
#ifndef STS_NETLOG_H
#define STS_NETLOG_H

/* Hooks sendto and recvfrom. Call while the game's read-only data is writable. The log-only observer
 * takes no part in the mod's handshake (see peers.h). */
void netlog_install(int observer);

/* The seed of the last online match, from the host's GAME_MATCH: the same on host and client.
 * Returns 0 if no match has been seen. */
int netlog_match_seed(uint32_t *seed);

/* Records which scene or menu is running, when it changes. Called every frame by the hooks. */
void netlog_scene(const char *name);

#endif
