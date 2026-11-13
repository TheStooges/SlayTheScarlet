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
#include "sounds.h"
#include "state.h"
#include "util.h"
}

using SokuLib::DrawUtils::RectangleShape;
using SokuLib::DrawUtils::Sprite;

/* Run decks replace this deck slot, in memory only. A local choice, not a game rule. */
#define RUN_DECK_SLOT 0

/* Saves and state.txt live here, inside the DLL's folder. */
#define RUNS_FOLDER "runs\\"

#define SCREEN_W 640
#define SCREEN_H 480
#define COLS 5
#define ROWS 4
/* The character menu's last row, after the characters: the settings screen on the left and the
 * gallery on the right. */
#define CHOOSE_SETTINGS STS_NUM_CHARACTERS
#define CHOOSE_SETTINGS_X 24
#define CHOOSE_SETTINGS_Y 356
#define CHOOSE_GALLERY (STS_NUM_CHARACTERS + 1)
#define CHOOSE_GALLERY_X (24 + (COLS - 1) * 120)
#define CHOOSE_GALLERY_Y CHOOSE_SETTINGS_Y
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
    ST_ROULETTE,        /* a pick made at random, shown by the cursor running through the offers */
    ST_SWEEP,           /* cards that left at random, shown by the cursor running over the deck */
    ST_RULES,           /* the run was made under other rules: carry on or start over? */
    ST_RULES_RESTORE,   /* going back to the run's own rules: what settings.ini would change */
    ST_SETTINGS,        /* settings.ini as a list to change */
    ST_SETTINGS_CONFIRM,/* saving would change the rules of saved runs: save anyway? */
    ST_SCORE,           /* a finished run: its results and each match */
    ST_GALLERY_KINDS,      /* the gallery: system, skill, or spell cards */
    ST_GALLERY_CHARACTERS, /* the gallery: whose skill or spell cards */
    ST_GALLERY_CARDS       /* the gallery: the cards, with their sounds */
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

/* The choices on the rule change screen. Carrying on, and going back to the run's own rules, are left
 * out when they are not possible. */
typedef enum { RULES_CARRY_ON, RULES_RESTORE, RULES_BACK, RULES_START_OVER, RULES_OPTION_COUNT } rules_option;
#define RULES_ROW_Y 150
#define RULES_ROW_PITCH 64

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

/* The score screen: a small grid of the deck at the chosen point, and below it the starting deck and
 * each match, in two lines each, this many at a time. */
#define SCORE_DECK_X 40
#define SCORE_DECK_Y 112
#define SCORE_LIST_Y 226
#define SCORE_ENTRY_H 38
#define SCORE_VISIBLE 5

/* embedded.cpp: a data file built into the DLL, by its resource name (CARDS_CSV, LANG_JA). The
 * bytes are not NUL-terminated and stay valid while the DLL is loaded. NULL if missing. */
const char *embedded_data(const char *name, size_t *length);

/* history.cpp: a run's record, for the score screen. Card names come from load_card_text, so it
 * must have loaded the run's character. The change is empty for the last match of a finished run. */
void count_results(const sts_run *run, int *won, int *lost, int *rounds_won, int *rounds_lost);
void describe_match(const sts_run *run, int index, char *result, size_t result_size, char *change, size_t change_size);

/* card_sounds.cpp: a card's sounds in a match, played on the mod's screens. Owner is
 * STS_SOUND_SYSTEM for a system card. card_sounds_start begins after delay frames, so moving quickly
 * over cards plays nothing, and the card's first sound plays then, wherever it falls in the move; card_sounds_tick, once a frame, plays what is due. A card with no row
 * plays its type's card-use sound, and a card whose sounds vary plays its outcomes in turn. That
 * suits the gallery, where a card is played over and over; the offer screens should play only the
 * first. card_sounds_release frees the files loaded. */
void card_sounds_start(int owner, uint16_t id, int type, int delay, int outcomes_in_turn);
/* The wait before a card's sounds, on every screen that plays them: 74 frames, about 1.23 seconds. */
#define CARD_SOUNDS_SETTLE_FRAMES 74
/* Once a frame: on the draft and reward screens, plays the offer under the cursor once it has rested
 * there (first outcome only), and stops it on leaving them. No other run screen plays sounds. */
void card_sounds_follow_offers(void);
/* One of the game's shared sounds (data/se/NNN.wav), at once, loaded as the card sounds are. */
void play_common_sound(int number);
void card_sounds_stop(void);
void card_sounds_tick(void);
void card_sounds_release(void);

/* gallery.cpp: every card, by type and then by character, each playing its sounds. Only cards found in
 * a run are shown; the others are face down and silent. */
void open_gallery(void);
void on_gallery_event(screen_event event);
int gallery_card_shown(int index);   /* of g_gallery_cards: found, or the gallery is unlocked */
extern int g_gallery_unlock_armed;  /* Unlock the gallery was chosen once, and waits for a second press */
int gallery_all_found(void);       /* every card in the game has been offered in a run */
int gallery_unlock_offered(void);  /* Unlock the gallery can still be chosen */
/* The character whose cards are shown, or -1 for the system cards. */
extern int g_gallery_character;
extern int g_gallery_kind;   /* an sts_card_type */
extern sts_card g_gallery_cards[STS_POOL_MAX_CARDS];
extern int g_gallery_count;
extern int g_gallery_art_dirty;
/* The kinds of card listed first, in this order, then Unlock the gallery until it is used. */
#define GALLERY_KINDS 3
#define GALLERY_UNLOCK_ROW GALLERY_KINDS

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
/* Cards that have been offered in a run (at the draft or after a match, taken or not), or were in a
 * starting deck, kept in
 * state.txt, for the gallery. note_found_offers records a run's offers (each save does);
 * note_found_from_saves goes through every saved run's offers, so runs made before cards were
 * recorded count too. card_found takes -1 as the character for a system card. */
void note_found_offers(const sts_run *run);
/* A starting deck, when a run begins: its cards are never offered, so they are recorded this way. */
void note_found_deck(int character, const sts_deck *deck);
void note_found_from_saves(void);
int card_found(int character, uint16_t id);
/* Unlock the gallery shows every card in it, found or not, from then on; the counts of cards found
 * stay as they are. */
int gallery_unlocked(void);
void unlock_gallery(void);
/* How many runs of this character were started over under these rules, and counting one more. */
int restart_count(int character, uint32_t rules_hash);
void count_restart(int character, uint32_t rules_hash);
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
extern sts_match_result g_match_result;   /* the last finished match, from the run deck's side */
extern int g_match_no_offer;

/* screen.cpp */
typedef enum { TOGGLE_NONE, TOGGLE_USE, TOGGLE_STOP } choose_toggle;
choose_toggle choose_toggle_for(int character);
void set_error(const char *what, const char *detail);
void maybe_start_reward(void);
void go_to_chooser_at(int cursor);
/* True on the first frame a button is held, then again every few frames while it stays held, as
 * menus move while a direction is held. held counts frames, negative for left and up. */
int repeats_while_held(int held);
/* Moves g_cursor through a grid of count cards, DECK_COLS wide, as on the deck screen. */
void move_card_grid(screen_event event, int count);
/* Seconds left to take the reward on screen, or -1 if there is no limit or no reward. */
int reward_seconds_left(void);
/* Seconds before the deck screen after a reward moves on by itself, or -1. */
int deck_seconds_left(void);
/* The offers the roulette runs through, which may be ones no longer on offer. */
extern sts_offer g_roulette_offers[STS_OFFER_MAX];
extern int g_roulette_count;
/* On the sweep: the cards that could have left, and which are drawn dimmed otherwise. */
extern int g_sweep_candidate[MAX_DECK_ART];
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
extern int g_rules_restore_others;   /* other runs that going back to this run's rules would affect */
extern char g_rules_problem[128];
extern int g_pending_offer;
extern int g_selected[MAX_DECK_ART];
extern int g_selected_count;
extern deck_change g_changes_removed[MAX_CHANGE_LINES];
extern deck_change g_changes_added[MAX_CHANGE_LINES];
extern int g_changes_removed_count;
extern int g_changes_added_count;
extern int g_score_top;   /* the first entry the score screen shows; g_cursor is the chosen one */
/* The score screen's entries: the starting deck (if no match was forgotten), then each match. */
int score_entry_count(void);
int score_entry_match(int entry);   /* its index in the history, or -1 for the draft */
/* The point of the run the deck screen shows for a finished run: a match's index in the history, -1
 * for the draft, or SCORE_FINAL_DECK. */
#define SCORE_FINAL_DECK (-2)
extern int g_viewed_match;

/* settings_screen.cpp */
const char *offers_off_screen(const sts_settings *settings);
/* How many saved runs follow the current rules but would not follow settings.ini with these rules in
 * it, named in g_settings_affected. */
int runs_following_other_rules(const sts_settings *rules);
/* Writes these rules into settings.ini (the settings that are not rules stay as they are) and loads
 * it. Returns 0, with g_error set, if that fails. */
int restore_rules(const sts_settings *rules);
/* Each setting that putting these rules into settings.ini would change, as the settings screen names
 * it and its values, in menu order. Settings the screen does not show are only counted. */
typedef struct {
    int menu;
    const char *label;
    char now[96];
    char restored[96];
} rule_change;
#define RULE_CHANGE_MAX (SETTINGS_MENU_COUNT * MAX_SETTINGS_ROWS)
void describe_rule_changes(const sts_settings *rules);
extern rule_change g_rule_changes[RULE_CHANGE_MAX];
extern int g_rule_change_count;
extern int g_rule_changes_hidden;   /* changes to settings only settings.ini has */
/* The table's lines: a heading for each menu, then its settings. It scrolls when they are more than
 * fit above the notes under it. */
int rule_table_line_count(void);
int rule_table_rows(void);
#define RULE_TABLE_Y 107
#define RULE_TABLE_ROW_H 19
extern int g_rule_table_top;
void open_settings(void);
/* The seed's text box, typed into with the keyboard. While it is open, the screen's buttons do
 * nothing: seed_entry_update reads the keys instead. */
int seed_entry_open(void);
int settings_on_seed_row(void);
void seed_entry_update(void);
extern char g_seed_entry[STS_SEED_SIZE];
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
