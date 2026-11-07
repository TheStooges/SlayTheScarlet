/* The draft screen. Input becomes events and events drive a small state machine, so a
 * later spectator view can drive the same screen from network events instead.
 *
 * Text comes from the game's own card files, so it is in whatever language the player
 * launched with. Text is passed through to the game's font untouched. */
#include <SokuLib.hpp>
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
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

static char g_dir[MAX_PATH];
static screen_state g_state = ST_CLOSED;
static int g_cursor = 0;
static int g_character = 0;
static int g_alt_armed = 0;
static int g_dirty = 1;
static char g_error[256];
static char g_status[256];

static int g_data_ok = 0;
static sts_card_db g_db;
static sts_settings g_settings;
static sts_run g_run;
static sts_run_phase g_saved_phase[STS_NUM_CHARACTERS];
static int g_saved_picks[STS_NUM_CHARACTERS];
static int g_has_save[STS_NUM_CHARACTERS];
static int g_saved_rules_changed[STS_NUM_CHARACTERS];
/* The run that was in use when the game closed, if the rules have changed since. Its deck is not
 * used until the player decides what to do about it. -1 if there is no such run. */
static int g_rules_changed_character = -1;
static int g_rules_can_continue = 0;
static int g_rules_start_over_armed = 0;
static char g_rules_problem[128];

static card_text g_texts[MAX_CARD_TEXT];
static int g_text_count = 0;

static SokuLib::SWRFont g_font_big, g_font_small;
static int g_fonts_ready = 0;
static Sprite g_labels[MAX_TEXTS];
static int g_label_count = 0;
static Sprite g_art[STS_OFFER_MAX];
static int g_has_art[STS_OFFER_MAX];
static RectangleShape g_box;

/* The deck screen's grid. Larger decks show their first MAX_DECK_ART cards. */
#define MAX_DECK_ART 40
#define DECK_COLS 10
#define DECK_X 28
#define DECK_Y 58
#define DECK_PITCH_X 59
#define DECK_PITCH_Y 76
static Sprite g_deck_art[MAX_DECK_ART];
static int g_has_deck_art[MAX_DECK_ART];
static int g_deck_art_dirty = 1;

/* The reward being resolved. */
static int g_pending_offer = 0;
static int g_selected[MAX_DECK_ART];
static int g_selected_count = 0;
static int g_last_selected = 0;
static int g_close_after_deck = 0;

/* What the last battle scene showed, kept for when the scene has gone. */
static int g_match_used_run_deck = 0;
static int g_match_run_on_right = 0;
static int g_match_score_left = 0;
static int g_match_score_right = 0;
static int g_match_reward_due = 0;
static int g_match_won = 0;
static int g_match_no_offer = 0;

/* What the last reward changed, listed on the deck screen beside the card details. Offers
 * add one card, and copies cannot exceed the copy limit, so few lines are ever needed. */
#define MAX_CHANGE_LINES 8
typedef struct {
    uint16_t id;
    int copies;
} deck_change;
static deck_change g_changes_removed[MAX_CHANGE_LINES];
static deck_change g_changes_added[MAX_CHANGE_LINES];
static int g_changes_removed_count = 0;
static int g_changes_added_count = 0;

static void enter_phase(void);
static void save_run(void);
static void maybe_start_reward(void);
static void describe_deck_change(const sts_deck *before);
static void clear_deck_change(void);
static void load_card_text(int character);
static int load_data(void);

/* ---------- data ---------- */

/* The log is also written from the threads that handle packets. */
static CRITICAL_SECTION g_log_lock;

void screen_set_dir(const char *dir)
{
    InitializeCriticalSection(&g_log_lock);
    strncpy(g_dir, dir, sizeof g_dir - 1);
}

static void set_error(const char *what, const char *detail)
{
    _snprintf(g_error, sizeof g_error - 1, "%s: %s", what, detail);
    g_error[sizeof g_error - 1] = 0;
    g_dirty = 1;
}

static void run_path(char *out, size_t size, int character)
{
    _snprintf(out, size - 1, "%srun_%02d.sav", g_dir, character);
    out[size - 1] = 0;
}

static int load_data(void)
{
    char path[MAX_PATH + 32];
    int line = 0;
    sts_db_status db;
    sts_settings_status st;
    const char *bad;

    if (g_data_ok)
        return 1;
    sprintf(path, "%scards.csv", g_dir);
    db = sts_card_db_load(path, &g_db, &line);
    if (db != STS_DB_OK) {
        set_error("cards.csv", sts_db_status_str(db));
        return 0;
    }
    sprintf(path, "%ssettings.ini", g_dir);
    st = sts_settings_load(path, &g_settings, &line);
    if (st != STS_SETTINGS_OK && st != STS_SETTINGS_IO) {   /* no file just means defaults */
        char where[64];
        sprintf(where, "settings.ini line %d", line);
        set_error(where, sts_settings_status_str(st));
        return 0;
    }
    bad = sts_settings_validate(&g_settings);
    if (bad) {
        set_error("settings.ini", bad);
        return 0;
    }
    /* Every run plays against opponents without the mod until full mode exists. */
    {
        uint32_t before = sts_settings_hash(&g_settings);

        sts_settings_force_compatible(&g_settings);
        if (sts_settings_hash(&g_settings) != before)
            slog("settings.ini asks for rules that need full mode, using compatible rules instead");
    }
    g_data_ok = 1;
    return 1;
}

static void scan_saves(void)
{
    int c;

    for (c = 0; c < STS_NUM_CHARACTERS; c++) {
        char path[MAX_PATH + 32];
        static sts_run tmp;

        run_path(path, sizeof path, c);
        g_has_save[c] = sts_run_load(path, &tmp) == STS_RUN_OK;
        g_saved_phase[c] = tmp.phase;
        g_saved_picks[c] = tmp.picks_done;
        g_saved_rules_changed[c] = g_has_save[c] && tmp.settings_hash != sts_settings_hash(&g_settings);
    }
}

/* Removes the game's <a> marker and anything else after a '<' in a card name. */
static void clean_name(char *name)
{
    char *tag = strchr(name, '<');
    size_t n;

    if (tag)
        *tag = 0;
    n = strlen(name);
    while (n > 0 && name[n - 1] == ' ')
        name[--n] = 0;
}

static void load_text_file(const char *path)
{
    SokuLib::CSVParser parser(path);

    for (auto &line : parser.data) {
        card_text *t;
        int i = 0;

        if (g_text_count >= MAX_CARD_TEXT)
            return;
        t = &g_texts[g_text_count];
        memset(t, 0, sizeof *t);
        for (auto &cell : line) {
            const char *s = (const char *)cell;
            if (i == 0)
                t->id = (uint16_t)atoi(s);
            else if (i == 1)
                strncpy(t->name, s, NAME_SIZE - 1);
            else if (i == 4)
                strncpy(t->desc, s, DESC_SIZE - 1);
            i++;
        }
        clean_name(t->name);
        g_text_count++;
    }
}

static void load_card_text(int character)
{
    char path[128];

    g_text_count = 0;
    load_text_file("data/csv/common/spellcard.csv");
    sprintf(path, "data/csv/%.64s/spellcard.csv", SokuLib::getCharName((unsigned)character));
    load_text_file(path);
}

static const card_text *find_text(uint16_t id)
{
    int i;

    for (i = 0; i < g_text_count; i++)
        if (g_texts[i].id == id)
            return &g_texts[i];
    return NULL;
}

/* ---------- log ---------- */

/* sts.log next to the DLL, restarted each launch. Holds no IPs or player names. */
void slog(const char *fmt, ...)
{
    static FILE *f = NULL;
    va_list ap;

    EnterCriticalSection(&g_log_lock);
    if (!f) {
        char path[MAX_PATH + 32];
        sprintf(path, "%ssts.log", g_dir);
        f = fopen(path, "w");
    }
    if (f) {
        fprintf(f, "[%lu] ", (unsigned long)GetTickCount());
        va_start(ap, fmt);
        vfprintf(f, fmt, ap);
        va_end(ap);
        fputc('\n', f);
        fflush(f);
    }
    LeaveCriticalSection(&g_log_lock);
}

static void ids_text(char *out, size_t size, SokuLib::Dequeue<unsigned short> &dq)
{
    size_t o = 0;
    int i;

    out[0] = 0;
    for (i = 0; i < dq.size && i < 256 && o + 8 < size; i++)
        o += (size_t)sprintf(out + o, "%u ", (unsigned)dq[i]);
}

/* ---------- the run deck in the game ---------- */

/* The finished deck being played. It is kept apart from g_run, which the screen may be
 * using for another character. */
static int g_active = 0;
static int g_active_character = 0;
static sts_deck g_active_deck;
static int g_slot_differs = 0;
/* What the slot held before the run deck: the player's real deck, put back when the run
 * stops being used. In memory only, like the run deck itself. */
static sts_deck g_real_deck;
static int g_have_real_deck = 0;

static SokuLib::Dequeue<unsigned short> &active_slot(void)
{
    return SokuLib::profile1.cards[g_active_character][RUN_DECK_SLOT];
}

int deck_matches_active(int character, const unsigned short *ids, int count)
{
    static sts_deck given, expected;
    int i;

    if (!g_active || character != g_active_character || count != g_active_deck.count)
        return 0;
    sts_deck_clear(&given);
    for (i = 0; i < count; i++)
        sts_deck_add(&given, ids[i]);
    sts_deck_sort(&given);
    expected = g_active_deck;
    sts_deck_sort(&expected);
    return memcmp(given.ids, expected.ids, (size_t)count * 2) == 0;
}

static int slot_holds_active_deck(void)
{
    auto &slot = active_slot();
    int i;

    if (slot.size != g_active_deck.count)
        return 0;
    for (i = 0; i < g_active_deck.count; i++)
        if (slot[i] != g_active_deck.ids[i])
            return 0;
    return 1;
}

/* Same size only: nothing is allocated, so nothing can go wrong with the game's heap. */
static int write_active_deck(char *why, size_t why_size)
{
    auto &slot = active_slot();
    int i;

    if (slot.size != g_active_deck.count) {
        _snprintf(why, why_size - 1, "the deck slot has %d cards, the run deck has %d",
                  slot.size, g_active_deck.count);
        why[why_size - 1] = 0;
        return 0;
    }
    for (i = 0; i < g_active_deck.count; i++)
        slot[i] = g_active_deck.ids[i];
    if (!slot_holds_active_deck()) {
        _snprintf(why, why_size - 1, "it did not read back correctly");
        why[why_size - 1] = 0;
        return 0;
    }
    return 1;
}

static void remember_real_deck(void)
{
    auto &slot = active_slot();
    int i;

    sts_deck_clear(&g_real_deck);
    for (i = 0; i < slot.size && i < STS_MAX_DECK; i++)
        sts_deck_add(&g_real_deck, slot[i]);
    g_have_real_deck = 1;
}

/* Puts the player's deck back, unless the game has already reloaded it. */
static void restore_real_deck(void)
{
    auto &slot = active_slot();
    int i;

    if (g_have_real_deck && slot_holds_active_deck() && slot.size == g_real_deck.count) {
        for (i = 0; i < g_real_deck.count; i++)
            slot[i] = g_real_deck.ids[i];
        slog("restored the real deck for character %d", g_active_character);
    }
    g_have_real_deck = 0;
}

/* active.txt remembers which run is in use across restarts. */
static void active_file_path(char *out)
{
    sprintf(out, "%sactive.txt", g_dir);
}

static void save_active_file(void)
{
    char path[MAX_PATH + 32];

    active_file_path(path);
    if (g_active) {
        FILE *f = fopen(path, "w");
        if (f) {
            fprintf(f, "%d\n", g_active_character);
            fclose(f);
        }
    } else {
        remove(path);
    }
}

static void deactivate_run_deck(void)
{
    if (!g_active)
        return;
    restore_real_deck();
    g_active = 0;
    g_slot_differs = 0;
    save_active_file();
    slog("stopped using the run deck for character %d", g_active_character);
}

/* Starts using g_run's deck, or updates the deck in use after a reward. */
static int activate_run_deck(void)
{
    char why[96] = "";
    int fresh = !g_active || g_active_character != g_run.character;

    if (g_active && fresh)
        restore_real_deck();
    /* Still on the same run: if the slot no longer holds the old run deck, the game has
     * reloaded the player's deck into it since. */
    if (!fresh && !slot_holds_active_deck())
        remember_real_deck();

    g_active_character = g_run.character;
    g_active_deck = g_run.deck;
    g_active = 1;
    g_slot_differs = 0;
    if (fresh)
        remember_real_deck();

    if (!write_active_deck(why, sizeof why)) {
        g_active = 0;
        g_have_real_deck = 0;
        save_active_file();
        set_error("Cannot use this deck", why);
        slog("activate failed for character %d: %s", g_active_character, why);
        return 0;
    }
    save_active_file();
    g_rules_changed_character = -1;
    slog("using run deck for character %d, slot %d, %d cards", g_active_character, RUN_DECK_SLOT, g_active_deck.count);
    return 1;
}

static int run_deck_in_use(void)
{
    return g_active && g_active_character == g_run.character;
}

/* Goes back to the run that was in use last time the game was open. */
static void resume_active_run(void)
{
    static int done = 0;
    char path[MAX_PATH + 32];
    int character = -1;
    FILE *f;

    if (done)
        return;
    done = 1;
    active_file_path(path);
    f = fopen(path, "r");
    if (!f)
        return;
    if (fscanf(f, "%d", &character) != 1)
        character = -1;
    fclose(f);

    if (character >= 0 && character < STS_NUM_CHARACTERS) {
        sts_run_status st;

        /* A typo in settings.ini is no reason to forget which run was in use. */
        if (!load_data()) {
            slog("could not resume the run for character %d: %s", character, g_error);
            return;
        }
        run_path(path, sizeof path, character);
        st = sts_run_load(path, &g_run);
        if (st == STS_RUN_OK)
            st = sts_run_validate(&g_run, &g_db, &g_settings);
        if (st == STS_RUN_SETTINGS_CHANGED) {
            g_rules_changed_character = character;
            slog("the rules changed since the run for character %d was saved", character);
            return;
        }
        if (st == STS_RUN_OK && g_run.phase != STS_PHASE_DRAFT) {
            activate_run_deck();
            /* An offer left on screen when the game closed comes straight back. */
            if (g_active && g_run.phase == STS_PHASE_REWARD)
                g_match_reward_due = 1;
            return;
        }
        slog("could not resume the run for character %d: %s", character, sts_run_status_str(st));
    }
    /* Whatever active.txt named cannot be used, so stop asking for it. */
    active_file_path(path);
    remove(path);
}

static void note_slot_change(const char *where)
{
    if (!g_active)
        return;
    if (slot_holds_active_deck()) {
        g_slot_differs = 0;
    } else if (!g_slot_differs) {
        char text[1100];
        g_slot_differs = 1;
        ids_text(text, sizeof text, active_slot());
        slog("slot no longer holds the run deck, noticed at %s, menus open %d. Now: %s",
             where, (int)SokuLib::menuManager.size(), text);
    }
}

/* Called when a scene after the battle starts: was that a finished match with the run deck? */
static void finish_match(void)
{
    int left = g_match_score_left, right = g_match_score_right;

    if (!g_match_used_run_deck)
        return;
    g_match_used_run_deck = 0;
    slog("match over: rounds won %d to %d", left, right);
    if (left == right || (left < 2 && right < 2)) {
        slog("match was not finished, no reward");
        return;
    }
    g_match_won = g_match_run_on_right ? right > left : left > right;
    g_match_reward_due = 1;
}

void deck_on_result_frame(void)
{
    finish_match();
    maybe_start_reward();
}

/* Only changes: the menu returns the same value on every frame of its exit fade. */
int deck_take_result_skip(void)
{
    int skip = g_match_no_offer;

    g_match_no_offer = 0;
    return skip;
}

void deck_log_result_close(int result)
{
    static int last = -1;

    if (result != last)
        slog("result menu closing, it returned %d", result);
    last = result;
}

void deck_on_title_frame(void)
{
    resume_active_run();
    finish_match();
    maybe_start_reward();
    note_slot_change("title");
}

/* Online, only the deck is kept up. Pausing an online scene for a late reward could stall the
 * connection, so a reward that did not reach the screen after the match waits for the title. */
void deck_on_net_select_frame(void)
{
    char why[96] = "";

    if (!g_active)
        return;
    note_slot_change("online character select");
    if (g_slot_differs) {
        remember_real_deck();
        if (write_active_deck(why, sizeof why)) {
            slog("wrote the run deck again");
            g_slot_differs = 0;
        } else {
            slog("could not write the run deck again: %s", why);
        }
    }
}

/* Character select is the point a write is known to reach the next match. */
void deck_on_select_frame(void)
{
    static DWORD last_tick = 0;
    DWORD now = GetTickCount();
    char why[96] = "";

    if (now - last_tick > 1000)
        slog("character select entered, run deck %s", g_active ? "active" : "not active");
    last_tick = now;

    finish_match();
    maybe_start_reward();

    if (!g_active)
        return;
    note_slot_change("character select");
    if (g_slot_differs) {
        /* The game put the player's deck back, possibly with edits from the deck editor. */
        remember_real_deck();
        if (write_active_deck(why, sizeof why)) {
            slog("wrote the run deck again");
            g_slot_differs = 0;
        } else {
            slog("could not write the run deck again: %s", why);
        }
    }
}

/* Returns 1 if this side is playing the active run deck. */
static int log_battle_side(const char *name, int character, int deck_slot, SokuLib::CharacterManager &player)
{
    /* Player::deckInfo.original; see the probe findings in CLAUDE.md */
    auto *original = reinterpret_cast<SokuLib::Dequeue<unsigned short> *>(reinterpret_cast<char *>(&player) + 0x57C + 0x20);
    static sts_deck loaded, expected;
    char text[1100];
    int i;

    ids_text(text, sizeof text, *original);
    slog("match %s: character %d, deck choice %d, loaded %d cards: %s", name, character, deck_slot, original->size, text);
    if (!g_active || character != g_active_character)
        return 0;

    sts_deck_clear(&loaded);
    for (i = 0; i < original->size && i < STS_MAX_DECK; i++)
        sts_deck_add(&loaded, (*original)[i]);
    sts_deck_sort(&loaded);
    expected = g_active_deck;
    sts_deck_sort(&expected);
    if (loaded.count == expected.count && memcmp(loaded.ids, expected.ids, (size_t)loaded.count * 2) == 0) {
        slog("match %s uses the RUN deck", name);
        return 1;
    }
    slog("match %s does NOT use the run deck", name);
    return 0;
}

void deck_on_battle_frame(void)
{
    static DWORD last_tick = 0;
    DWORD now = GetTickCount();

    /* Diagnostics only, and they read game memory directly: a bad read must not end the match. */
    if (now - last_tick > 1000) {
        __try {
            auto &mgr = SokuLib::getBattleMgr();
            int offline = SokuLib::mainMode == SokuLib::BATTLE_MODE_VSCOM || SokuLib::mainMode == SokuLib::BATTLE_MODE_VSPLAYER;
            /* SokuLib's names are from the opponent's view: "VSCLIENT" means you are the host. */
            int host = SokuLib::mainMode == SokuLib::BATTLE_MODE_VSCLIENT;
            int client = SokuLib::mainMode == SokuLib::BATTLE_MODE_VSSERVER;
            int left = log_battle_side("left", (int)SokuLib::leftPlayerInfo.character, SokuLib::leftPlayerInfo.deck, mgr.leftCharacterManager);
            int right = log_battle_side("right", (int)SokuLib::rightPlayerInfo.character, SokuLib::rightPlayerInfo.deck, mgr.rightCharacterManager);

            /* Only VS matches played by a person earn a reward: not practice, replays or
             * spectating. Offline the run deck only reaches the left player; online, the local
             * player's own side. */
            g_match_run_on_right = client;
            g_match_used_run_deck = SokuLib::subMode != SokuLib::BATTLE_SUBMODE_REPLAY
                && (((offline || host) && left) || (client && right));
            g_match_score_left = 0;
            g_match_score_right = 0;
            slog("match mode %d submode %d, counts for a reward: %d", (int)SokuLib::mainMode, (int)SokuLib::subMode, g_match_used_run_deck);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            slog("match: could not read the decks");
        }
    }
    last_tick = now;

    __try {
        auto &mgr = SokuLib::getBattleMgr();
        g_match_score_left = mgr.leftCharacterManager.score;
        g_match_score_right = mgr.rightCharacterManager.score;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

/* ---------- state machine ---------- */

static void save_run(void)
{
    char path[MAX_PATH + 32];
    sts_run_status st;

    run_path(path, sizeof path, g_run.character);
    st = sts_run_save(path, &g_run);
    if (st != STS_RUN_OK)
        set_error("Could not save the run", sts_run_status_str(st));
}

static void set_deck_status(void)
{
    if (run_deck_in_use())
        _snprintf(g_status, sizeof g_status - 1, "In use as %s's Deck %d. Your own deck is safe in your profile.",
                  sts_character_name(g_run.character), RUN_DECK_SLOT + 1);
    else
        _snprintf(g_status, sizeof g_status - 1, "Not in use. %s's Deck %d is your own deck.",
                  sts_character_name(g_run.character), RUN_DECK_SLOT + 1);
    g_status[sizeof g_status - 1] = 0;
}

static void enter_phase(void)
{
    g_cursor = 0;
    g_alt_armed = 0;
    g_dirty = 1;
    if (g_run.phase == STS_PHASE_DRAFT) {
        g_state = ST_DRAFT;
    } else {
        g_state = g_run.phase == STS_PHASE_REWARD ? ST_REWARD : ST_DECK;
        g_deck_art_dirty = 1;
        /* Opening a run only looks at it. A run already in use picks up any new deck. */
        if (run_deck_in_use())
            activate_run_deck();
        set_deck_status();
    }
}

/* Runs on the first title or character select frame after a finished match. */
static void maybe_start_reward(void)
{
    char path[MAX_PATH + 32];
    sts_reward_outcome outcome = STS_REWARD_NONE;
    sts_run_status st;

    if (!g_match_reward_due)
        return;
    g_match_reward_due = 0;
    if (!g_active || g_state != ST_CLOSED)
        return;

    /* g_run may hold another character if the screen was used since this deck became active. */
    run_path(path, sizeof path, g_active_character);
    st = sts_run_load(path, &g_run);
    if (st == STS_RUN_OK)
        st = sts_run_validate(&g_run, &g_db, &g_settings);
    if (st != STS_RUN_OK) {
        slog("reward skipped, the run could not be loaded: %s", sts_run_status_str(st));
        return;
    }
    load_card_text(g_active_character);
    /* An offer still waiting from before is shown again rather than replaced. */
    if (g_run.phase != STS_PHASE_REWARD) {
        sts_deck before = g_run.deck;

        st = sts_run_begin_reward(&g_run, &g_db, &g_settings, g_match_won, &outcome);
        if (outcome == STS_REWARD_AUTO)
            describe_deck_change(&before);
        slog("reward: won %d, status %s, outcome %d", g_match_won, sts_run_status_str(st), (int)outcome);
        if (st != STS_RUN_OK)
            return;
        save_run();
        if (outcome == STS_REWARD_NONE) {
            g_match_no_offer = 1;
            return;
        }
    } else {
        slog("showing the offer that was already waiting");
    }

    g_error[0] = 0;
    g_status[0] = 0;
    g_close_after_deck = 1;
    enter_phase();
    SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CONFIRM);
}

/* Records what a reward changed, comparing the deck with its old contents. */
static void describe_deck_change(const sts_deck *before)
{
    const sts_deck *decks[2];
    int d, i;

    clear_deck_change();
    decks[0] = before;
    decks[1] = &g_run.deck;
    for (d = 0; d < 2; d++) {
        for (i = 0; i < decks[d]->count; i++) {
            uint16_t id = decks[d]->ids[i];
            int difference = sts_deck_count_of(&g_run.deck, id) - sts_deck_count_of(before, id);
            deck_change *list = d == 0 ? g_changes_removed : g_changes_added;
            int *count = d == 0 ? &g_changes_removed_count : &g_changes_added_count;

            /* Each distinct card once: at its first position in the deck being scanned. */
            if (i > 0 && decks[d]->ids[i - 1] == id)
                continue;
            if (d == 0)
                difference = -difference;
            if (difference > 0 && *count < MAX_CHANGE_LINES) {
                list[*count].id = id;
                list[*count].copies = difference;
                (*count)++;
            }
        }
    }
}

static void clear_deck_change(void)
{
    g_changes_removed_count = 0;
    g_changes_added_count = 0;
}

/* Applies the chosen offer and shows the new deck. */
static void commit_reward(const sts_replace_choice *choice)
{
    sts_deck before = g_run.deck;
    sts_run_status st = sts_run_reward_pick(&g_run, &g_db, &g_settings, g_pending_offer, choice);

    if (st != STS_RUN_OK) {
        set_error("Reward", sts_run_status_str(st));
        slog("reward pick refused: %s", sts_run_status_str(st));
        return;
    }
    slog("reward taken: card %u", (unsigned)g_run.offers[g_pending_offer].id);
    SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CONFIRM);
    g_selected_count = 0;
    describe_deck_change(&before);
    save_run();
    enter_phase();
}

static void commit_selected_removals(void)
{
    uint16_t ids[MAX_DECK_ART];
    sts_replace_choice choice;
    int i, n = 0;

    for (i = 0; i < g_run.deck.count && i < MAX_DECK_ART; i++)
        if (g_selected[i])
            ids[n++] = g_run.deck.ids[i];
    choice.remove_ids = ids;
    choice.remove_count = n;
    choice.type = 0;
    commit_reward(&choice);
}

/* Fills the change list with what the chosen swap would do, for the confirm screen. */
static void preview_selected_swap(void)
{
    const sts_offer *o = &g_run.offers[g_pending_offer];
    int i;

    clear_deck_change();
    for (i = 0; i < g_run.deck.count && i < MAX_DECK_ART; i++) {
        uint16_t id;
        int n;

        if (!g_selected[i])
            continue;
        id = g_run.deck.ids[i];
        /* The deck is sorted, so copies of a card are together. */
        n = g_changes_removed_count - 1;
        if (n >= 0 && g_changes_removed[n].id == id) {
            g_changes_removed[n].copies++;
        } else if (g_changes_removed_count < MAX_CHANGE_LINES) {
            g_changes_removed[g_changes_removed_count].id = id;
            g_changes_removed[g_changes_removed_count].copies = 1;
            g_changes_removed_count++;
        }
    }
    g_changes_added[0].id = o->id;
    g_changes_added[0].copies = o->copies;
    g_changes_added_count = 1;
}

/* Moves through the card grid shared by the deck and removal screens. */
static void move_grid_cursor(screen_event ev)
{
    int count = g_run.deck.count < MAX_DECK_ART ? g_run.deck.count : MAX_DECK_ART;
    int step = ev == EV_RIGHT ? 1 : ev == EV_LEFT ? count - 1
             : ev == EV_DOWN ? DECK_COLS : count - DECK_COLS % count;

    if (count > 0) {
        g_cursor = (g_cursor + step) % count;
        SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_MOVE);
        g_dirty = 1;
    }
}

static void next_available_type(int direction)
{
    unsigned mask = sts_replace_types_available(&g_db, g_run.character, &g_run.deck,
                                                g_run.offers[g_pending_offer].copies);
    int i;

    for (i = 0; i < 3; i++) {
        g_cursor = (g_cursor + direction + 3) % 3;
        if (mask & STS_TYPE_BIT(g_cursor))
            break;
    }
}

static void start_new_run(int character)
{
    LARGE_INTEGER t;
    sts_run_status st;

    /* Offers are only seen by this player, so a local seed is fine. */
    QueryPerformanceCounter(&t);
    st = sts_run_start(&g_run, &g_db, &g_settings, character, (uint64_t)t.QuadPart);
    if (st != STS_RUN_OK) {
        set_error("Could not start a run", sts_run_status_str(st));
        return;
    }
    /* That character's old run deck is finished with. */
    if (g_active && g_active_character == character)
        deactivate_run_deck();
    if (g_rules_changed_character == character) {
        g_rules_changed_character = -1;
        save_active_file();
    }
    save_run();
    enter_phase();
}

/* The choices on the rule change screen. Carrying on is left out when it is not possible. */
typedef enum { RULES_CARRY_ON, RULES_BACK, RULES_START_OVER } rules_option;
#define RULES_ROW_Y 150
#define RULES_ROW_PITCH 70

static int rules_option_count(void)
{
    return g_rules_can_continue ? 3 : 2;
}

static rules_option rules_option_at(int index)
{
    return (rules_option)(g_rules_can_continue ? index : index + 1);
}

/* Works out whether g_run, made under other rules, can carry on under the current ones. */
static void enter_rules_screen(void)
{
    static sts_run trial;
    sts_run_status st;

    g_state = ST_RULES;
    g_cursor = 0;
    g_rules_start_over_armed = 0;
    g_rules_problem[0] = 0;
    g_dirty = 1;

    trial = g_run;
    st = sts_run_adopt_rules(&trial, &g_db, &g_settings);
    g_rules_can_continue = g_settings.allow_rule_changes && st == STS_RUN_OK;
    if (!g_settings.allow_rule_changes) {
        sprintf(g_rules_problem, "The current settings do not allow a run to carry on under new rules.");
    } else if (st != STS_RUN_OK) {
        sts_deck_status deck = sts_deck_validate(&g_run.deck, &g_settings.rules, NULL);

        _snprintf(g_rules_problem, sizeof g_rules_problem - 1, "It cannot carry on under them: %s.",
                  deck != STS_DECK_OK ? sts_deck_status_str(deck) : sts_run_status_str(st));
        g_rules_problem[sizeof g_rules_problem - 1] = 0;
    }
}

static void open_character(int character)
{
    char path[MAX_PATH + 32];
    sts_run_status st;

    g_character = character;
    g_error[0] = 0;
    g_status[0] = 0;
    load_card_text(character);
    run_path(path, sizeof path, character);
    st = sts_run_load(path, &g_run);
    if (st == STS_RUN_IO) {
        start_new_run(character);
        return;
    }
    if (st == STS_RUN_OK)
        st = sts_run_validate(&g_run, &g_db, &g_settings);
    if (st == STS_RUN_SETTINGS_CHANGED) {
        enter_rules_screen();
        return;
    }
    if (st != STS_RUN_OK) {
        set_error(sts_character_name(character), sts_run_status_str(st));
        return;
    }
    enter_phase();
}

static void go_to_chooser(void)
{
    scan_saves();
    g_state = ST_CHOOSE;
    g_cursor = g_character;
    g_alt_armed = 0;
    g_dirty = 1;
}

/* Pressing the third button twice gives up on a run, so one slip cannot lose it. */
static int alt_twice(void)
{
    if (!g_alt_armed) {
        g_alt_armed = 1;
        g_dirty = 1;
        return 0;
    }
    g_alt_armed = 0;
    return 1;
}

static void handle_event(screen_event ev)
{
    if (ev != EV_ALT && g_alt_armed) {
        g_alt_armed = 0;
        g_dirty = 1;
    }
    switch (g_state) {
    case ST_CHOOSE:
        switch (ev) {
        case EV_LEFT:  g_cursor = (g_cursor + STS_NUM_CHARACTERS - 1) % STS_NUM_CHARACTERS; break;
        case EV_RIGHT: g_cursor = (g_cursor + 1) % STS_NUM_CHARACTERS; break;
        case EV_UP:    g_cursor = (g_cursor + STS_NUM_CHARACTERS - COLS) % STS_NUM_CHARACTERS; break;
        case EV_DOWN:  g_cursor = (g_cursor + COLS) % STS_NUM_CHARACTERS; break;
        case EV_CONFIRM:
            if (g_data_ok) {
                SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CONFIRM);
                open_character(g_cursor);
            }
            g_dirty = 1;
            return;
        case EV_CANCEL:
            SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CANCEL);
            g_state = ST_CLOSED;
            return;
        case EV_ALT:
            if (g_data_ok && alt_twice()) {
                g_character = g_cursor;
                g_error[0] = 0;
                g_status[0] = 0;
                load_card_text(g_cursor);
                start_new_run(g_cursor);
            }
            g_dirty = 1;
            return;
        case EV_TOGGLE:
            return;
        }
        SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_MOVE);
        g_error[0] = 0;
        g_dirty = 1;
        break;

    case ST_DRAFT:
        switch (ev) {
        case EV_LEFT:
        case EV_RIGHT:
            if (g_run.offer_count > 0) {
                int step = ev == EV_RIGHT ? 1 : g_run.offer_count - 1;
                g_cursor = (g_cursor + step) % g_run.offer_count;
                SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_MOVE);
                g_dirty = 1;
            }
            break;
        case EV_CONFIRM: {
            sts_run_status st = sts_run_draft_pick(&g_run, &g_db, &g_settings, g_cursor);
            if (st != STS_RUN_OK) {
                set_error("Draft", sts_run_status_str(st));
                break;
            }
            SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CONFIRM);
            save_run();
            /* A finished draft is meant to be played. */
            if (g_run.phase == STS_PHASE_READY)
                activate_run_deck();
            enter_phase();
            break;
        }
        case EV_CANCEL:
            SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CANCEL);
            go_to_chooser();
            break;
        default:
            break;
        }
        break;

    case ST_DECK:
        switch (ev) {
        case EV_LEFT:
        case EV_RIGHT:
        case EV_UP:
        case EV_DOWN:
            move_grid_cursor(ev);
            break;
        case EV_TOGGLE:
            g_error[0] = 0;
            if (run_deck_in_use())
                deactivate_run_deck();
            else
                activate_run_deck();
            set_deck_status();
            SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CONFIRM);
            g_dirty = 1;
            break;
        case EV_CONFIRM:
        case EV_CANCEL:
            SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CANCEL);
            clear_deck_change();
            if (g_close_after_deck) {
                g_close_after_deck = 0;
                g_state = ST_CLOSED;
            } else {
                go_to_chooser();
            }
            break;
        case EV_ALT:
            if (alt_twice()) {
                g_error[0] = 0;
                g_status[0] = 0;
                start_new_run(g_run.character);
            }
            g_dirty = 1;
            break;
        default:
            break;
        }
        break;

    /* There is no way out of a reward except taking one: it is saved, so a restart comes
     * back to the same offers. */
    case ST_REWARD:
        switch (ev) {
        case EV_LEFT:
        case EV_RIGHT:
            if (g_run.offer_count > 0) {
                int step = ev == EV_RIGHT ? 1 : g_run.offer_count - 1;
                g_cursor = (g_cursor + step) % g_run.offer_count;
                SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_MOVE);
                g_dirty = 1;
            }
            break;
        case EV_CONFIRM:
            if (g_cursor >= g_run.offer_count)
                break;
            g_pending_offer = g_cursor;
            g_error[0] = 0;
            g_dirty = 1;
            if (g_settings.reward.mode == STS_REWARD_REPLACE && g_settings.reward.replace == STS_REPLACE_CHOOSE) {
                memset(g_selected, 0, sizeof g_selected);
                g_selected_count = 0;
                g_cursor = 0;
                g_state = ST_REWARD_REMOVE;
                SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CONFIRM);
            } else if (g_settings.reward.mode == STS_REWARD_REPLACE && g_settings.reward.replace == STS_REPLACE_BY_TYPE) {
                g_cursor = 2;
                next_available_type(1);
                g_state = ST_REWARD_TYPE;
                SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CONFIRM);
            } else {
                commit_reward(NULL);
            }
            break;
        default:
            break;
        }
        break;

    case ST_REWARD_TYPE:
        switch (ev) {
        case EV_UP:
        case EV_LEFT:
            next_available_type(-1);
            SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_MOVE);
            g_dirty = 1;
            break;
        case EV_DOWN:
        case EV_RIGHT:
            next_available_type(1);
            SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_MOVE);
            g_dirty = 1;
            break;
        case EV_CONFIRM: {
            sts_replace_choice choice;
            choice.remove_ids = NULL;
            choice.remove_count = 0;
            choice.type = g_cursor;
            commit_reward(&choice);
            break;
        }
        case EV_CANCEL:
            SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CANCEL);
            g_cursor = g_pending_offer;
            g_state = ST_REWARD;
            g_dirty = 1;
            break;
        default:
            break;
        }
        break;

    case ST_REWARD_REMOVE:
        switch (ev) {
        case EV_LEFT:
        case EV_RIGHT:
        case EV_UP:
        case EV_DOWN:
            move_grid_cursor(ev);
            break;
        case EV_CONFIRM:
            if (g_cursor >= MAX_DECK_ART)
                break;
            g_selected[g_cursor] = !g_selected[g_cursor];
            g_selected_count += g_selected[g_cursor] ? 1 : -1;
            g_dirty = 1;
            SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CONFIRM);
            if (g_selected_count >= g_run.offers[g_pending_offer].copies) {
                g_last_selected = g_cursor;
                preview_selected_swap();
                g_state = ST_REWARD_CONFIRM;
            }
            break;
        case EV_CANCEL:
            SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CANCEL);
            g_selected_count = 0;
            g_cursor = g_pending_offer;
            g_state = ST_REWARD;
            g_dirty = 1;
            break;
        default:
            break;
        }
        break;

    case ST_REWARD_CONFIRM:
        switch (ev) {
        case EV_CONFIRM:
            commit_selected_removals();
            break;
        case EV_CANCEL:
            /* Undoes only the last pick, so the others need not be chosen again. */
            SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CANCEL);
            g_selected[g_last_selected] = 0;
            g_selected_count--;
            clear_deck_change();
            g_state = ST_REWARD_REMOVE;
            g_dirty = 1;
            break;
        default:
            break;
        }
        break;

    case ST_RULES:
        switch (ev) {
        case EV_LEFT:
        case EV_RIGHT:
        case EV_UP:
        case EV_DOWN: {
            int count = rules_option_count();
            int step = (ev == EV_DOWN || ev == EV_RIGHT) ? 1 : count - 1;

            g_cursor = (g_cursor + step) % count;
            g_rules_start_over_armed = 0;
            SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_MOVE);
            g_dirty = 1;
            break;
        }
        case EV_CONFIRM:
            switch (rules_option_at(g_cursor)) {
            case RULES_CARRY_ON: {
                sts_run_status st = sts_run_adopt_rules(&g_run, &g_db, &g_settings);

                if (st != STS_RUN_OK) {
                    set_error("Could not carry on", sts_run_status_str(st));
                    break;
                }
                slog("run for character %d now follows the current rules", g_run.character);
                SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CONFIRM);
                save_run();
                /* The run that was in use before the rules changed goes back into use. */
                if (g_rules_changed_character == g_run.character && g_run.phase != STS_PHASE_DRAFT)
                    activate_run_deck();
                enter_phase();
                break;
            }
            case RULES_BACK:
                SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CANCEL);
                go_to_chooser();
                break;
            case RULES_START_OVER:
                /* A second press is needed, so one slip cannot lose the run. */
                if (!g_rules_start_over_armed) {
                    g_rules_start_over_armed = 1;
                    SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CONFIRM);
                    g_dirty = 1;
                    break;
                }
                g_rules_start_over_armed = 0;
                g_error[0] = 0;
                g_status[0] = 0;
                SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CONFIRM);
                start_new_run(g_run.character);
                break;
            }
            break;
        case EV_CANCEL:
            SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CANCEL);
            if (g_rules_start_over_armed) {
                g_rules_start_over_armed = 0;
                g_dirty = 1;
            } else {
                go_to_chooser();
            }
            break;
        default:
            break;
        }
        break;

    case ST_CLOSED:
        break;
    }
}

int screen_is_open(void)
{
    return g_state != ST_CLOSED;
}

void screen_open(void)
{
    {
        /* For checking the hints against the key config screen. */
        const SokuLib::KeyBindings &k = SokuLib::profile1.keyboardBindings;
        const SokuLib::KeyBindings &p = SokuLib::profile1.controllerBindings;
        slog("bindings: keyboard a=%d b=%d c=%d d=%d, pad a=%d b=%d c=%d d=%d", k.a, k.b, k.c, k.d, p.a, p.b, p.c, p.d);
    }
    g_error[0] = 0;
    g_status[0] = 0;
    load_data();
    if (g_data_ok)
        scan_saves();
    g_state = ST_CHOOSE;
    g_cursor = g_character;
    g_alt_armed = 0;
    g_dirty = 1;
    SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CONFIRM);
}

/* True on the first frame held, then repeatedly while held. */
static int repeats(int held)
{
    if (held < 0)
        held = -held;
    return held == 1 || (held >= 30 && held % 6 == 0);
}

void screen_update(const SokuLib::KeyInput &input)
{
    if (input.horizontalAxis < 0 && repeats(input.horizontalAxis))
        handle_event(EV_LEFT);
    else if (input.horizontalAxis > 0 && repeats(input.horizontalAxis))
        handle_event(EV_RIGHT);
    if (input.verticalAxis < 0 && repeats(input.verticalAxis))
        handle_event(EV_UP);
    else if (input.verticalAxis > 0 && repeats(input.verticalAxis))
        handle_event(EV_DOWN);
    if (input.a == 1)
        handle_event(EV_CONFIRM);
    else if (input.b == 1)
        handle_event(EV_CANCEL);
    else if (input.c == 1)
        handle_event(EV_ALT);
    else if (input.d == 1)
        handle_event(EV_TOGGLE);
}

/* ---------- drawing ---------- */

static void init_font(SokuLib::SWRFont &font, int height, int weight)
{
    SokuLib::FontDescription desc;

    memset(&desc, 0, sizeof desc);
    desc.r1 = desc.g1 = desc.b1 = 255;
    desc.r2 = desc.g2 = desc.b2 = 255;
    desc.height = height;
    desc.weight = weight;
    desc.italic = 0;
    desc.shadow = 2;
    desc.bufferSize = 1000000;
    desc.useOffset = 0;
    strcpy(desc.faceName, SokuLib::defaultFontName);
    font.create();
    font.setIndirect(desc);
}

static void ensure_fonts(void)
{
    if (g_fonts_ready)
        return;
    init_font(g_font_big, 20, FW_BOLD);
    init_font(g_font_small, 14, FW_NORMAL);
    g_fonts_ready = 1;
}

/* One line of the main menu's hint, with its right edge at HINT_RIGHT. */
#define HINT_RIGHT 626
static void make_hint_line(Sprite *s, const char *text, int y, unsigned color)
{
    SokuLib::Vector2i real;

    if (!s->texture.createFromText(text, g_font_small, {300, 20}, &real))
        return;
    s->setSize(real.to<unsigned>());
    s->rect.left = 0;
    s->rect.top = 0;
    s->rect.width = real.x;
    s->rect.height = real.y;
    s->setPosition({HINT_RIGHT - real.x, y});
    s->tint = color;
}

void screen_render_title_hint(void)
{
    static Sprite lines[2];
    static int shown_for = -2;
    int blocked = !g_active && g_rules_changed_character >= 0;
    int now = blocked ? 100 + g_rules_changed_character : g_active ? g_active_character : -1;
    int i;

    /* The text only needs making again when the run in use changes. */
    if (now != shown_for) {
        char text[64];

        ensure_fonts();
        make_hint_line(&lines[0], "F7: Slay the Scarlet", 422, COLOR_WHITE);
        if (blocked)
            sprintf(text, "%s: rules changed", sts_character_name(g_rules_changed_character));
        else if (g_active)
            sprintf(text, "In use: %s", sts_character_name(g_active_character));
        else
            sprintf(text, "No run in use");
        make_hint_line(&lines[1], text, 438, blocked ? COLOR_RED : g_active ? COLOR_GREEN : COLOR_GREY);
        shown_for = now;
    }
    for (i = 0; i < 2; i++)
        lines[i].draw();
}

static void add_label(const char *text, SokuLib::SWRFont &font, int x, int y, int w, int h, unsigned color)
{
    Sprite *s;
    SokuLib::Vector2i real;

    if (!text || !*text || g_label_count >= MAX_TEXTS)
        return;
    s = &g_labels[g_label_count];
    if (!s->texture.createFromText(text, font, {(unsigned)w, (unsigned)h}, &real))
        return;
    s->setSize(real.to<unsigned>());
    s->rect.left = 0;
    s->rect.top = 0;
    s->rect.width = real.x;
    s->rect.height = real.y;
    s->setRotation(0.0f);
    s->setPosition({x, y});
    s->tint = color;
    g_label_count++;
}

/* The player's real button, as the game's key config shows it: "Z or Pad2". Takes one of the
 * fields of KeyBindings. Keeps a few buffers so one line of text can name several buttons. */
static const char *button_name(int SokuLib::KeyBindings::*field)
{
    static char buffers[4][48];
    static int next = 0;
    char *out = buffers[next];
    char key[32] = "";
    int dik = SokuLib::profile1.keyboardBindings.*field;
    int pad = SokuLib::profile1.controllerBindings.*field;
    /* The scan code goes in bits 16-23, with bit 24 marking DirectInput's extended keys. */
    LONG param = (LONG)((dik & 0x7F) << 16) | ((dik & 0x80) ? (1L << 24) : 0);

    next = (next + 1) % 4;
    if (!GetKeyNameTextA(param, key, sizeof key))
        sprintf(key, "Key%d", dik);
    /* The key config screen counts pad buttons from 1. */
    if (pad >= 0)
        _snprintf(out, 47, "%s or Pad%d", key, pad + 1);
    else
        _snprintf(out, 47, "%s", key);
    out[47] = 0;
    return out;
}

#define BUTTON_A button_name(&SokuLib::KeyBindings::a)
#define BUTTON_B button_name(&SokuLib::KeyBindings::b)
#define BUTTON_C button_name(&SokuLib::KeyBindings::c)
#define BUTTON_D button_name(&SokuLib::KeyBindings::d)

/* Skill inputs use the game's own deck screen images, one per input, in the skill's
 * cost_or_slot order. Each has "Skill Card" on its top half and the arrows and button name
 * on its bottom half; only the arrows are drawn. The positions were measured from the images:
 * three arrows for most inputs, two for 22. */
#define SKILL_INPUT_IMAGES 5
#define SKILL_INPUT_TOP 16
#define SKILL_INPUT_H 14
static const int g_input_left[SKILL_INPUT_IMAGES] = { 5, 5, 5, 5, 6 };
static const int g_input_width[SKILL_INPUT_IMAGES] = { 31, 31, 31, 31, 18 };

typedef struct {
    int image;
    int x, y;
} input_draw;

static Sprite g_input_sprites[SKILL_INPUT_IMAGES];
static int g_input_loaded[SKILL_INPUT_IMAGES];
static int g_inputs_ready = 0;
static input_draw g_input_draws[STS_OFFER_MAX + 1];
static int g_input_draw_count = 0;

static void load_skill_input_images(void)
{
    int i;

    if (g_inputs_ready)
        return;
    g_inputs_ready = 1;
    for (i = 0; i < SKILL_INPUT_IMAGES; i++) {
        char path[64];
        Sprite *s = &g_input_sprites[i];

        sprintf(path, "data/profile/deck2/022_SkillCard%03dCost.bmp", i);
        g_input_loaded[i] = s->texture.loadFromGame(path);
        if (!g_input_loaded[i]) {
            slog("could not load %s", path);
            continue;
        }
        s->setSize({(unsigned)g_input_width[i], SKILL_INPUT_H});
        s->rect.left = g_input_left[i];
        s->rect.top = SKILL_INPUT_TOP;
        s->rect.width = g_input_width[i];
        s->rect.height = SKILL_INPUT_H;
        s->tint = COLOR_GOLD;
    }
}

/* Queues a skill's input image for drawing. */
static void add_skill_input(int slot, int x, int y)
{
    load_skill_input_images();
    if (slot >= 0 && slot < SKILL_INPUT_IMAGES && g_input_loaded[slot]
        && g_input_draw_count < (int)(sizeof g_input_draws / sizeof g_input_draws[0])) {
        input_draw *d = &g_input_draws[g_input_draw_count++];
        d->image = slot;
        d->x = x;
        d->y = y;
    }
}

/* Copies up to cols bytes of s into out, never splitting a two-byte Shift-JIS character.
 * With at_spaces set, a line that does not reach the end of s breaks at its last space.
 * Returns how many bytes of s were used. */
static size_t take_line(const char *s, int cols, int at_spaces, char *out)
{
    size_t n = 0, space = 0;
    size_t len;

    while (s[n] && n < (size_t)cols) {
        size_t step = IsDBCSLeadByteEx(932, (BYTE)s[n]) && s[n + 1] ? 2 : 1;
        if (n + step > (size_t)cols)
            break;
        if (s[n] == ' ')
            space = n;
        n += step;
    }
    if (at_spaces && s[n] && s[n] != ' ' && space > 0)
        n = space;
    memcpy(out, s, n);
    len = n;
    while (len > 0 && out[len - 1] == ' ')
        len--;
    out[len] = 0;
    return n;
}

static void add_wrapped(const char *text, SokuLib::SWRFont &font, int x, int y, int w, int line_h,
                        int cols, int at_spaces, int max_lines, unsigned color)
{
    char line[DESC_SIZE];
    int row;

    if (!text)
        return;
    for (row = 0; row < max_lines && *text; row++) {
        text += take_line(text, cols, at_spaces, line);
        while (at_spaces && *text == ' ')
            text++;
        add_label(line, font, x, y + row * line_h, w, line_h + 4, color);
    }
}

static int load_card_sprite(Sprite *s, uint16_t id, int x, int y, unsigned scale)
{
    char path[128];
    SokuLib::Vector2u size;

    if (id < 100)
        sprintf(path, "data/card/common/card%03u.bmp", (unsigned)id);
    else
        sprintf(path, "data/card/%.64s/card%03u.bmp", SokuLib::getCharName((unsigned)g_run.character), (unsigned)id);
    if (!s->texture.loadFromGame(path))
        return 0;
    size = s->texture.getSize();
    s->setSize({size.x * scale, size.y * scale});
    s->rect.left = 0;
    s->rect.top = 0;
    s->rect.width = (int)size.x;
    s->rect.height = (int)size.y;
    s->setPosition({x, y});
    return 1;
}

static void load_art(int index, uint16_t id, int x, int y)
{
    g_has_art[index] = load_card_sprite(&g_art[index], id, x, y, 2);
}

/* Reading card files takes a moment, so the deck images load once per deck rather than
 * on every cursor move. */
static void load_deck_art(void)
{
    int i;

    for (i = 0; i < MAX_DECK_ART; i++)
        g_has_deck_art[i] = i < g_run.deck.count
            && load_card_sprite(&g_deck_art[i], g_run.deck.ids[i],
                                DECK_X + (i % DECK_COLS) * DECK_PITCH_X, DECK_Y + (i / DECK_COLS) * DECK_PITCH_Y, 1);
    g_deck_art_dirty = 0;
}

/* The card's type line. Skills also show their input and button, as the game's deck screen does. */
static void add_card_kind(const sts_card *c, int x, int y, int w)
{
    char text[48], skill[STS_SKILL_LABEL_SIZE];

    switch (c->type) {
    case STS_CARD_SYSTEM:
        sprintf(text, "System, cost %d", c->cost_or_slot);
        break;
    case STS_CARD_SPELL:
        sprintf(text, "Spell, cost %d", c->cost_or_slot);
        break;
    case STS_CARD_SKILL:
        if (sts_skill_label(&g_db, g_run.character, c->id, skill)) {
            int start = x, before = g_label_count, number_w = 0, slot = c->cost_or_slot;

            /* "Skill, a" and "214" are separate so the arrows can centre over the digits. */
            sprintf(text, "Skill, %c", skill[0]);
            add_label(text, g_font_small, x, y, w, 20, COLOR_GOLD);
            if (g_label_count > before)
                x += (int)g_labels[before].getSize().x;
            before = g_label_count;
            add_label(skill + 1, g_font_small, x, y, 40, 20, COLOR_GOLD);
            if (g_label_count > before)
                number_w = (int)g_labels[before].getSize().x;
            if (slot < SKILL_INPUT_IMAGES) {
                int arrows_x = x + (number_w - g_input_width[slot]) / 2;
                add_skill_input(slot, arrows_x > start ? arrows_x : start, y - SKILL_INPUT_H);
            }
            return;
        }
        sprintf(text, "Skill");
        break;
    default:
        text[0] = 0;
        break;
    }
    add_label(text, g_font_small, x, y, w, 20, COLOR_GOLD);
}

#define OFFER_W 180
#define OFFER_GAP 16
#define OFFER_Y 70

static int offer_x(int i, int count)
{
    int total = count * OFFER_W + (count - 1) * OFFER_GAP;
    return (SCREEN_W - total) / 2 + i * (OFFER_W + OFFER_GAP);
}

static void rebuild_choose(void)
{
    int c;
    char line[128];

    add_label("Slay the Scarlet: choose a character", g_font_big, 20, 16, 600, 30, COLOR_WHITE);
    for (c = 0; c < STS_NUM_CHARACTERS; c++) {
        int x = 24 + (c % COLS) * 120;
        int y = 80 + (c / COLS) * 70;
        unsigned color = COLOR_WHITE;

        add_label(sts_character_name(c), g_font_big, x + 6, y + 6, 110, 26, COLOR_WHITE);
        line[0] = 0;
        if (g_has_save[c]) {
            if (g_saved_rules_changed[c]) {
                sprintf(line, "Rules changed");
                color = COLOR_RED;
            } else if (g_saved_phase[c] == STS_PHASE_DRAFT) {
                sprintf(line, "Draft %d/%d", g_saved_picks[c], g_settings.draft_picks);
                color = COLOR_GOLD;
            } else if (g_saved_phase[c] == STS_PHASE_REWARD) {
                sprintf(line, "Reward waiting");
                color = COLOR_GOLD;
            } else if (g_active && g_active_character == c) {
                sprintf(line, "In use");
                color = COLOR_GREEN;
            } else {
                sprintf(line, "Deck ready");
                color = COLOR_GREY;
            }
        }
        add_label(line, g_font_small, x + 6, y + 32, 110, 20, color);
    }
    if (g_alt_armed)
        sprintf(line, "Press %s again to discard this character's run and start over.", BUTTON_C);
    else
        sprintf(line, "%s: open   %s: close   %s twice: start over", BUTTON_A, BUTTON_B, BUTTON_C);
    add_label(line, g_font_small, 20, 440, 600, 20, g_alt_armed ? COLOR_RED : COLOR_GREY);
}

static void rebuild_draft(void)
{
    char line[192];
    int i;

    if (g_state == ST_REWARD) {
        const char *verb = g_settings.reward.mode == STS_REWARD_ADD ? "add"
                         : g_settings.reward.mode == STS_REWARD_REMOVE ? "remove" : "swap in";
        sprintf(line, "%s: choose a card to %s", sts_character_name(g_run.character), verb);
        add_label(line, g_font_big, 20, 16, 470, 30, COLOR_WHITE);
        sprintf(line, "Match %d", g_run.matches_played);
        add_label(line, g_font_small, 500, 22, 130, 20, COLOR_GREY);
    } else {
        sprintf(line, "%s: pick %d of %d", sts_character_name(g_run.character),
                g_run.picks_done + 1, g_settings.draft_picks);
        add_label(line, g_font_big, 20, 16, 400, 30, COLOR_WHITE);
        sprintf(line, "Deck %d/%d", g_run.deck.count, g_settings.rules.deck_min);
        add_label(line, g_font_small, 500, 22, 130, 20, COLOR_GREY);
    }

    for (i = 0; i < g_run.offer_count && i < STS_OFFER_MAX; i++) {
        const sts_offer *o = &g_run.offers[i];
        const card_text *t = find_text(o->id);
        const sts_card *c = sts_card_db_find(&g_db, g_run.character, o->id);
        int x = offer_x(i, g_run.offer_count);

        load_art(i, o->id, x + 8, OFFER_Y + 8);
        add_wrapped(t ? t->name : "?", g_font_small, x + 8, OFFER_Y + 146, OFFER_W - 16, 17, 22, 1, 3, COLOR_WHITE);
        if (c)
            add_card_kind(c, x + 8, OFFER_Y + 202, OFFER_W - 16);
        if (o->copies > 1) {
            sprintf(line, "x%d", o->copies);
            add_label(line, g_font_big, x + OFFER_W - 40, OFFER_Y + 8, 36, 28, COLOR_GOLD);
        }
    }
    if (g_cursor < g_run.offer_count) {
        const card_text *t = find_text(g_run.offers[g_cursor].id);
        /* The game's own descriptions are laid out in rows of exactly this many bytes. */
        if (t)
            add_wrapped(t->desc, g_font_small, 24, OFFER_Y + 236, 592, 17, 36, 0, 6, COLOR_WHITE);
    }
    if (g_state == ST_REWARD)
        sprintf(line, "Left/Right: choose   %s: take", BUTTON_A);
    else
        sprintf(line, "Left/Right: choose   %s: take   %s: back (progress is saved)", BUTTON_A, BUTTON_B);
    add_label(line, g_font_small, 20, 440, 600, 20, COLOR_GREY);
}

/* Name, type and description of one card, starting at y. */
static void add_card_details(uint16_t id, int y)
{
    const card_text *t = find_text(id);
    const sts_card *c = sts_card_db_find(&g_db, g_run.character, id);

    /* A skill's arrows sit above its type line, so they need room under the name. */
    int kind_y = y + (c && c->type == STS_CARD_SKILL ? 46 : 28);

    add_label(t ? t->name : "?", g_font_big, 24, y, 592, 28, COLOR_WHITE);
    if (c)
        add_card_kind(c, 24, kind_y, 592);
    if (t)
        add_wrapped(t->desc, g_font_small, 24, kind_y + 20, 592, 17, 36, 0, 6, COLOR_WHITE);
}

/* One heading and its cards. Returns the y position after them. */
static int add_change_section(const char *heading, const deck_change *list, int count, int y, unsigned color)
{
    const int x = 288;
    int i;

    if (count == 0)
        return y;
    add_label(heading, g_font_small, x, y, 340, 20, color);
    y += 18;
    for (i = 0; i < count; i++) {
        const card_text *t = find_text(list[i].id);
        const sts_card *c = sts_card_db_find(&g_db, g_run.character, list[i].id);
        char prefix[STS_SKILL_LABEL_SIZE + 8] = "";
        char text[NAME_SIZE + 32];

        /* "a22 Taste of Death", "4-cost Death Sign ...": what matters most about each type. */
        if (c && c->type == STS_CARD_SKILL) {
            char skill[STS_SKILL_LABEL_SIZE];
            if (sts_skill_label(&g_db, g_run.character, c->id, skill))
                sprintf(prefix, "%s ", skill);
        } else if (c && c->type == STS_CARD_SPELL) {
            sprintf(prefix, "%d-cost ", c->cost_or_slot);
        }
        if (list[i].copies > 1)
            _snprintf(text, sizeof text - 1, "%s%s x%d", prefix, t ? t->name : "?", list[i].copies);
        else
            _snprintf(text, sizeof text - 1, "%s%s", prefix, t ? t->name : "?");
        text[sizeof text - 1] = 0;
        add_label(text, g_font_small, x + 8, y, 340, 20, COLOR_WHITE);
        y += 17;
    }
    return y + 6;
}

/* What a reward changed, or is about to, in the space beside the card details. y is the top of
 * those details; the list starts below their first line, which a long card name fills. */
static void add_change_list(int y, const char *removed_heading, const char *added_heading)
{
    y += 30;
    y = add_change_section(removed_heading, g_changes_removed, g_changes_removed_count, y, COLOR_RED);
    add_change_section(added_heading, g_changes_added, g_changes_added_count, y, COLOR_GREEN);
}

static void rebuild_reward_remove(void)
{
    char line[192];
    const sts_offer *o = &g_run.offers[g_pending_offer];
    const card_text *t = find_text(o->id);
    int remaining = o->copies - g_selected_count;

    if (g_deck_art_dirty)
        load_deck_art();
    if (g_state == ST_REWARD_CONFIRM) {
        add_label("Choose what leaves your deck", g_font_big, 20, 16, 600, 30, COLOR_WHITE);
        add_label("Confirm?", g_font_big, 520, 432, 110, 30, COLOR_GOLD);
        if (g_cursor < g_run.deck.count)
            add_card_details(g_run.deck.ids[g_cursor], DECK_Y + 2 * DECK_PITCH_Y + 8);
        add_change_list(DECK_Y + 2 * DECK_PITCH_Y + 8, "Removing", "Adding");
        sprintf(line, "%s: confirm   %s: change the last pick", BUTTON_A, BUTTON_B);
        add_label(line, g_font_small, 20, 440, 600, 20, COLOR_GREY);
        return;
    }
    add_label("Choose what leaves your deck", g_font_big, 20, 16, 600, 30, COLOR_WHITE);
    if (g_cursor < g_run.deck.count)
        add_card_details(g_run.deck.ids[g_cursor], DECK_Y + 2 * DECK_PITCH_Y + 8);
    sprintf(line, "Replacing with %dx %.90s. Choose %d more.", o->copies, t ? t->name : "?", remaining);
    add_label(line, g_font_small, 20, 416, 600, 20, COLOR_GOLD);
    sprintf(line, "Arrows: choose   %s: select   %s: back to the offers", BUTTON_A, BUTTON_B);
    add_label(line, g_font_small, 20, 440, 600, 20, COLOR_GREY);
}

static void rebuild_rules(void)
{
    char line[192];
    int i;

    sprintf(line, "%s: the rules have changed", sts_character_name(g_run.character));
    add_label(line, g_font_big, 20, 16, 600, 30, COLOR_WHITE);
    add_label("This run was made under different rules from the current settings.",
              g_font_small, 40, 80, 580, 20, COLOR_WHITE);
    add_label(g_rules_problem, g_font_small, 40, 106, 580, 20, COLOR_RED);

    for (i = 0; i < rules_option_count(); i++) {
        int y = RULES_ROW_Y + i * RULES_ROW_PITCH;
        const char *note = NULL;

        switch (rules_option_at(i)) {
        case RULES_CARRY_ON:
            add_label("Carry on under the current rules", g_font_big, 56, y, 500, 28, COLOR_WHITE);
            note = "Your deck and progress stay as they are.";
            break;
        case RULES_BACK:
            add_label("Back", g_font_big, 56, y, 500, 28, COLOR_WHITE);
            note = "Leaves the run as it is.";
            break;
        case RULES_START_OVER:
            add_label("Start over", g_font_big, 56, y, 500, 28, g_rules_start_over_armed ? COLOR_RED : COLOR_WHITE);
            note = "Discards this run's deck and progress.";
            break;
        }
        add_label(note, g_font_small, 72, y + 26, 500, 20, COLOR_GREY);
    }
    if (g_rules_start_over_armed)
        sprintf(line, "Press %s again to discard this run.   %s: cancel", BUTTON_A, BUTTON_B);
    else
        sprintf(line, "Up/Down: choose   %s: select   %s: back", BUTTON_A, BUTTON_B);
    add_label(line, g_font_small, 20, 440, 600, 20, g_rules_start_over_armed ? COLOR_RED : COLOR_GREY);
}

static void rebuild_reward_type(void)
{
    static const char *const names[3] = { "System cards", "Skill cards", "Spell cards" };
    char line[192];
    const sts_offer *o = &g_run.offers[g_pending_offer];
    const card_text *t = find_text(o->id);
    unsigned mask = sts_replace_types_available(&g_db, g_run.character, &g_run.deck, o->copies);
    int type, i;

    add_label("Choose what type leaves your deck", g_font_big, 20, 16, 600, 30, COLOR_WHITE);
    for (type = 0; type < 3; type++) {
        int count = 0;
        for (i = 0; i < g_run.deck.count; i++) {
            const sts_card *c = sts_card_db_find(&g_db, g_run.character, g_run.deck.ids[i]);
            if (c && c->type == type)
                count++;
        }
        sprintf(line, "%s (%d in deck)", names[type], count);
        add_label(line, g_font_big, 56, 110 + type * 50, 500, 28,
                  (mask & STS_TYPE_BIT(type)) ? COLOR_WHITE : COLOR_GREY);
    }
    sprintf(line, "Replacing with %dx %.90s. Random cards of that type leave.", o->copies, t ? t->name : "?");
    add_label(line, g_font_small, 20, 416, 600, 20, COLOR_GOLD);
    sprintf(line, "Up/Down: choose   %s: confirm   %s: back to the offers", BUTTON_A, BUTTON_B);
    add_label(line, g_font_small, 20, 440, 600, 20, COLOR_GREY);
}

static void rebuild_deck(void)
{
    char line[192];
    int rows = (g_run.deck.count + DECK_COLS - 1) / DECK_COLS;
    int y;

    if (g_deck_art_dirty)
        load_deck_art();

    sprintf(line, "%s: your deck", sts_character_name(g_run.character));
    add_label(line, g_font_big, 20, 16, 400, 30, COLOR_WHITE);
    sprintf(line, "%d cards", g_run.deck.count);
    add_label(line, g_font_small, 540, 22, 90, 20, COLOR_GREY);

    /* Details of the card under the cursor, below however many rows the deck needs. */
    if (rows > 2)
        rows = 2;
    y = DECK_Y + rows * DECK_PITCH_Y + 8;
    if (g_cursor < g_run.deck.count)
        add_card_details(g_run.deck.ids[g_cursor], y);
    add_change_list(y, "Removed", "Added");
    add_label(g_status, g_font_small, 20, 396, 600, 20, run_deck_in_use() ? COLOR_GREEN : COLOR_GOLD);
    sprintf(line, "%s: %s", BUTTON_D, run_deck_in_use() ? "stop using this run" : "use this run");
    add_label(line, g_font_small, 20, 416, 600, 20, COLOR_WHITE);
    if (g_alt_armed)
        sprintf(line, "Press %s again to discard this run and start over.", BUTTON_C);
    else
        sprintf(line, "Arrows: inspect   %s: back   %s twice: start over", BUTTON_B, BUTTON_C);
    add_label(line, g_font_small, 20, 440, 600, 20, g_alt_armed ? COLOR_RED : COLOR_GREY);
}

static void rebuild(void)
{
    int i;

    ensure_fonts();
    g_label_count = 0;
    g_input_draw_count = 0;
    for (i = 0; i < STS_OFFER_MAX; i++)
        g_has_art[i] = 0;

    switch (g_state) {
    case ST_CHOOSE: rebuild_choose(); break;
    case ST_DRAFT:
    case ST_REWARD:
        rebuild_draft();
        break;
    case ST_DECK:          rebuild_deck(); break;
    case ST_REWARD_REMOVE:
    case ST_REWARD_CONFIRM:
        rebuild_reward_remove();
        break;
    case ST_REWARD_TYPE:   rebuild_reward_type(); break;
    case ST_RULES:         rebuild_rules(); break;
    case ST_CLOSED:        break;
    }
    add_label(g_error, g_font_small, 20, 416, 600, 20, COLOR_RED);
    g_dirty = 0;
}

static void draw_box(int x, int y, int w, int h, unsigned fill, unsigned border)
{
    g_box.setPosition({x, y});
    g_box.setSize({(unsigned)w, (unsigned)h});
    g_box.setFillColor(fill);
    g_box.setBorderColor(border);
    g_box.draw();
}

void screen_render(int opaque)
{
    int i;

    if (g_dirty)
        rebuild();

    draw_box(0, 0, SCREEN_W, SCREEN_H, opaque ? 0xFF000000u : 0xE0000000u, opaque ? 0xFF000000u : 0xE0000000u);
    if (g_state == ST_CHOOSE) {
        int x = 24 + (g_cursor % COLS) * 120;
        int y = 80 + (g_cursor / COLS) * 70;
        draw_box(x, y, 116, 60, 0x60FFE080u, 0xFFFFE080u);
    } else if (g_state == ST_REWARD_TYPE) {
        draw_box(44, 104 + g_cursor * 50, 552, 38, 0x60FFE080u, 0xFFFFE080u);
    } else if (g_state == ST_RULES) {
        draw_box(44, RULES_ROW_Y - 6 + g_cursor * RULES_ROW_PITCH, 552, 58,
                 g_rules_start_over_armed ? 0x60FF6060u : 0x60FFE080u,
                 g_rules_start_over_armed ? 0xFFFF6060u : 0xFFFFE080u);
    } else if (g_state == ST_DRAFT || g_state == ST_REWARD) {
        for (i = 0; i < g_run.offer_count && i < STS_OFFER_MAX; i++) {
            int selected = i == g_cursor;
            draw_box(offer_x(i, g_run.offer_count), OFFER_Y, OFFER_W, 226,
                     selected ? 0x60FFE080u : 0x40FFFFFFu, selected ? 0xFFFFE080u : 0x80FFFFFFu);
            if (g_has_art[i])
                g_art[i].draw();
        }
    } else if (g_state == ST_DECK || g_state == ST_REWARD_REMOVE || g_state == ST_REWARD_CONFIRM) {
        /* Two rows fit above the card details. A larger deck scrolls with the cursor. */
        int top = g_cursor / DECK_COLS > 0 ? g_cursor / DECK_COLS - 1 : 0;

        for (i = top * DECK_COLS; i < g_run.deck.count && i < MAX_DECK_ART && i < (top + 2) * DECK_COLS; i++) {
            int x = DECK_X + (i % DECK_COLS) * DECK_PITCH_X;
            int y = DECK_Y + (i / DECK_COLS - top) * DECK_PITCH_Y;

            int picked = (g_state == ST_REWARD_REMOVE || g_state == ST_REWARD_CONFIRM) && g_selected[i];

            if (i == g_cursor || picked) {
                /* An even margin around the image, whatever size the game's art is. */
                SokuLib::Vector2u size = g_has_deck_art[i] ? g_deck_art[i].getSize()
                                                           : SokuLib::Vector2u{DECK_PITCH_X - 8, DECK_PITCH_Y - 8};

                draw_box(x - 3, y - 3, (int)size.x + 6, (int)size.y + 6,
                         picked ? 0x80FF6060u : 0x60FFE080u, picked ? 0xFFFF6060u : 0xFFFFE080u);
            }
            if (g_has_deck_art[i]) {
                g_deck_art[i].setPosition({x, y});
                g_deck_art[i].draw();
            }
        }
    }
    for (i = 0; i < g_label_count; i++)
        g_labels[i].draw();
    for (i = 0; i < g_input_draw_count; i++) {
        Sprite *s = &g_input_sprites[g_input_draws[i].image];

        s->setPosition({g_input_draws[i].x, g_input_draws[i].y});
        s->draw();
    }
}
