/* The mod's screens, drawn over the title and character select scenes and the screen after a
 * match. Everything here runs on the game's main thread, from those scenes' hooks. */
#ifndef STS_SCREEN_H
#define STS_SCREEN_H

#include <SokuLib.hpp>

/* Sets the folder holding cards.csv, settings.ini and run saves. Ends in a backslash. */
void screen_set_dir(const char *dir);

int screen_is_open(void);
void screen_open(void);

/* After a match online, with both players sharing deck changes: waits for the opponent to finish with
 * their reward, then shows what both players' decks gained and lost. Closes once seen, or at once if
 * neither deck changed, or if they leave. */
void screen_open_opponent(void);

/* Handles one frame of input. */
void screen_update(const SokuLib::KeyInput &input);

/* Lights the key prompts of the keys held this frame (screen_draw.cpp). Called by screen_update. */
void screen_light_prompts(const SokuLib::KeyInput &input, int escape);

/* Draws on top of whatever the game has drawn this frame. With opaque set it replaces the
 * frame; otherwise the game's scene shows faintly behind it. */
void screen_render(int opaque);

/* Online character select: a warning, at the bottom left, while the opponent's rules differ from this player's
 * (found in the mod's handshake). Nothing when they match, or the opponent has no mod. */
void screen_render_rules_warning(void);

/* The main menu's reminder of the hotkey and which run is in use. */
void screen_render_title_hint(void);

#endif
