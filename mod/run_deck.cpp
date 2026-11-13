/* The run deck in the game: writing it into the deck slot, remembering which run is in
 * use, putting the player's real deck back, and refusing to run alongside deck editing mods.
 *
 * There are two players: player 1 on the left with profile1, and player 2 on the right with
 * profile2, in local VS only (player 2 never plays online). Each profile keeps its own runs, in
 * runs\<profile>\. The values below about the run in use belong to the selected player, g_player;
 * select_player (at the end) swaps them, so the rest of the mod handles one player at a time. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "internal.h"

/* The run that was in use when the game closed, if the rules have changed since. Its deck is not
 * used until the player decides what to do about it. -1 if there is no such run. */
int g_rules_changed_character = -1;

void ids_text(char *out, size_t size, SokuLib::Dequeue<unsigned short> &deque)
{
    size_t offset = 0;
    int wau;

    out[0] = 0;
    for (wau = 0; wau < deque.size && wau < 256 && offset + 8 < size; wau++)
        offset += (size_t)sprintf(out + offset, "%u ", (unsigned)deque[wau]);
}

/* The finished deck being played. It is kept apart from g_run, which the screen may be
 * using for another character. */
int g_active = 0;
int g_active_character = 0;
sts_deck g_active_deck;
int g_slot_differs = 0;
/* What the slot held before the run deck: the player's real deck, put back when the run
 * stops being used. In memory only, like the run deck itself. */
static sts_deck g_real_deck;
static int g_have_real_deck = 0;

SokuLib::Profile &player_profile(int player)
{
    return player == 1 ? SokuLib::profile2 : SokuLib::profile1;
}

static SokuLib::Dequeue<unsigned short> &active_slot(void)
{
    return player_profile(g_player).cards[g_active_character][RUN_DECK_SLOT];
}

int deck_is_active(const unsigned short *ids, int count)
{
    static sts_deck given, expected;
    int wau;

    if (!g_active || count != g_active_deck.count)
        return 0;
    sts_deck_clear(&given);
    for (wau = 0; wau < count; wau++)
        sts_deck_add(&given, ids[wau]);
    sts_deck_sort(&given);
    expected = g_active_deck;
    sts_deck_sort(&expected);
    return memcmp(given.ids, expected.ids, (size_t)count * 2) == 0;
}

static int slot_holds_active_deck(void)
{
    auto &slot = active_slot();
    int wau;

    if (slot.size != g_active_deck.count)
        return 0;
    for (wau = 0; wau < g_active_deck.count; wau++)
        if (slot[wau] != g_active_deck.ids[wau])
            return 0;
    return 1;
}

/* Same size only: nothing is allocated, so nothing can go wrong with the game's heap. */
int write_active_deck(char *why, size_t why_size)
{
    auto &slot = active_slot();
    int wau;

    if (slot.size != g_active_deck.count) {
        _snprintf(why, why_size - 1, "the deck slot has %d cards, the run deck has %d",
                  slot.size, g_active_deck.count);
        why[why_size - 1] = 0;
        return 0;
    }
    for (wau = 0; wau < g_active_deck.count; wau++)
        slot[wau] = g_active_deck.ids[wau];
    if (!slot_holds_active_deck()) {
        _snprintf(why, why_size - 1, "it did not read back correctly");
        why[why_size - 1] = 0;
        return 0;
    }
    return 1;
}

/* Deck editing mods that cannot run alongside a run deck, or NULL if none is loaded. EasyDecks
 * reads the deck from profile memory, shows the run deck and saves it over the player's real
 * deck in their .pf. Nothing goes into memory while one of these is loaded. */
const char *blocking_mod(void)
{
    static const char *const names[] = { "EasyDecks", "InfiniteDecks" };
    size_t wau;

    for (wau = 0; wau < sizeof names / sizeof names[0]; wau++) {
        char file[32];

        sprintf(file, "%s.dll", names[wau]);
        if (GetModuleHandleA(file))
            return names[wau];
    }
    return NULL;
}

void remember_real_deck(void)
{
    auto &slot = active_slot();
    int wau;

    sts_deck_clear(&g_real_deck);
    for (wau = 0; wau < slot.size && wau < STS_MAX_DECK; wau++)
        sts_deck_add(&g_real_deck, slot[wau]);
    g_have_real_deck = 1;
}

/* Puts the player's deck back, unless the game has already reloaded it. */
static void restore_real_deck(void)
{
    auto &slot = active_slot();
    int wau;

    if (g_have_real_deck && slot_holds_active_deck() && slot.size == g_real_deck.count) {
        for (wau = 0; wau < g_real_deck.count; wau++)
            slot[wau] = g_real_deck.ids[wau];
        slog("restored the real deck for character %d", g_active_character);
    }
    g_have_real_deck = 0;
}

/* The selected player's profile remembers the run it has in use across restarts, and how many runs
 * were started over, in runs\<profile>\state.txt. */
static sts_state g_saved_state;
static int g_saved_state_loaded = 0;

/* The gallery's cards found, and whether it was unlocked, belong to no one profile: they are in
 * runs\state.txt, in the same format (its run in use and restart counts are not used). */
static sts_state g_gallery_state;
static int g_gallery_state_loaded = 0;

/* Writes a state into folder\state.txt, making the folders first. A new file replaces the old one in
 * one step, so a failed write cannot leave half a file. */
static void write_state_into(const sts_state *state, const char *folder)
{
    char path[MAX_PATH + 96], temporary[MAX_PATH + 104];
    static char text[STS_STATE_TEXT_MAX];
    size_t length = sts_state_serialize(state, text, sizeof text);
    FILE *file;
    int ok;

    sprintf(path, "%s" RUNS_FOLDER, g_dir);
    CreateDirectoryA(path, NULL);
    CreateDirectoryA(folder, NULL);
    sprintf(path, "%sstate.txt", folder);
    sprintf(temporary, "%s.new", path);
    file = length ? fopen(temporary, "wb") : NULL;
    ok = file && fwrite(text, 1, length, file) == length;
    if (file)
        ok = fclose(file) == 0 && ok;
    if (ok)
        ok = MoveFileExA(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) {
        remove(temporary);
        slog("could not write %s", path);
    }
}

static void read_state_from(sts_state *state, const char *folder)
{
    char path[MAX_PATH + 96];
    size_t length = 0;
    char *text;

    sts_state_init(state);
    sprintf(path, "%sstate.txt", folder);
    text = sts_read_file(path, &length);
    if (!text)
        return;
    if (!sts_state_parse(text, length, state))
        slog("%s could not be read, so it is taken as empty", path);
    free(text);
}

static void write_state_file(void)
{
    char folder[MAX_PATH + 80];

    player_runs_folder(folder, sizeof folder);
    write_state_into(&g_saved_state, folder);
}

static void load_state_file(void)
{
    char folder[MAX_PATH + 80];

    if (g_saved_state_loaded)
        return;
    g_saved_state_loaded = 1;
    player_runs_folder(folder, sizeof folder);
    read_state_from(&g_saved_state, folder);
}

static void gallery_folder(char *out)
{
    sprintf(out, "%s" RUNS_FOLDER, g_dir);
}

static void write_gallery_state(void)
{
    char folder[MAX_PATH + 16];

    gallery_folder(folder);
    write_state_into(&g_gallery_state, folder);
}

static void load_gallery_state(void)
{
    char folder[MAX_PATH + 16];

    if (g_gallery_state_loaded)
        return;
    g_gallery_state_loaded = 1;
    gallery_folder(folder);
    read_state_from(&g_gallery_state, folder);
}

/* The set a card counts in: its character's, or the system cards'. */
static int found_set(int character, uint16_t id)
{
    const sts_card *card = sts_card_db_find(&g_db, character, id);

    return card && card->type == STS_CARD_SYSTEM ? STS_STATE_FOUND_SYSTEM : character;
}

static int add_offers_found(int character, const sts_offer *offers, int count)
{
    int changed = 0, wau;

    for (wau = 0; wau < count; wau++)
        changed |= sts_state_add_found(&g_gallery_state, found_set(character, offers[wau].id), offers[wau].id);
    return changed;
}

/* The offers on screen, and with offer_pick random the ones the pick was made from, which the roulette
 * runs through. */
static int add_run_offers_found(const sts_run *run)
{
    if (run->character < 0 || run->character >= STS_NUM_CHARACTERS)
        return 0;
    return add_offers_found(run->character, run->offers, run->offer_count)
        | add_offers_found(run->character, run->rolled, run->rolled_count);
}

/* A starting deck's cards count as found too, as they are never offered. */
static int add_deck_found(int character, const sts_deck *deck)
{
    int changed = 0, wau;

    if (character < 0 || character >= STS_NUM_CHARACTERS)
        return 0;
    for (wau = 0; wau < deck->count; wau++)
        changed |= sts_state_add_found(&g_gallery_state, found_set(character, deck->ids[wau]), deck->ids[wau]);
    return changed;
}

void note_found_deck(int character, const sts_deck *deck)
{
    load_gallery_state();
    if (add_deck_found(character, deck))
        write_gallery_state();
}

void note_found_offers(const sts_run *run)
{
    load_gallery_state();
    if (add_run_offers_found(run))
        write_gallery_state();
}

void note_found_from_saves(void)
{
    static sts_run run;
    char path[MAX_PATH + 96];
    int character, changed = 0, player, selected = g_player;

    load_gallery_state();
    /* Both players' runs: whichever profile they are on. */
    for (player = 0; player < RUN_PLAYERS; player++) {
        if (!player_available(player))
            continue;
        select_player(player);
    for (character = 0; character < STS_NUM_CHARACTERS; character++) {
        if (!run_file_exists(character))
            continue;
        run_path(path, sizeof path, character);
        if (sts_run_load(path, &run) != STS_RUN_OK)
            continue;
        changed |= add_run_offers_found(&run);
        /* The starting deck: during the draft it is in the deck, and afterwards it is in the deck the
         * draft left (with the picks, which were offered anyway). */
        if (run.phase == STS_PHASE_DRAFT) {
            changed |= add_deck_found(character, &run.deck);
        } else {
            sts_deck deck;

            if (sts_run_deck_after(&run, -1, &deck))
                changed |= add_deck_found(character, &deck);
        }
    }
    }
    select_player(selected);
    if (changed)
        write_gallery_state();
}

int gallery_unlocked(void)
{
    load_gallery_state();
    return g_gallery_state.gallery_unlocked;
}

void unlock_gallery(void)
{
    load_gallery_state();
    g_gallery_state.gallery_unlocked = 1;
    write_gallery_state();
    slog("the gallery was unlocked");
}

int card_found(int character, uint16_t id)
{
    load_gallery_state();
    if (character < 0)
        return sts_state_found(&g_gallery_state, STS_STATE_FOUND_SYSTEM, id);
    return sts_state_found(&g_gallery_state, found_set(character, id), id);
}

void save_active_file(void)
{
    load_state_file();
    g_saved_state.active = g_active ? g_active_character : -1;
    write_state_file();
}

int restart_count(int character, uint32_t rules_hash)
{
    load_state_file();
    return sts_state_restarts(&g_saved_state, character, rules_hash);
}

void count_restart(int character, uint32_t rules_hash)
{
    load_state_file();
    sts_state_add_restart(&g_saved_state, character, rules_hash);
    write_state_file();
}

void deactivate_run_deck(void)
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
int activate_run_deck(void)
{
    char why[96] = "";
    int fresh = !g_active || g_active_character != g_run.character;

    if (blocking_mod()) {
        set_error(tr("Cannot use this deck"), tr("another mod is loaded that could overwrite your real deck"));
        return 0;
    }
    if (g_run.phase == STS_PHASE_FINISHED)
        return 0;
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
        set_error(tr("Cannot use this deck"), why);
        slog("activate failed for character %d: %s", g_active_character, why);
        return 0;
    }
    save_active_file();
    g_rules_changed_character = -1;
    slog("using run deck for character %d, slot %d, %d cards", g_active_character, RUN_DECK_SLOT, g_active_deck.count);
    return 1;
}

int run_deck_in_use(void)
{
    return g_active && g_active_character == g_run.character;
}

/* Goes back to the run the selected player's profile had in use last time the game was open. */
static void resume_active_run(void)
{
    char path[MAX_PATH + 96];
    int character = -1;

    /* state.txt keeps naming it, so the run comes back once the other mod is disabled. */
    if (blocking_mod()) {
        slog("%s is loaded, so no run deck will be used", blocking_mod());
        return;
    }
    load_state_file();
    character = g_saved_state.active;
    if (character < 0)
        return;

    if (character < STS_NUM_CHARACTERS) {
        sts_run_status status;

        /* A typo in settings.ini is no reason to forget which run was in use. */
        if (!load_data()) {
            slog("could not resume the run for character %d: %s", character, g_error);
            return;
        }
        run_path(path, sizeof path, character);
        status = sts_run_load(path, &g_run);
        /* A finished run is not played any more. */
        if (status == STS_RUN_OK && g_run.phase == STS_PHASE_FINISHED)
            status = STS_RUN_WRONG_PHASE;
        if (status == STS_RUN_OK)
            status = sts_run_validate(&g_run, &g_db, &g_settings);
        if (status == STS_RUN_SETTINGS_CHANGED) {
            g_rules_changed_character = character;
            slog("the rules changed since the run for character %d was saved", character);
            return;
        }
        if (status == STS_RUN_OK && g_run.phase != STS_PHASE_DRAFT) {
            activate_run_deck();
            /* An offer left on screen when the game closed comes straight back. */
            if (g_active && g_run.phase == STS_PHASE_REWARD)
                g_match_reward_due[g_player] = 1;
            return;
        }
        slog("could not resume the run for character %d: %s", character, sts_run_status_str(status));
    }
    /* Whatever state.txt named cannot be used, so stop asking for it. */
    g_saved_state.active = -1;
    write_state_file();
}

/* New settings changed the rules of the run in use. As at start-up, the player's own deck goes back
 * and state.txt keeps naming the run, so it comes back if the rules are changed back. */
void suspend_run_deck_for_rules(void)
{
    if (!g_active)
        return;
    restore_real_deck();
    g_active = 0;
    g_slot_differs = 0;
    g_rules_changed_character = g_active_character;
    slog("the new settings changed the rules of the run for character %d, so it is no longer used", g_active_character);
}

void note_slot_change(const char *where)
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

/* ---------- the two players ---------- */

int g_player = 0;

/* Each profile's runs folder: the profile's file name without .pf, with anything but letters, digits,
 * spaces and - _ ( ) . replaced, and then a hash of the whole name so two names cannot meet. */
#define PROFILE_KEY_SIZE 64
static char g_profile_keys[RUN_PLAYERS][PROFILE_KEY_SIZE];
static int g_resumed[RUN_PLAYERS];

static void profile_key(int player, char *out)
{
    const char *file = "", *name, *character;
    size_t length, used = 0;
    int replaced = 0;

    __try {
        file = (const char *)player_profile(player).file;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        file = "";
    }
    if (!file)
        file = "";
    name = file;
    for (character = file; *character; character++)
        if (*character == '/' || *character == '\\')
            name = character + 1;
    length = strlen(name);
    if (length > 3 && _stricmp(name + length - 3, ".pf") == 0)
        length -= 3;
    for (; length > 0 && used < PROFILE_KEY_SIZE - 12; name++, length--) {
        char letter = *name;
        int allowed = (letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z') || (letter >= '0' && letter <= '9')
            || letter == ' ' || letter == '-' || letter == '_' || letter == '(' || letter == ')' || letter == '.';

        out[used++] = allowed ? letter : '_';
        replaced |= !allowed;
    }
    if (used == 0) {
        strcpy(out, "profile");
        used = strlen(out);
        replaced = 1;
    }
    out[used] = 0;
    if (replaced || length > 0)
        sprintf(out + used, "-%08x", (unsigned)sts_fnv1a(STS_FNV_START, file, strlen(file)));
}

static const char *player_key(int player)
{
    if (!g_profile_keys[player][0])
        profile_key(player, g_profile_keys[player]);
    return g_profile_keys[player];
}

int player_available(int player)
{
    if (player == 0)
        return 1;
    return player == 1 && _stricmp(player_key(0), player_key(1)) != 0;
}

const char *player_profile_name(int player)
{
    __try {
        return (const char *)player_profile(player).name;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return "";
    }
}

void player_runs_folder(char *out, size_t size)
{
    _snprintf(out, size - 1, "%s" RUNS_FOLDER "%s\\", g_dir, player_key(g_player));
    out[size - 1] = 0;
}

/* Each player's values while the other is selected. */
typedef struct {
    int active, active_character, slot_differs, rules_changed_character, have_real_deck, state_loaded;
    sts_deck active_deck, real_deck;
    sts_state state;
} player_values;
static player_values g_stored[RUN_PLAYERS];
static int g_stored_ready = 0;

static void put_away(player_values *values)
{
    values->active = g_active;
    values->active_character = g_active_character;
    values->slot_differs = g_slot_differs;
    values->rules_changed_character = g_rules_changed_character;
    values->have_real_deck = g_have_real_deck;
    values->state_loaded = g_saved_state_loaded;
    values->active_deck = g_active_deck;
    values->real_deck = g_real_deck;
    values->state = g_saved_state;
}

static void bring_back(const player_values *values)
{
    g_active = values->active;
    g_active_character = values->active_character;
    g_slot_differs = values->slot_differs;
    g_rules_changed_character = values->rules_changed_character;
    g_have_real_deck = values->have_real_deck;
    g_saved_state_loaded = values->state_loaded;
    g_active_deck = values->active_deck;
    g_real_deck = values->real_deck;
    g_saved_state = values->state;
}

void select_player(int player)
{
    int wau;

    if (player < 0 || player >= RUN_PLAYERS || player == g_player)
        return;
    if (!g_stored_ready) {
        for (wau = 0; wau < RUN_PLAYERS; wau++) {
            memset(&g_stored[wau], 0, sizeof g_stored[wau]);
            g_stored[wau].rules_changed_character = -1;
            sts_state_init(&g_stored[wau].state);
        }
        g_stored_ready = 1;
    }
    put_away(&g_stored[g_player]);
    bring_back(&g_stored[player]);
    g_player = player;
}

/* A player whose profile changed (at the profile menu) starts afresh with that profile's runs. The game
 * has loaded the new profile's decks into memory, so there is no real deck to put back. */
static void forget_player(int player)
{
    select_player(player);
    if (g_active)
        slog("player %d changed profile, so their run is no longer in use", player + 1);
    g_active = 0;
    g_slot_differs = 0;
    g_have_real_deck = 0;
    g_rules_changed_character = -1;
    g_saved_state_loaded = 0;
    g_resumed[player] = 0;
}

void players_on_title_frame(void)
{
    int selected = g_player, player;

    for (player = 0; player < RUN_PLAYERS; player++) {
        char key[PROFILE_KEY_SIZE];

        profile_key(player, key);
        if (strcmp(key, g_profile_keys[player]) == 0)
            continue;
        if (g_profile_keys[player][0])
            forget_player(player);
        strcpy(g_profile_keys[player], key);
        slog("player %d plays on profile '%s', runs in runs\\%s\\", player + 1, player_profile_name(player), key);
    }
    /* On the same profile as player 1, player 2 has no runs of their own: their deck goes back, and
     * nothing is saved, as the folder is player 1's. */
    if (!player_available(1)) {
        select_player(1);
        if (g_active) {
            restore_real_deck();
            g_active = 0;
            g_slot_differs = 0;
            slog("player 2 is on the same profile as player 1, so their run is not used");
        }
    }
    for (player = 0; player < RUN_PLAYERS; player++) {
        if (g_resumed[player] || !player_available(player))
            continue;
        g_resumed[player] = 1;
        select_player(player);
        resume_active_run();
    }
    select_player(selected);
}
