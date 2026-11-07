/* The run deck in the game: writing it into the deck slot, remembering which run is in
 * use, putting the player's real deck back, and refusing to run alongside deck editing mods. */
#include <stdio.h>
#include <string.h>
#include "internal.h"

/* The run that was in use when the game closed, if the rules have changed since. Its deck is not
 * used until the player decides what to do about it. -1 if there is no such run. */
int g_rules_changed_character = -1;

void ids_text(char *out, size_t size, SokuLib::Dequeue<unsigned short> &deque)
{
    size_t offset = 0;
    int i;

    out[0] = 0;
    for (i = 0; i < deque.size && i < 256 && offset + 8 < size; i++)
        offset += (size_t)sprintf(out + offset, "%u ", (unsigned)deque[i]);
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
int write_active_deck(char *why, size_t why_size)
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

/* Deck editing mods that cannot run alongside a run deck, or NULL if none is loaded. EasyDecks
 * reads the deck from profile memory, shows the run deck and saves it over the player's real
 * deck in their .pf. Nothing goes into memory while one of these is loaded. */
const char *blocking_mod(void)
{
    static const char *const names[] = { "EasyDecks", "InfiniteDecks" };
    size_t i;

    for (i = 0; i < sizeof names / sizeof names[0]; i++) {
        char file[32];

        sprintf(file, "%s.dll", names[i]);
        if (GetModuleHandleA(file))
            return names[i];
    }
    return NULL;
}

void remember_real_deck(void)
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
    sprintf(out, "%s" RUNS_FOLDER "active.txt", g_dir);
}

void save_active_file(void)
{
    char path[MAX_PATH + 32];

    active_file_path(path);
    if (g_active) {
        FILE *file = fopen(path, "w");
        if (file) {
            fprintf(file, "%d\n", g_active_character);
            fclose(file);
        }
    } else {
        remove(path);
    }
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
        set_error("Cannot use this deck", "another mod is loaded that could overwrite your real deck");
        return 0;
    }
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

int run_deck_in_use(void)
{
    return g_active && g_active_character == g_run.character;
}

/* Goes back to the run that was in use last time the game was open. */
void resume_active_run(void)
{
    static int done = 0;
    char path[MAX_PATH + 32];
    int character = -1;
    FILE *file;

    if (done)
        return;
    done = 1;
    /* active.txt stays, so the run comes back once the other mod is disabled. */
    if (blocking_mod()) {
        slog("%s is loaded, so no run deck will be used", blocking_mod());
        return;
    }
    active_file_path(path);
    file = fopen(path, "r");
    if (!file)
        return;
    if (fscanf(file, "%d", &character) != 1)
        character = -1;
    fclose(file);

    if (character >= 0 && character < STS_NUM_CHARACTERS) {
        sts_run_status status;

        /* A typo in settings.ini is no reason to forget which run was in use. */
        if (!load_data()) {
            slog("could not resume the run for character %d: %s", character, g_error);
            return;
        }
        run_path(path, sizeof path, character);
        status = sts_run_load(path, &g_run);
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
                g_match_reward_due = 1;
            return;
        }
        slog("could not resume the run for character %d: %s", character, sts_run_status_str(status));
    }
    /* Whatever active.txt named cannot be used, so stop asking for it. */
    active_file_path(path);
    remove(path);
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
