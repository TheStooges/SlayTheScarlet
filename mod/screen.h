/* The mod's screens, drawn over the title and character select scenes and the screen after a
 * match. Everything here runs on the game's main thread, from those scenes' hooks. */
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

/* The main menu's reminder of the hotkey and which run is in use. */
void screen_render_title_hint(void);

#endif
