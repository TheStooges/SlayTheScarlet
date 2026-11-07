/* Read-only logging of netplay, to learn what reaches the match online. Nothing here changes
 * a packet or the game's state. */
#ifndef STS_NETLOG_H
#define STS_NETLOG_H

/* Hooks sendto and recvfrom. Call while the game's read-only data is writable. */
void netlog_install(void);

/* Records which scene or menu is running, when it changes. Called every frame by the hooks. */
void netlog_scene(const char *name);

#endif
