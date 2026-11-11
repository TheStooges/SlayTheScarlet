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
#include "util.h"
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
/* The character menu's last entry, after the characters: the settings screen. */
#define CHOOSE_SETTINGS STS_NUM_CHARACTERS
#define CHOOSE_SETTINGS_X 24
#define CHOOSE_SETTINGS_Y 356
/* How many cards the draft and reward screens fit side by side. */
#define OFFERS_ON_SCREEN 3
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
    ST_RULES,           /* the run was made under other rules: carry on or start over? */
    ST_SETTINGS,        /* settings.ini as a list to change */
    ST_SETTINGS_CONFIRM /* saving would change the rules of saved runs: save anyway? */
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

/* One setting on the settings screen. Its help is a few lines: some always shown, and some about one
 * value (help_value, as the file writes it), shown only while that value is selected. */
#define SETTINGS_HELP_LINES 8
typedef struct {
    const char *section, *key;
    const char *label;
    const char *help[SETTINGS_HELP_LINES];
    const char *help_value[SETTINGS_HELP_LINES];   /* NULL: always shown */
    int help_count;
} settings_row;
#define MAX_SETTINGS_ROWS 48
/* The settings are split into menus, listed in this order and followed by Save changes. */
enum { SETTINGS_MENU_DRAFT, SETTINGS_MENU_REWARD, SETTINGS_MENU_RUN, SETTINGS_MENU_COUNT };
#define SETTINGS_LIST_Y 56
#define SETTINGS_ROW_H 19
#define SETTINGS_VISIBLE_ROWS 14

/* run_data.cpp */
void run_path(char *out, size_t size, int character);
int run_file_exists(int character);
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
void suspend_run_deck_for_rules(void);
extern int g_rules_changed_character;
extern int g_active;
extern int g_active_character;
extern sts_deck g_active_deck;
extern int g_slot_differs;

/* match.cpp. g_match_reward_due asks for the reward screen: after a new match (g_match_new), or
 * to show an offer that was already waiting when the game started. */
extern int g_match_reward_due;
extern int g_match_new;
extern int g_match_won;
extern int g_match_no_offer;

/* screen.cpp */
typedef enum { TOGGLE_NONE, TOGGLE_USE, TOGGLE_STOP } choose_toggle;
choose_toggle choose_toggle_for(int character);
void set_error(const char *what, const char *detail);
void maybe_start_reward(void);
void go_to_chooser_at(int cursor);
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
extern uint32_t g_saved_hash[STS_NUM_CHARACTERS];
extern int g_rules_start_over_armed;
extern char g_rules_problem[128];
extern int g_pending_offer;
extern int g_selected[MAX_DECK_ART];
extern int g_selected_count;
extern deck_change g_changes_removed[MAX_CHANGE_LINES];
extern deck_change g_changes_added[MAX_CHANGE_LINES];
extern int g_changes_removed_count;
extern int g_changes_added_count;

/* settings_screen.cpp */
const char *offers_off_screen(const sts_settings *settings);
void open_settings(void);
void on_settings_event(screen_event event);
void on_settings_confirm_event(screen_event event);
void settings_value_text(const settings_row *row, char *out, size_t size);
int settings_row_changed(const settings_row *row);
int settings_help_shown(const settings_row *row, int line);
const char *settings_menu_name(int menu);
const char *settings_menu_help(int menu);
int settings_menu_changed(int menu);
extern settings_row g_settings_rows[MAX_SETTINGS_ROWS];
extern int g_settings_row_count;
extern int g_settings_top;
extern int g_settings_leave_armed;
extern int g_settings_menu;   /* the open menu, or -1 on the list of menus */
extern char g_settings_affected[192];

/* screen_draw.cpp */
extern int g_deck_art_dirty;

#endif
