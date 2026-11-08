/* What the mod's own source files share. Not for anything outside mod/. */
#ifndef STS_INTERNAL_H
#define STS_INTERNAL_H

#include <SokuLib.hpp>
#include <windows.h>
#include "log.h"
#include "match.h"
#include "run_deck.h"
#include "screen.h"

extern "C" {
#include "offer.h"
#include "pf.h"
#include "pool.h"
#include "run.h"
#include "settings.h"
}

using SokuLib::DrawUtils::RectangleShape;
using SokuLib::DrawUtils::Sprite;

/* Run decks replace this deck slot, in memory only. A local choice, not a game rule. */
#define RUN_DECK_SLOT 0

/* Saves and active.txt live here, inside the DLL's folder. */
#define RUNS_FOLDER "runs\\"

#define SCREEN_W 640
#define SCREEN_H 480
#define COLS 5
#define ROWS 4
#define COLOR_WHITE  0xFFFFFFFFu
#define COLOR_GREY   0xFFB0B0B0u
#define COLOR_GOLD   0xFFFFE080u
#define COLOR_GREEN  0xFF90FF90u
#define COLOR_RED    0xFFFF9090u

#define MAX_TEXTS 96
#define NAME_SIZE 96
#define DESC_SIZE 512
#define MAX_CARD_TEXT (STS_POOL_MAX_CARDS * 2)

typedef enum {
    ST_CLOSED, ST_CHOOSE, ST_DRAFT, ST_DECK,
    ST_REWARD,          /* offers after a match */
    ST_REWARD_TYPE,     /* REPLACE by type: which type leaves */
    ST_REWARD_REMOVE,   /* REPLACE by choice: which cards leave */
    ST_REWARD_CONFIRM,  /* REPLACE by choice: last check before the swap */
    ST_RULES            /* the run was made under other rules: carry on or start over? */
} screen_state;
typedef enum { EV_LEFT, EV_RIGHT, EV_UP, EV_DOWN, EV_CONFIRM, EV_CANCEL, EV_ALT, EV_TOGGLE } screen_event;

typedef struct {
    uint16_t id;
    char name[NAME_SIZE];
    char desc[DESC_SIZE];
} card_text;

/* The deck screen's grid. Larger decks show their first MAX_DECK_ART cards. */
#define MAX_DECK_ART 40
#define DECK_COLS 10
#define DECK_X 28
#define DECK_Y 58
#define DECK_PITCH_X 59
#define DECK_PITCH_Y 76

/* Offers add one card, and copies cannot exceed the copy limit, so few lines are ever needed. */
#define MAX_CHANGE_LINES 8
typedef struct {
    uint16_t id;
    int copies;
} deck_change;

/* The choices on the rule change screen. Carrying on is left out when it is not possible. */
typedef enum { RULES_CARRY_ON, RULES_BACK, RULES_START_OVER } rules_option;
#define RULES_ROW_Y 150
#define RULES_ROW_PITCH 70

/* run_data.cpp */
void run_path(char *out, size_t size, int character);
int load_data(void);
void save_run(void);
extern char g_dir[MAX_PATH];
extern int g_data_ok;
extern sts_card_db g_db;
extern sts_settings g_settings;
extern sts_run g_run;

/* text.cpp: the mod's own text in the game's language. tr takes the English text as written in
 * the source and returns its translation, or the English if there is none. Use it with a string
 * literal, tr("..."), so the tests can check every one has a translation. */
const char *tr(const char *english);
const char *character_label(int character);
/* 1 if the game runs in Japanese, 0 under the English translation (th123e). */
int game_is_japanese(void);

/* card_text.cpp */
void load_card_text(int character);
const card_text *find_text(uint16_t id);

/* run_deck.cpp */
void ids_text(char *out, size_t size, SokuLib::Dequeue<unsigned short> &deque);
int write_active_deck(char *why, size_t why_size);
const char *blocking_mod(void);
void remember_real_deck(void);
void save_active_file(void);
void deactivate_run_deck(void);
int activate_run_deck(void);
int run_deck_in_use(void);
void resume_active_run(void);
void note_slot_change(const char *where);
extern int g_rules_changed_character;
extern int g_active;
extern int g_active_character;
extern sts_deck g_active_deck;
extern int g_slot_differs;

/* match.cpp */
extern int g_match_reward_due;
extern int g_match_won;
extern int g_match_no_offer;

/* screen.cpp */
void set_error(const char *what, const char *detail);
void maybe_start_reward(void);
int rules_option_count(void);
rules_option rules_option_at(int index);
extern screen_state g_state;
extern int g_cursor;
extern int g_alt_armed;
extern int g_dirty;
extern char g_error[256];
extern char g_status[256];
extern sts_run_phase g_saved_phase[STS_NUM_CHARACTERS];
extern int g_saved_picks[STS_NUM_CHARACTERS];
extern int g_has_save[STS_NUM_CHARACTERS];
extern int g_saved_rules_changed[STS_NUM_CHARACTERS];
extern int g_rules_start_over_armed;
extern char g_rules_problem[128];
extern int g_pending_offer;
extern int g_selected[MAX_DECK_ART];
extern int g_selected_count;
extern deck_change g_changes_removed[MAX_CHANGE_LINES];
extern deck_change g_changes_added[MAX_CHANGE_LINES];
extern int g_changes_removed_count;
extern int g_changes_added_count;

/* screen_draw.cpp */
extern int g_deck_art_dirty;

#endif
