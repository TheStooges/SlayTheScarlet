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

/* Draws on top of whatever the game has drawn this frame. With opaque set it replaces the
 * frame; otherwise the game's scene shows faintly behind it. */
void screen_render(int opaque);

/* Run deck upkeep, called every frame of the matching scene. The title frame only
 * records changes to the slot: the deck editor must keep seeing the player's real deck.
 * The result, title and select frames also open the reward screen after a finished
 * match, whichever comes first; normally that is the result menu. */
void deck_on_title_frame(void);
void deck_on_select_frame(void);
void deck_on_battle_frame(void);
void deck_on_result_frame(void);

/* Records what the Result menu returned while being told to close. */
void deck_log_result_close(int result);

#endif
