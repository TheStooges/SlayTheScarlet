/* The draft screen, drawn over the title scene. Everything here runs on the game's
 * main thread, from the title scene's onProcess and onRender hooks. */
#ifndef STS_SCREEN_H
#define STS_SCREEN_H

#include <SokuLib.hpp>

/* Sets the folder holding cards.csv, settings.ini and run saves. Ends in a backslash. */
void screen_set_dir(const char *dir);

int screen_is_open(void);
void screen_open(void);

/* Handles one frame of input. */
void screen_update(const SokuLib::KeyInput &input);

/* Draws on top of whatever the game has drawn this frame. */
void screen_render(void);

/* Run deck upkeep, called every frame of the matching scene. The title frame only
 * records changes to the slot: the deck editor must keep seeing the player's real deck. */
void deck_on_title_frame(void);
void deck_on_select_frame(void);
void deck_on_battle_frame(void);

#endif
