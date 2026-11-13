/* Plays the sounds a card makes in a match, as data/card_sounds.csv lists them (built into the DLL):
 * each of the game's own sound files at its frame, counted from when the card is chosen. Nothing
 * runs in a blocking loop: card_sounds_tick, called once a frame, plays what is due.
 *
 * The files load through the game's sound manager the first time they are needed, and stay loaded
 * until card_sounds_release. */
#include <stdio.h>
#include <string.h>
#include "internal.h"

/* The game's sound manager, which SokuLib keeps with its texture functions (load 0x401AF0, play
 * 0x401D50, remove 0x401BD0). Characters' sound tables are loaded and freed through it. */
#define SOUND_MANAGER_ADDRESS 0x89F9F8
#define SOUND_MANAGER (reinterpret_cast<SokuLib::TextureManager *>(SOUND_MANAGER_ADDRESS))

/* The files loaded so far. Each card uses a few, and one screen shows at most a few dozen cards. */
#define MAX_LOADED_SOUNDS 256

typedef struct {
    int folder;
    int number;
    int handle;     /* 0 if the file could not be loaded */
} loaded_sound;

static sts_sound_table g_table;
static int g_table_state = -1;   /* -1 not read yet, then 0 or 1 */
static loaded_sound g_loaded[MAX_LOADED_SOUNDS];
static int g_loaded_count = 0;

/* What is playing: the plays of one card, and the frame it has reached. */
static const sts_sound_play *g_plays = NULL;
static int g_play_count = 0;
static int g_wait = 0;          /* frames still to wait before the first */
static int g_elapsed = -1;      /* frames since the first, or -1 when nothing plays */
static int g_first_frame = 0;   /* the earliest play's frame, which plays as soon as the wait ends */
static int g_last_frame = 0;
static int g_outcome = 0;       /* which of the card's outcomes, for a card whose sounds vary */
/* How many times each row's card has played, for its outcomes in turn. Kept until the DLL
 * unloads, so leaving the screen and coming back carries on where it was. */
static unsigned g_turns[STS_SOUNDS_MAX_ROWS];
static sts_sound_play g_fallback;

static int load_table(void)
{
    size_t length = 0;
    const char *text;
    int err_line = 0;
    sts_sounds_status status;

    if (g_table_state >= 0)
        return g_table_state;
    text = embedded_data("CARD_SOUNDS", &length);
    if (!text) {
        slog("card sounds: the table is not in the DLL");
        g_table_state = 0;
        return 0;
    }
    status = sts_sounds_parse(text, length, &g_table, &err_line);
    if (status != STS_SOUNDS_OK) {
        slog("card sounds: %s on line %d", sts_sounds_status_str(status), err_line);
        g_table_state = 0;
        return 0;
    }
    slog("card sounds: %d cards, %d sounds", g_table.row_count, g_table.play_count);
    g_table_state = 1;
    return 1;
}

static int sound_handle(int folder, int number)
{
    char path[64];
    const char *name = sts_sound_folder_name(folder);
    int handle = 0, wau;

    for (wau = 0; wau < g_loaded_count; wau++)
        if (g_loaded[wau].folder == folder && g_loaded[wau].number == number)
            return g_loaded[wau].handle;
    if (!name || g_loaded_count >= MAX_LOADED_SOUNDS)
        return 0;
    if (folder == STS_SOUND_COMMON)
        sprintf(path, "data/se/%03d.wav", number);
    else
        sprintf(path, "data/se/%s/%03d.wav", name, number);
    SOUND_MANAGER->loadSound(&handle, path);
    if (!handle)
        slog("card sounds: could not load %s", path);
    /* Kept even when it failed, so a missing file is not tried again every time. */
    g_loaded[g_loaded_count].folder = folder;
    g_loaded[g_loaded_count].number = number;
    g_loaded[g_loaded_count].handle = handle;
    g_loaded_count++;
    return handle;
}

static const sts_sound_row *card_sound_row(int owner, uint16_t id)
{
    if (!load_table())
        return NULL;
    return sts_sounds_find(&g_table, owner, id);
}

static const sts_sound_play *card_sound_plays(const sts_sound_row *row, int outcome)
{
    return &g_table.plays[row->first[outcome]];
}

void card_sounds_start(int owner, uint16_t id, int type, int delay, int outcomes_in_turn)
{
    const sts_sound_row *row = card_sound_row(owner, id);
    int wau;

    if (row) {
        /* A card's outcomes go round, one more each time it plays, where they are wanted. */
        g_outcome = outcomes_in_turn && row->outcomes > 1 ? (int)(g_turns[row - g_table.rows]++ % row->outcomes) : 0;
        g_plays = card_sound_plays(row, g_outcome);
        g_play_count = row->count[g_outcome];
    } else {
        g_outcome = 0;
        /* A card with no row plays the sound every card of its type makes. */
        g_fallback.folder = STS_SOUND_COMMON;
        g_fallback.number = type == STS_CARD_SPELL ? STS_SOUND_SPELL_DECLARED : STS_SOUND_CARD_USED;
        g_fallback.frame = 0;
        g_plays = &g_fallback;
        g_play_count = 1;
    }
    /* Every card starts the moment the wait ends: its plays are counted from its first one, wherever
     * that is in the card's move. The gaps between them stay as they are. */
    g_first_frame = g_play_count > 0 ? g_plays[0].frame : 0;
    g_last_frame = 0;
    for (wau = 0; wau < g_play_count; wau++) {
        if (g_plays[wau].frame < g_first_frame)
            g_first_frame = g_plays[wau].frame;
        if (g_plays[wau].frame > g_last_frame)
            g_last_frame = g_plays[wau].frame;
    }
    g_wait = delay;
    g_elapsed = -1;
}

void card_sounds_stop(void)
{
    g_plays = NULL;
    g_play_count = 0;
    g_elapsed = -1;
    g_wait = 0;
}

void card_sounds_tick(void)
{
    int wau;

    if (!g_plays)
        return;
    if (g_elapsed < 0) {
        if (g_wait > 0) {
            g_wait--;
            return;
        }
        g_elapsed = 0;
    }
    for (wau = 0; wau < g_play_count; wau++) {
        if (g_plays[wau].frame - g_first_frame == g_elapsed) {
            int handle = sound_handle(g_plays[wau].folder, g_plays[wau].number);

            if (handle)
                SOUND_MANAGER->playSound(handle);
        }
    }
    if (++g_elapsed > g_last_frame - g_first_frame)
        card_sounds_stop();
}

/* The offer under the cursor on the draft and reward screens, so its sounds start when it changes. */
static int g_offer_state = -1, g_offer_cursor = -1, g_offer_id = -1, g_offer_round = -1;

void card_sounds_follow_offers(void)
{
    int on_offers = g_settings.card_sounds && (g_state == ST_DRAFT || g_state == ST_REWARD) && g_cursor >= 0
        && g_cursor < g_run.offer_count;
    /* A new set of offers (the next pick, or the next reward) counts as a change even on the same card. */
    int round = g_run.picks_done * 1000 + g_run.history_count;

    if (!on_offers) {
        if (g_offer_state >= 0)
            card_sounds_stop();
        g_offer_state = -1;
        if (g_state == ST_CLOSED)
            card_sounds_release();
        return;
    }
    if (g_offer_state == (int)g_state && g_offer_cursor == g_cursor && g_offer_id == g_run.offers[g_cursor].id
        && g_offer_round == round)
        return;
    g_offer_state = (int)g_state;
    g_offer_cursor = g_cursor;
    g_offer_id = g_run.offers[g_cursor].id;
    g_offer_round = round;
    {
        const sts_card *card = sts_card_db_find(&g_db, g_run.character, g_run.offers[g_cursor].id);

        /* Only the first of a card's outcomes: Reisen's Elixir does not explode on an offer. */
        card_sounds_start(card && card->type == STS_CARD_SYSTEM ? STS_SOUND_SYSTEM : g_run.character,
                          g_run.offers[g_cursor].id, card ? card->type : STS_CARD_SKILL, CARD_SOUNDS_SETTLE_FRAMES, 0);
    }
}

void play_common_sound(int number)
{
    int handle = sound_handle(STS_SOUND_COMMON, number);

    if (handle)
        SOUND_MANAGER->playSound(handle);
}

void card_sounds_release(void)
{
    int wau;

    card_sounds_stop();
    for (wau = 0; wau < g_loaded_count; wau++)
        if (g_loaded[wau].handle)
            SOUND_MANAGER->removeSound(g_loaded[wau].handle);
    g_loaded_count = 0;
}
