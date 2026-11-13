/* The mod's screens: their state, and what each input does. Input becomes events and events
 * drive a small state machine, so a later spectator view can drive the same screen from network
 * events instead. */
#include <stdio.h>
#include <string.h>
#include "internal.h"

screen_state g_state = ST_CLOSED;
int g_cursor = 0;
static int g_character = 0;
int g_alt_armed = 0;
int g_dirty = 1;
char g_error[256];
char g_status[256];

sts_run_phase g_saved_phase[STS_NUM_CHARACTERS];
int g_saved_picks[STS_NUM_CHARACTERS];
int g_has_save[STS_NUM_CHARACTERS];
int g_saved_rules_changed[STS_NUM_CHARACTERS];
uint32_t g_saved_hash[STS_NUM_CHARACTERS];
static rules_option g_rules_options[RULES_OPTION_COUNT];
static int g_rules_option_count = 0;
int g_rules_start_over_armed = 0;
int g_rules_restore_others = 0;
int g_rule_table_top = 0;
char g_rules_problem[128];

/* The reward being resolved. */
int g_pending_offer = 0;
int g_selected[MAX_DECK_ART];
int g_selected_count = 0;
static int g_last_selected = 0;
static int g_close_after_deck = 0;

/* What the last reward changed, listed on the deck screen beside the card details. */
deck_change g_changes_removed[MAX_CHANGE_LINES];
deck_change g_changes_added[MAX_CHANGE_LINES];
int g_changes_removed_count = 0;
int g_changes_added_count = 0;

int g_score_top = 0;
int g_viewed_match = SCORE_FINAL_DECK;
static int g_score_cursor = 0;           /* the entry to come back to from the deck screen */
static sts_deck g_final_deck;            /* the run's own deck, while g_run.deck shows an earlier one */

static void describe_deck_change(const sts_deck *before);
static void clear_deck_change(void);

/* After a reward the deck screen shows the change, and with a time limit moves on by itself after
 * this long, as if the confirm button were pressed. */
#define DECK_SCREEN_FRAMES (15 * 60)
static int g_deck_frames_left = 0;

/* With a time limit, the deck screen after a reward moves on by itself. */
static void start_deck_timer(void)
{
    g_deck_frames_left = g_settings.time_limit > 0 ? DECK_SCREEN_FRAMES : 0;
}

void set_error(const char *what, const char *detail)
{
    _snprintf(g_error, sizeof g_error - 1, "%s: %s", what, detail);
    g_error[sizeof g_error - 1] = 0;
    g_dirty = 1;
}

static void scan_saves(void)
{
    int character;

    for (character = 0; character < STS_NUM_CHARACTERS; character++) {
        char path[MAX_PATH + 32];
        static sts_run tmp;

        run_path(path, sizeof path, character);
        g_has_save[character] = sts_run_load(path, &tmp) == STS_RUN_OK;
        g_saved_phase[character] = tmp.phase;
        g_saved_picks[character] = tmp.picks_done;
        g_saved_hash[character] = tmp.settings_hash;
        /* A finished run is not played any more, so new rules do not matter to it. */
        g_saved_rules_changed[character] = g_has_save[character] && tmp.phase != STS_PHASE_FINISHED
            && tmp.settings_hash != sts_settings_hash(&g_settings);
    }
}

static void set_deck_status(void)
{
    if (run_deck_in_use())
        _snprintf(g_status, sizeof g_status - 1, tr("In use as %s's Deck %d. Your own deck is safe in your profile."),
                  character_label(g_run.character), RUN_DECK_SLOT + 1);
    else
        _snprintf(g_status, sizeof g_status - 1, tr("Not in use. %s's Deck %d is your own deck."),
                  character_label(g_run.character), RUN_DECK_SLOT + 1);
    g_status[sizeof g_status - 1] = 0;
}

static void enter_phase(void)
{
    g_cursor = 0;
    g_alt_armed = 0;
    g_dirty = 1;
    g_deck_art_dirty = 1;
    if (g_run.phase == STS_PHASE_DRAFT) {
        g_state = ST_DRAFT;
    } else if (g_run.phase == STS_PHASE_FINISHED) {
        g_state = ST_SCORE;
        g_score_top = 0;
        g_score_cursor = 0;
    } else {
        g_state = g_run.phase == STS_PHASE_REWARD ? ST_REWARD : ST_DECK;
        /* Opening a run only looks at it. A run already in use picks up any new deck. */
        if (run_deck_in_use())
            activate_run_deck();
        set_deck_status();
    }
}

/* The deck screen, whatever the phase. With offers waiting it leads on to them. */
static void show_deck(void)
{
    enter_phase();
    g_state = ST_DECK;
}

static int on_reward_screen(void)
{
    return g_state == ST_REWARD || g_state == ST_REWARD_TYPE || g_state == ST_REWARD_REMOVE
        || g_state == ST_REWARD_CONFIRM;
}

/* ---------- the roulette ----------
 * A pick made at random is shown, not made, here: the result is known first, and the cursor runs
 * through the offers, slowing down, to land on it. The confirm button skips to the end. */

typedef enum { ROULETTE_THEN_SHOW, ROULETTE_THEN_TIME_OUT } roulette_then;

/* Frames between steps: fast, then slowing down, 144 frames, and a pause on the pick: 3 seconds. */
static const int g_roulette_gaps[] = {3, 3, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 8, 9, 10, 12, 14, 16, 19};
#define ROULETTE_STEPS ((int)(sizeof g_roulette_gaps / sizeof g_roulette_gaps[0]))
#define ROULETTE_HOLD 36

sts_offer g_roulette_offers[STS_OFFER_MAX];
int g_roulette_count = 0;
static int g_roulette_target = 0;
static int g_roulette_step = 0;
static int g_roulette_wait = 0;
static roulette_then g_roulette_then = ROULETTE_THEN_SHOW;

static void start_deck_timer(void);
static void time_out_reward_at(int index);
static void start_pending_sweep(void);
static int g_sweep_pending = 0;   /* a sweep is set up, to start when the roulette ends */

/* Returns 0, showing nothing, if the roulette is off or there was only one offer. */
static int start_roulette(const sts_offer *offers, int count, int target, roulette_then then)
{
    if (!g_settings.reward_roulette || count < 2 || count > STS_OFFER_MAX || target < 0 || target >= count)
        return 0;
    memcpy(g_roulette_offers, offers, sizeof offers[0] * (size_t)count);
    g_roulette_count = count;
    g_roulette_target = target;
    g_roulette_then = then;
    g_roulette_step = 0;
    g_roulette_wait = g_roulette_gaps[0];
    /* Starting this far back, the last step lands on the pick. */
    g_cursor = ((target - ROULETTE_STEPS) % count + count) % count;
    g_alt_armed = 0;
    g_state = ST_ROULETTE;
    g_dirty = 1;
    return 1;
}

static void finish_roulette(void)
{
    g_dirty = 1;
    if (g_roulette_then == ROULETTE_THEN_TIME_OUT) {
        g_state = ST_REWARD;
        time_out_reward_at(g_roulette_target);
        return;
    }
    if (g_sweep_pending) {
        start_pending_sweep();
        return;
    }
    enter_phase();
    if (g_state == ST_DECK)
        start_deck_timer();
}

static void tick_roulette(void)
{
    if (g_state != ST_ROULETTE || --g_roulette_wait > 0)
        return;
    if (g_roulette_step == ROULETTE_STEPS) {
        finish_roulette();
        return;
    }
    g_cursor = (g_cursor + 1) % g_roulette_count;
    g_roulette_step++;
    g_dirty = 1;
    if (g_roulette_step == ROULETTE_STEPS) {
        SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CONFIRM);
        g_roulette_wait = ROULETTE_HOLD;
    } else {
        SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_MOVE);
        g_roulette_wait = g_roulette_gaps[g_roulette_step];
    }
}

static void on_roulette_event(screen_event event)
{
    if (event != EV_CONFIRM)
        return;
    g_cursor = g_roulette_target;
    SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CONFIRM);
    finish_roulette();
}

/* ---------- the sweep ----------
 * Cards that left the deck at random are shown the same way: the deck as it was, the cursor running
 * over the cards that could leave and slowing onto each that did, marking it. About 3 seconds
 * whatever the number of cards. The change is already made and saved; this only shows it. */

typedef enum { SWEEP_THEN_ENTER_PHASE, SWEEP_THEN_SHOW_DECK } sweep_then;

#define SWEEP_FRAMES 150        /* moving, shared between the cards that leave */
#define SWEEP_LANDING_HOLD 18   /* the pause on each card but the last */
#define SWEEP_FINAL_HOLD 36

int g_sweep_candidate[MAX_DECK_ART];
static sweep_then g_sweep_then = SWEEP_THEN_ENTER_PHASE;
static sts_deck g_sweep_before, g_sweep_after;
static int g_sweep_list[MAX_DECK_ART];      /* the cards that could leave, as grid positions in order */
static int g_sweep_list_count = 0;
static int g_sweep_targets[MAX_DECK_ART];   /* places in g_sweep_list of the cards that left, in order */
static int g_sweep_target_count = 0;
static int g_sweep_next = 0;                /* the next target */
static int g_sweep_place = 0;               /* where the cursor is in g_sweep_list */
static int g_sweep_steps = 0, g_sweep_step = 0, g_sweep_wait = 0, g_sweep_weights = 0;

/* Steps get longer towards the landing: their weights are d*d + 4*i*i, shared out over the frames. */
static int sweep_gap(int step)
{
    int gap = (SWEEP_FRAMES / g_sweep_target_count) * (g_sweep_steps * g_sweep_steps + 4 * step * step) / g_sweep_weights;

    return gap < 2 ? 2 : gap;
}

static void begin_sweep_segment(void)
{
    int minimum = g_sweep_next == 0 ? 10 : 4, wau;

    g_sweep_steps = (g_sweep_targets[g_sweep_next] - g_sweep_place + g_sweep_list_count) % g_sweep_list_count;
    while (g_sweep_steps < minimum)
        g_sweep_steps += g_sweep_list_count;
    g_sweep_weights = 0;
    for (wau = 0; wau < g_sweep_steps; wau++)
        g_sweep_weights += g_sweep_steps * g_sweep_steps + 4 * wau * wau;
    g_sweep_step = 0;
    g_sweep_wait = sweep_gap(0);
}

/* After a reward that took cards out at random (from `before`, the deck now being the new one): sets
 * up the sweep for start_pending_sweep. Returns 0 if there is nothing to show, or the setting is off. */
static int prepare_sweep(const sts_deck *before, sweep_then then)
{
    int used[MAX_DECK_ART] = {0};
    int change, copy, wau, by_type = g_settings.reward.replace == STS_REPLACE_BY_TYPE, leaving_type = -1;
    uint16_t incoming = g_changes_added_count > 0 ? g_changes_added[0].id : 0xFFFF;

    g_sweep_pending = 0;
    if (!g_settings.reward_roulette || g_changes_removed_count == 0 || before->count > MAX_DECK_ART)
        return 0;
    if (by_type) {
        const sts_card *card = sts_card_db_find(&g_db, g_run.character, g_changes_removed[0].id);
        leaving_type = card ? (int)card->type : -1;
    }
    g_sweep_list_count = 0;
    for (wau = 0; wau < before->count; wau++) {
        const sts_card *card = sts_card_db_find(&g_db, g_run.character, before->ids[wau]);

        g_sweep_candidate[wau] = before->ids[wau] != incoming && (!by_type || (card && (int)card->type == leaving_type));
        if (g_sweep_candidate[wau])
            g_sweep_list[g_sweep_list_count++] = wau;
    }
    /* Each copy that left, at a place of its own, in deck order. */
    g_sweep_target_count = 0;
    for (wau = 0; wau < g_sweep_list_count; wau++) {
        uint16_t id = before->ids[g_sweep_list[wau]];

        for (change = 0; change < g_changes_removed_count; change++) {
            int taken = 0;

            if (g_changes_removed[change].id != id)
                continue;
            for (copy = 0; copy < wau; copy++)
                taken += used[copy] && before->ids[g_sweep_list[copy]] == id;
            if (taken < g_changes_removed[change].copies) {
                used[wau] = 1;
                g_sweep_targets[g_sweep_target_count++] = wau;
            }
        }
    }
    if (g_sweep_list_count < 2 || g_sweep_target_count == 0)
        return 0;
    g_sweep_before = *before;
    g_sweep_after = g_run.deck;
    g_sweep_then = then;
    g_sweep_pending = 1;
    return 1;
}

static void start_pending_sweep(void)
{
    g_sweep_pending = 0;
    g_run.deck = g_sweep_before;
    g_deck_art_dirty = 1;
    memset(g_selected, 0, sizeof g_selected);
    g_sweep_next = 0;
    g_sweep_place = (g_sweep_targets[0] - 10 + 10 * g_sweep_list_count) % g_sweep_list_count;
    begin_sweep_segment();
    g_cursor = g_sweep_list[g_sweep_place];
    g_alt_armed = 0;
    g_state = ST_SWEEP;
    g_dirty = 1;
}

static void finish_sweep(void)
{
    g_run.deck = g_sweep_after;
    g_deck_art_dirty = 1;
    memset(g_selected, 0, sizeof g_selected);
    g_selected_count = 0;
    if (g_sweep_then == SWEEP_THEN_SHOW_DECK)
        show_deck();
    else
        enter_phase();
    if (g_state == ST_DECK)
        start_deck_timer();
}

static void tick_sweep(void)
{
    if (g_state != ST_SWEEP || --g_sweep_wait > 0)
        return;
    if (g_sweep_step == g_sweep_steps) {
        /* The pause after a landing is over. */
        if (g_sweep_next < g_sweep_target_count)
            begin_sweep_segment();
        else
            finish_sweep();
        return;
    }
    g_sweep_place = (g_sweep_place + 1) % g_sweep_list_count;
    g_cursor = g_sweep_list[g_sweep_place];
    g_sweep_step++;
    g_dirty = 1;
    if (g_sweep_step < g_sweep_steps) {
        SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_MOVE);
        g_sweep_wait = sweep_gap(g_sweep_step);
        return;
    }
    g_selected[g_cursor] = 1;
    g_sweep_next++;
    SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CONFIRM);
    g_sweep_wait = g_sweep_next < g_sweep_target_count ? SWEEP_LANDING_HOLD : SWEEP_FINAL_HOLD;
}

static void on_sweep_event(screen_event event)
{
    if (event != EV_CONFIRM)
        return;
    SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CONFIRM);
    finish_sweep();
}

/* Offers just picked at random by the run (offer_pick random) are shown by the roulette, once. */
static int roulette_for_rolled_pick(void)
{
    int shown = g_run.rolled_count > 1
        && start_roulette(g_run.rolled, g_run.rolled_count, g_run.rolled_pick, ROULETTE_THEN_SHOW);

    g_run.rolled_count = 0;
    return shown;
}

/* The run just ended: Deck 1 goes back to the player's own deck. */
static void on_run_finished(void)
{
    slog("the run for character %d is finished after %d matches", g_run.character, g_run.matches_played);
    if (g_active && g_active_character == g_run.character)
        deactivate_run_deck();
}

int score_entry_count(void)
{
    return g_run.history_count + (g_run.history_forgotten == 0);
}

int score_entry_match(int entry)
{
    return g_run.history_forgotten == 0 ? entry - 1 : entry;
}

/* The mod's screen was already open when a match ended. If it is an offer from an earlier match (a
 * disconnect left it under the game's network menu and the player reconnected from there), this
 * match's reward waits behind it, and comes up once it is taken. Anything else on screen loses it. */
static void owe_reward_behind_open_screen(int new_match)
{
    sts_reward_outcome outcome = STS_REWARD_NONE;
    sts_run_status status;

    if (!new_match)
        return;
    if (!on_reward_screen() || g_run.character != g_active_character) {
        slog("reward lost: the mod's screen was open on something other than an offer");
        return;
    }
    status = sts_run_begin_reward(&g_run, &g_db, &g_settings, &g_match_result, &outcome);
    slog("reward: won %d, owed behind the offer on screen, status %s, owed %d",
         g_match_result.won, sts_run_status_str(status), g_run.owed_count);
    if (status == STS_RUN_OK)
        save_run();
    g_dirty = 1;
}

/* Loads the run in use into g_run, which may hold another character if the screen was used since
 * this deck became active. Returns 0 if it cannot be loaded. */
static int load_active_run(void)
{
    char path[MAX_PATH + 32];
    sts_run_status status;

    run_path(path, sizeof path, g_active_character);
    status = sts_run_load(path, &g_run);
    if (status == STS_RUN_OK)
        status = sts_run_validate(&g_run, &g_db, &g_settings);
    if (status != STS_RUN_OK) {
        slog("reward skipped, the run could not be loaded: %s", sts_run_status_str(status));
        return 0;
    }
    load_card_text(g_active_character);
    return 1;
}

/* Begins the reward for the match just played, and saves it. An offer still waiting from before is
 * shown again, and this match's reward is owed. Returns 1 if there is something to show. */
static int begin_match_reward(void)
{
    sts_reward_outcome outcome = STS_REWARD_NONE;
    sts_deck before = g_run.deck;
    sts_run_status status = sts_run_begin_reward(&g_run, &g_db, &g_settings, &g_match_result, &outcome);

    g_sweep_pending = 0;
    if (outcome == STS_REWARD_AUTO) {
        describe_deck_change(&before);
        prepare_sweep(&before, SWEEP_THEN_ENTER_PHASE);
    }
    slog("reward: won %d, status %s, outcome %d, owed %d", g_match_result.won, sts_run_status_str(status),
         (int)outcome, g_run.owed_count);
    if (status != STS_RUN_OK)
        return 0;
    save_run();
    if (outcome == STS_REWARD_FINISHED)
        on_run_finished();
    if (outcome == STS_REWARD_NONE) {
        g_match_no_offer = 1;
        return 0;
    }
    return 1;
}

/* Opens the screen on the reward: a random pick or removal shown first, else the run's phase. */
static void show_reward(int new_match)
{
    g_error[0] = 0;
    g_status[0] = 0;
    g_close_after_deck = 1;
    SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CONFIRM);
    if (new_match && roulette_for_rolled_pick())
        return;
    if (g_sweep_pending) {
        start_pending_sweep();
        return;
    }
    enter_phase();
    if (g_state == ST_DECK)
        start_deck_timer();
}

/* Runs on the first title or character select frame after a finished match, and when the game
 * starts with an offer still waiting. */
void maybe_start_reward(void)
{
    int new_match = g_match_new;

    if (!g_match_reward_due)
        return;
    g_match_reward_due = 0;
    g_match_new = 0;
    if (!g_active)
        return;
    if (g_state != ST_CLOSED) {
        owe_reward_behind_open_screen(new_match);
        return;
    }
    if (!load_active_run())
        return;
    if (new_match) {
        if (!begin_match_reward())
            return;
    } else if (g_run.phase == STS_PHASE_REWARD) {
        slog("showing the offer that was already waiting");
    } else {
        return;
    }
    show_reward(new_match);
}

/* Records what a reward changed, comparing the deck with its old contents. */
static void describe_deck_change(const sts_deck *before)
{
    const sts_deck *decks[2];
    int which, wau;

    clear_deck_change();
    decks[0] = before;
    decks[1] = &g_run.deck;
    for (which = 0; which < 2; which++) {
        for (wau = 0; wau < decks[which]->count; wau++) {
            uint16_t id = decks[which]->ids[wau];
            int difference = sts_deck_count_of(&g_run.deck, id) - sts_deck_count_of(before, id);
            deck_change *list = which == 0 ? g_changes_removed : g_changes_added;
            int *count = which == 0 ? &g_changes_removed_count : &g_changes_added_count;

            /* Each distinct card once: at its first position in the deck being scanned. */
            if (wau > 0 && decks[which]->ids[wau - 1] == id)
                continue;
            if (which == 0)
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
static void after_reward_taken(const sts_deck *before, int random_removal);

static void commit_reward(const sts_replace_choice *choice)
{
    sts_deck before = g_run.deck;
    sts_run_status status = sts_run_reward_pick(&g_run, &g_db, &g_settings, g_pending_offer, choice);

    if (status != STS_RUN_OK) {
        set_error(tr("Reward"), sts_run_status_str(status));
        slog("reward pick refused: %s", sts_run_status_str(status));
        return;
    }
    slog("reward taken: card %u", (unsigned)g_run.offers[g_pending_offer].id);
    after_reward_taken(&before, g_settings.reward.mode == STS_REWARD_REPLACE && g_settings.reward.replace != STS_REPLACE_CHOOSE);
}

/* The time limit ran out: the choices made so far are kept and the rest made at random. With no offer
 * chosen yet, the roulette shows which one is taken first. */
static void time_out_reward(void)
{
    if (g_state == ST_REWARD && g_run.offer_count > 1 && g_settings.reward_roulette) {
        int index = sts_run_random_offer(&g_run);

        if (!start_roulette(g_run.offers, g_run.offer_count, index, ROULETTE_THEN_TIME_OUT))
            time_out_reward_at(index);
        return;
    }
    time_out_reward_at(-1);
}

/* index: the offer, or -1 for the one being looked at (or one at random, if none is yet). */
static void time_out_reward_at(int index)
{
    static int failed = 0;
    uint16_t ids[MAX_DECK_ART];
    sts_deck before = g_run.deck;
    int wau, count = 0, copies;
    sts_run_status status;

    if (index < 0 && (g_state == ST_REWARD_REMOVE || g_state == ST_REWARD_CONFIRM || g_state == ST_REWARD_TYPE))
        index = g_pending_offer;
    if (g_state == ST_REWARD_REMOVE || g_state == ST_REWARD_CONFIRM)
        for (wau = 0; wau < g_run.deck.count && wau < MAX_DECK_ART; wau++)
            if (g_selected[wau])
                ids[count++] = g_run.deck.ids[wau];
    /* All the cards that leave had been chosen only if as many as the offer's copies were. */
    copies = index >= 0 && index < g_run.offer_count ? g_run.offers[index].copies : STS_MAX_DECK;
    status = sts_run_reward_time_out(&g_run, &g_db, &g_settings, index, ids, count, -1);
    if (status != STS_RUN_OK) {
        /* Not expected, as offers are only made if they can be taken. Once, so it is not every frame. */
        if (!failed)
            slog("time ran out, but the reward could not be taken at random: %s", sts_run_status_str(status));
        failed = 1;
        return;
    }
    failed = 0;
    slog("time ran out: reward taken at random, %d cards had been chosen", count);
    after_reward_taken(&before, g_settings.reward.mode == STS_REWARD_REPLACE
                                && (g_settings.reward.replace != STS_REPLACE_CHOOSE || count < copies));
}

/* The same after any reward: the deck screen shows what changed, then the score or more offers. */
static void after_reward_taken(const sts_deck *before, int random_removal)
{
    sweep_then then = g_run.phase == STS_PHASE_READY ? SWEEP_THEN_ENTER_PHASE : SWEEP_THEN_SHOW_DECK;

    SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CONFIRM);
    g_selected_count = 0;
    describe_deck_change(before);
    save_run();
    if (g_run.phase == STS_PHASE_FINISHED)
        on_run_finished();   /* the match owed behind this offer was the last: this change, then the score */
    if (g_run.phase == STS_PHASE_REWARD)
        slog("owed reward: new offers, %d more owed", g_run.owed_count);
    /* Cards that left at random are shown leaving first. */
    if (random_removal && prepare_sweep(before, then)) {
        start_pending_sweep();
        return;
    }
    g_sweep_pending = 0;
    if (then == SWEEP_THEN_SHOW_DECK)
        show_deck();
    else
        enter_phase();
    start_deck_timer();
}

int reward_seconds_left(void)
{
    int left;

    if (g_settings.time_limit <= 0 || g_run.phase != STS_PHASE_REWARD || !on_reward_screen())
        return -1;
    left = g_settings.time_limit * 60 - g_run.offer_frames;
    return left > 0 ? (left + 59) / 60 : 0;
}

int deck_seconds_left(void)
{
    return g_state == ST_DECK && g_deck_frames_left > 0 ? (g_deck_frames_left + 59) / 60 : -1;
}

static void on_deck_event(screen_event event);

/* Once a frame while the mod's screen is open: the reward's clock, kept in the run so a restart
 * goes on from where it was, and the deck screen's. */
static void tick_timers(void)
{
    tick_roulette();
    tick_sweep();
    if (reward_seconds_left() >= 0) {
        g_run.offer_frames++;
        if (g_run.offer_frames % 60 == 0) {
            save_run();
            g_dirty = 1;
        }
        if (g_run.offer_frames >= g_settings.time_limit * 60)
            time_out_reward();
    }
    if (g_state != ST_DECK) {
        g_deck_frames_left = 0;
    } else if (g_deck_frames_left > 0) {
        g_deck_frames_left--;
        if (g_deck_frames_left % 60 == 0)
            g_dirty = 1;
        if (g_deck_frames_left == 0)
            on_deck_event(EV_CONFIRM);
    }
}

static void commit_selected_removals(void)
{
    uint16_t ids[MAX_DECK_ART];
    sts_replace_choice choice;
    int wau, count = 0;

    for (wau = 0; wau < g_run.deck.count && wau < MAX_DECK_ART; wau++)
        if (g_selected[wau])
            ids[count++] = g_run.deck.ids[wau];
    choice.remove_ids = ids;
    choice.remove_count = count;
    choice.type = 0;
    commit_reward(&choice);
}

/* Fills the change list with what the chosen swap would do, for the confirm screen. */
static void preview_selected_swap(void)
{
    const sts_offer *offer = &g_run.offers[g_pending_offer];
    int wau;

    clear_deck_change();
    for (wau = 0; wau < g_run.deck.count && wau < MAX_DECK_ART; wau++) {
        uint16_t id;
        int count;

        if (!g_selected[wau])
            continue;
        id = g_run.deck.ids[wau];
        /* The deck is sorted, so copies of a card are together. */
        count = g_changes_removed_count - 1;
        if (count >= 0 && g_changes_removed[count].id == id) {
            g_changes_removed[count].copies++;
        } else if (g_changes_removed_count < MAX_CHANGE_LINES) {
            g_changes_removed[g_changes_removed_count].id = id;
            g_changes_removed[g_changes_removed_count].copies = 1;
            g_changes_removed_count++;
        }
    }
    g_changes_added[0].id = offer->id;
    g_changes_added[0].copies = offer->copies;
    g_changes_added_count = 1;
}

/* Left and right wrap around within a row, as on the character menu; a shorter last row wraps at
 * its own end. Up and down keep the column, wrapping between the top and bottom rows, and land on
 * the last card of a shorter last row when the column is past its end. The column is remembered
 * through that row, as the character menu does, so going down and back up returns to the same card. */
static int g_grid_column = 0;
static int g_grid_last_cursor = -1, g_grid_last_count = -1;   /* where the cursor was left */

void move_card_grid(screen_event event, int count)
{
    int rows, row, row_start, row_length, column;

    if (count <= 0)
        return;
    rows = (count + DECK_COLS - 1) / DECK_COLS;
    row = g_cursor / DECK_COLS;
    row_start = row * DECK_COLS;
    row_length = count - row_start < DECK_COLS ? count - row_start : DECK_COLS;
    column = g_cursor - row_start;
    /* Anything else that moved the cursor (a new screen, a new deck) sets the column afresh. */
    if (g_cursor != g_grid_last_cursor || count != g_grid_last_count)
        g_grid_column = column;
    if (event == EV_RIGHT) {
        g_cursor = row_start + (column + 1) % row_length;
        g_grid_column = g_cursor - row_start;
    } else if (event == EV_LEFT) {
        g_cursor = row_start + (column + row_length - 1) % row_length;
        g_grid_column = g_cursor - row_start;
    } else {
        row = (row + (event == EV_DOWN ? 1 : rows - 1)) % rows;
        g_cursor = row * DECK_COLS + g_grid_column;
        if (g_cursor >= count)
            g_cursor = count - 1;
    }
    g_grid_last_cursor = g_cursor;
    g_grid_last_count = count;
    SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_MOVE);
    g_dirty = 1;
}

/* The card grid shared by the deck and removal screens. */
static void move_grid_cursor(screen_event event)
{
    move_card_grid(event, g_run.deck.count < MAX_DECK_ART ? g_run.deck.count : MAX_DECK_ART);
}

static void next_available_type(int direction)
{
    const sts_offer *offer = &g_run.offers[g_pending_offer];
    unsigned mask = sts_replace_types_available(&g_db, g_run.character, &g_run.deck, offer->id, offer->copies);
    int wau;

    for (wau = 0; wau < 3; wau++) {
        g_cursor = (g_cursor + direction + 3) % 3;
        if (mask & STS_TYPE_BIT(g_cursor))
            break;
    }
}

/* A run this character has is about to go, by starting over or deleting it: one more started over
 * under the rules it was made under. */
static void count_discarded_run(int character)
{
    char path[MAX_PATH + 32];
    static sts_run old;

    run_path(path, sizeof path, character);
    if (!run_file_exists(character))
        return;
    count_restart(character, sts_run_load(path, &old) == STS_RUN_OK ? old.settings_hash : sts_settings_hash(&g_settings));
}

static void start_new_run(int character)
{
    LARGE_INTEGER ticks;
    sts_run_status status;
    static sts_run fresh;

    /* Offers are only seen by this player, so a local seed is fine, unless the rules set one. */
    QueryPerformanceCounter(&ticks);
    status = sts_run_start(&fresh, &g_db, &g_settings, character, sts_run_seed(&g_settings, (uint64_t)ticks.QuadPart));
    if (status != STS_RUN_OK) {
        set_error(tr("Could not start a run"), sts_run_status_str(status));
        return;
    }
    count_discarded_run(character);
    g_run = fresh;
    /* The starting deck's cards are found for the gallery, as they are never offered. */
    note_found_deck(character, &g_run.deck);
    g_run.restarts = restart_count(character, g_run.settings_hash);
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

int rules_option_count(void)
{
    return g_rules_option_count;
}

rules_option rules_option_at(int index)
{
    return g_rules_options[index];
}

/* Works out whether g_run, made under other rules, can carry on under the current ones. */
static void enter_rules_screen(void)
{
    static sts_run trial;
    sts_run_status status;

    int can_continue, can_restore;

    g_state = ST_RULES;
    g_cursor = 0;
    g_rules_start_over_armed = 0;
    g_rules_problem[0] = 0;
    g_dirty = 1;

    trial = g_run;
    status = sts_run_adopt_rules(&trial, &g_db, &g_settings);
    can_continue = g_settings.allow_rule_changes && status == STS_RUN_OK;
    can_restore = sts_run_rules_restorable(&g_run);
    g_rules_option_count = 0;
    if (can_continue)
        g_rules_options[g_rules_option_count++] = RULES_CARRY_ON;
    if (can_restore)
        g_rules_options[g_rules_option_count++] = RULES_RESTORE;
    g_rules_options[g_rules_option_count++] = RULES_BACK;
    g_rules_options[g_rules_option_count++] = RULES_START_OVER;
    if (!g_settings.allow_rule_changes) {
        sprintf(g_rules_problem, tr("The current settings do not allow a run to carry on under new rules."));
    } else if (status != STS_RUN_OK) {
        sts_deck_status deck = sts_deck_validate(&g_run.deck, &g_settings.rules, NULL);

        _snprintf(g_rules_problem, sizeof g_rules_problem - 1, tr("It cannot carry on under them: %s."),
                  deck != STS_DECK_OK ? sts_deck_status_str(deck) : sts_run_status_str(status));
        g_rules_problem[sizeof g_rules_problem - 1] = 0;
    }
}

static void open_character(int character)
{
    char path[MAX_PATH + 32];
    sts_run_status status;

    g_character = character;
    g_error[0] = 0;
    g_status[0] = 0;
    load_card_text(character);
    run_path(path, sizeof path, character);
    status = sts_run_load(path, &g_run);
    if (status == STS_RUN_IO) {
        start_new_run(character);
        return;
    }
    /* A finished run only shows its score, whatever the rules are now. */
    if (status == STS_RUN_OK && g_run.phase == STS_PHASE_FINISHED) {
        enter_phase();
        return;
    }
    if (status == STS_RUN_OK)
        status = sts_run_validate(&g_run, &g_db, &g_settings);
    if (status == STS_RUN_SETTINGS_CHANGED) {
        enter_rules_screen();
        return;
    }
    if (status != STS_RUN_OK) {
        set_error(character_label(character), sts_run_status_str(status));
        return;
    }
    enter_phase();
}

static void stop_viewing_past_deck(void);

void go_to_chooser_at(int cursor)
{
    stop_viewing_past_deck();
    if (g_data_ok)
        scan_saves();
    g_state = ST_CHOOSE;
    g_cursor = cursor;
    g_alt_armed = 0;
    g_dirty = 1;
}

static void go_to_chooser(void)
{
    go_to_chooser_at(g_character);
}

/* Pressing the third button twice gives up on a run, so one slip cannot lose it. Both presses make
 * a sound, as on the rule change screen, so the first one is noticed. */
static int alt_twice(void)
{
    SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CONFIRM);
    if (!g_alt_armed) {
        g_alt_armed = 1;
        g_dirty = 1;
        return 0;
    }
    g_alt_armed = 0;
    return 1;
}

/* The draft and the reward screen both move along a row of offers. */
static void move_offer_cursor(screen_event event)
{
    if (g_run.offer_count > 0) {
        int step = event == EV_RIGHT ? 1 : g_run.offer_count - 1;
        g_cursor = (g_cursor + step) % g_run.offer_count;
        SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_MOVE);
        g_dirty = 1;
    }
}

/* What the toggle button does on the character screen for this character: stop using its run,
 * start using it, or nothing (no finished deck, rules changed, or runs are off). */
choose_toggle choose_toggle_for(int character)
{
    if (character < 0 || character >= STS_NUM_CHARACTERS || !g_data_ok || blocking_mod())
        return TOGGLE_NONE;
    if (g_active && g_active_character == character)
        return TOGGLE_STOP;
    if (g_has_save[character] && !g_saved_rules_changed[character] && g_saved_phase[character] != STS_PHASE_DRAFT
        && g_saved_phase[character] != STS_PHASE_FINISHED && g_rules_changed_character != character)
        return TOGGLE_USE;
    return TOGGLE_NONE;
}

/* The same as the deck screen's toggle, without opening the run. Starting one stops any other. */
static void toggle_from_chooser(int character)
{
    char path[MAX_PATH + 32];
    sts_run_status status;
    choose_toggle toggle = choose_toggle_for(character);

    if (toggle == TOGGLE_NONE)
        return;
    g_error[0] = 0;
    g_dirty = 1;
    if (toggle == TOGGLE_STOP) {
        deactivate_run_deck();
        SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CANCEL);
        return;
    }
    run_path(path, sizeof path, character);
    status = sts_run_load(path, &g_run);
    if (status == STS_RUN_OK)
        status = sts_run_validate(&g_run, &g_db, &g_settings);
    if (status != STS_RUN_OK) {
        set_error(character_label(character), sts_run_status_str(status));
        return;
    }
    g_character = character;
    if (activate_run_deck())
        SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CONFIRM);
}

/* The column the cursor had in the character grid, for coming back up from the last row. */
static int g_choose_column = 0;

static int on_last_choose_row(void)
{
    return g_cursor == CHOOSE_SETTINGS || g_cursor == CHOOSE_GALLERY;
}

/* Up and down go through the character rows and the row below them, wrapping round. That row has
 * Settings under the first column and the gallery under the last: the one nearer the column is
 * chosen, Settings from the middle one. */
static void move_choose_vertically(int direction)
{
    int rows = STS_NUM_CHARACTERS / COLS + 1;
    int row = on_last_choose_row() ? rows - 1 : g_cursor / COLS;

    if (!on_last_choose_row())
        g_choose_column = g_cursor % COLS;
    row = (row + direction + rows) % rows;
    if (row < rows - 1)
        g_cursor = row * COLS + g_choose_column;
    else
        g_cursor = g_choose_column > COLS / 2 ? CHOOSE_GALLERY : CHOOSE_SETTINGS;
}

/* Deletes a character's run from the character menu. Opening the character afterwards starts a new
 * draft. A run in use stops first, which puts the player's own deck back. */
static void delete_run(int character)
{
    char path[MAX_PATH + 32];

    if (g_active && g_active_character == character)
        deactivate_run_deck();
    if (g_rules_changed_character == character) {
        g_rules_changed_character = -1;
        save_active_file();
    }
    count_discarded_run(character);
    run_path(path, sizeof path, character);
    if (remove(path) != 0) {
        set_error(character_label(character), tr("the run could not be deleted"));
        return;
    }
    slog("deleted the run for character %d", character);
    g_error[0] = 0;
    g_character = character;
    scan_saves();
}

static void on_choose_event(screen_event event)
{
    /* The last row has Settings and the gallery: left and right go between them, and the buttons
     * for runs do nothing there. Going back up then lands on the column above the entry. */
    if (on_last_choose_row() && (event == EV_ALT || event == EV_TOGGLE))
        return;
    if (on_last_choose_row() && (event == EV_LEFT || event == EV_RIGHT)) {
        g_cursor = g_cursor == CHOOSE_SETTINGS ? CHOOSE_GALLERY : CHOOSE_SETTINGS;
        g_choose_column = g_cursor == CHOOSE_SETTINGS ? 0 : COLS - 1;
        SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_MOVE);
        g_error[0] = 0;
        g_dirty = 1;
        return;
    }
    switch (event) {
    /* Left and right wrap around within a row, as up and down do within a column. */
    case EV_LEFT:  g_cursor = g_cursor - g_cursor % COLS + (g_cursor % COLS + COLS - 1) % COLS; break;
    case EV_RIGHT: g_cursor = g_cursor - g_cursor % COLS + (g_cursor % COLS + 1) % COLS; break;
    case EV_UP:    move_choose_vertically(-1); break;
    case EV_DOWN:  move_choose_vertically(1); break;
    case EV_CONFIRM:
        if (g_cursor == CHOOSE_SETTINGS) {
            open_settings();
            return;
        }
        if (g_cursor == CHOOSE_GALLERY) {
            open_gallery();
            return;
        }
        if (g_data_ok && !blocking_mod()) {
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
        /* Nothing to delete on a character with no run. */
        if (g_data_ok && !blocking_mod() && run_file_exists(g_cursor) && alt_twice())
            delete_run(g_cursor);
        g_dirty = 1;
        return;
    case EV_TOGGLE:
        toggle_from_chooser(g_cursor);
        return;
    }
    SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_MOVE);
    g_error[0] = 0;
    g_dirty = 1;
}

static void take_draft_offer(void)
{
    sts_run_status status = sts_run_draft_pick(&g_run, &g_db, &g_settings, g_cursor);

    if (status != STS_RUN_OK) {
        set_error(tr("Draft"), sts_run_status_str(status));
        return;
    }
    SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CONFIRM);
    save_run();
    /* A finished draft is meant to be played. */
    if (g_run.phase == STS_PHASE_READY)
        activate_run_deck();
    enter_phase();
}

static void on_draft_event(screen_event event)
{
    switch (event) {
    case EV_LEFT:
    case EV_RIGHT:
        move_offer_cursor(event);
        break;
    case EV_CONFIRM:
        take_draft_offer();
        break;
    case EV_CANCEL:
        SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CANCEL);
        go_to_chooser();
        break;
    default:
        break;
    }
}

/* Puts the run's own deck back after the deck screen showed an earlier one. Nothing saves the run
 * while it shows one, but going back to the chooser or the score always comes through here. */
static void stop_viewing_past_deck(void)
{
    if (g_viewed_match == SCORE_FINAL_DECK)
        return;
    g_run.deck = g_final_deck;
    g_viewed_match = SCORE_FINAL_DECK;
    g_deck_art_dirty = 1;
}

/* The deck screen, showing the deck as it was after a match (or the draft), with what that match
 * changed listed beside it. */
static void view_deck_at(int match)
{
    sts_deck deck, before;

    if (!sts_run_deck_after(&g_run, match, &deck))
        return;
    g_score_cursor = g_cursor;
    g_final_deck = g_run.deck;
    g_viewed_match = match;
    g_run.deck = deck;
    clear_deck_change();
    /* Both decks are worked out from the run's own deck, which the history fits. */
    if (match >= 0) {
        static sts_run final_run;

        final_run = g_run;
        final_run.deck = g_final_deck;
        if (sts_run_deck_after(&final_run, match - 1, &before))
            describe_deck_change(&before);
    }
    g_state = ST_DECK;
    g_cursor = 0;
    g_alt_armed = 0;
    g_dirty = 1;
    g_deck_art_dirty = 1;
}

/* A finished run's deck can only be looked at, and leads back to the score. */
static void on_finished_deck_event(screen_event event)
{
    switch (event) {
    case EV_LEFT:
    case EV_RIGHT:
    case EV_UP:
    case EV_DOWN:
        move_grid_cursor(event);
        break;
    case EV_CONFIRM:
    case EV_CANCEL:
        clear_deck_change();
        stop_viewing_past_deck();
        SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CANCEL);
        g_state = ST_SCORE;
        g_cursor = g_score_cursor;
        g_dirty = 1;
        break;
    default:
        break;
    }
}

/* Up and down choose a point in the run, and the deck above shows the deck at that point. */
static void on_score_event(screen_event event)
{
    switch (event) {
    case EV_UP:
    case EV_DOWN: {
        int count = score_entry_count();

        /* Wraps round, from the top entry to the bottom one and back. */
        g_cursor = (g_cursor + (event == EV_UP ? count - 1 : 1)) % count;
        if (g_cursor < g_score_top)
            g_score_top = g_cursor;
        if (g_cursor >= g_score_top + SCORE_VISIBLE)
            g_score_top = g_cursor - SCORE_VISIBLE + 1;
        SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_MOVE);
        g_dirty = 1;
        break;
    }
    case EV_CONFIRM:
        SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CONFIRM);
        view_deck_at(score_entry_match(g_cursor));
        break;
    case EV_CANCEL:
        SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CANCEL);
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
            g_close_after_deck = 0;
            start_new_run(g_run.character);
        }
        g_dirty = 1;
        break;
    default:
        break;
    }
}

static void on_deck_event(screen_event event)
{
    if (g_run.phase == STS_PHASE_FINISHED) {
        on_finished_deck_event(event);
        return;
    }
    switch (event) {
    case EV_LEFT:
    case EV_RIGHT:
    case EV_UP:
    case EV_DOWN:
        move_grid_cursor(event);
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
        clear_deck_change();
        if (g_run.phase == STS_PHASE_REWARD) {
            SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CONFIRM);
            if (!roulette_for_rolled_pick())
                enter_phase();
            break;
        }
        SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CANCEL);
        if (g_close_after_deck) {
            g_close_after_deck = 0;
            g_state = ST_CLOSED;
        } else {
            go_to_chooser();
        }
        break;
    case EV_ALT:
        /* Not on the way to an owed reward's offers. */
        if (g_run.phase == STS_PHASE_REWARD)
            break;
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
}

/* Takes the offer under the cursor, first asking what leaves the deck if the rules want that. */
static void take_reward_offer(void)
{
    if (g_cursor >= g_run.offer_count)
        return;
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
        /* One step on from the last type wraps round to the first that is available. */
        g_cursor = 2;
        next_available_type(1);
        g_state = ST_REWARD_TYPE;
        SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CONFIRM);
    } else {
        commit_reward(NULL);
    }
}

/* There is no way out of a reward except taking one: it is saved, so a restart comes
 * back to the same offers. */
static void on_reward_event(screen_event event)
{
    switch (event) {
    case EV_LEFT:
    case EV_RIGHT:
        move_offer_cursor(event);
        break;
    case EV_CONFIRM:
        take_reward_offer();
        break;
    default:
        break;
    }
}

static void on_reward_type_event(screen_event event)
{
    switch (event) {
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
}

/* Marks or unmarks the card under the cursor, and asks for confirmation once enough are marked. */
static void toggle_removal(void)
{
    if (g_cursor >= MAX_DECK_ART)
        return;
    /* The card coming in cannot also be what leaves. */
    if (g_run.deck.ids[g_cursor] == g_run.offers[g_pending_offer].id) {
        SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CANCEL);
        return;
    }
    g_selected[g_cursor] = !g_selected[g_cursor];
    g_selected_count += g_selected[g_cursor] ? 1 : -1;
    g_dirty = 1;
    SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CONFIRM);
    if (g_selected_count >= g_run.offers[g_pending_offer].copies) {
        g_last_selected = g_cursor;
        preview_selected_swap();
        g_state = ST_REWARD_CONFIRM;
    }
}

static void on_reward_remove_event(screen_event event)
{
    switch (event) {
    case EV_LEFT:
    case EV_RIGHT:
    case EV_UP:
    case EV_DOWN:
        move_grid_cursor(event);
        break;
    case EV_CONFIRM:
        toggle_removal();
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
}

static void on_reward_confirm_event(screen_event event)
{
    switch (event) {
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
}

static void carry_on_under_new_rules(void)
{
    sts_run_status status = sts_run_adopt_rules(&g_run, &g_db, &g_settings);

    if (status != STS_RUN_OK) {
        set_error(tr("Could not carry on"), sts_run_status_str(status));
        return;
    }
    slog("run for character %d now follows the current rules", g_run.character);
    SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CONFIRM);
    save_run();
    /* The new rules' match limit may already have been reached. */
    if (g_run.phase == STS_PHASE_FINISHED) {
        if (g_rules_changed_character == g_run.character) {
            g_rules_changed_character = -1;
            save_active_file();
        }
        on_run_finished();
        enter_phase();
        return;
    }
    /* The run that was in use before the rules changed goes back into use. */
    if (g_rules_changed_character == g_run.character && g_run.phase != STS_PHASE_DRAFT)
        activate_run_deck();
    enter_phase();
}

/* First shows what settings.ini would change, and which other runs it would affect. */
static void ask_to_go_back_to_run_rules(void)
{
    g_error[0] = 0;
    describe_rule_changes(&g_run.rules);
    g_rules_restore_others = runs_following_other_rules(&g_run.rules);
    g_rule_table_top = 0;
    g_state = ST_RULES_RESTORE;
    g_dirty = 1;
    SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CONFIRM);
}

/* Puts the rules the run was made under back into settings.ini, so it carries on as it was. */
static void go_back_to_run_rules(void)
{
    sts_run_status status;

    g_dirty = 1;
    if (!restore_rules(&g_run.rules)) {
        SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CANCEL);
        return;
    }
    /* The restored file is loaded and checked like any other, so check the run fits it. */
    status = sts_run_validate(&g_run, &g_db, &g_settings);
    if (status != STS_RUN_OK) {
        set_error(tr("Could not carry on"), sts_run_status_str(status));
        SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CANCEL);
        return;
    }
    slog("run for character %d carries on under its own rules again", g_run.character);
    SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CONFIRM);
    enter_phase();
}

static void start_over_from_rules(void)
{
    /* A second press is needed, so one slip cannot lose the run. */
    if (!g_rules_start_over_armed) {
        g_rules_start_over_armed = 1;
        SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CONFIRM);
        g_dirty = 1;
        return;
    }
    g_rules_start_over_armed = 0;
    g_error[0] = 0;
    g_status[0] = 0;
    SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CONFIRM);
    start_new_run(g_run.character);
}

static void on_rules_event(screen_event event)
{
    switch (event) {
    case EV_LEFT:
    case EV_RIGHT:
    case EV_UP:
    case EV_DOWN: {
        int count = rules_option_count();
        int step = (event == EV_DOWN || event == EV_RIGHT) ? 1 : count - 1;

        g_cursor = (g_cursor + step) % count;
        g_rules_start_over_armed = 0;
        SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_MOVE);
        g_dirty = 1;
        break;
    }
    case EV_CONFIRM:
        switch (rules_option_at(g_cursor)) {
        case RULES_CARRY_ON:
            carry_on_under_new_rules();
            break;
        case RULES_RESTORE:
            ask_to_go_back_to_run_rules();
            break;
        case RULES_OPTION_COUNT:
            break;
        case RULES_BACK:
            SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CANCEL);
            go_to_chooser();
            break;
        case RULES_START_OVER:
            start_over_from_rules();
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
}

static void on_rules_restore_event(screen_event event)
{
    int last_top = rule_table_line_count() - rule_table_rows();

    switch (event) {
    /* Up and down scroll the table, when it is too long for the screen. */
    case EV_UP:
    case EV_DOWN:
        if ((event == EV_UP && g_rule_table_top > 0) || (event == EV_DOWN && g_rule_table_top < last_top)) {
            g_rule_table_top += event == EV_UP ? -1 : 1;
            SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_MOVE);
            g_dirty = 1;
        }
        break;
    case EV_CONFIRM:
        go_back_to_run_rules();
        break;
    case EV_CANCEL:
        SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CANCEL);
        g_error[0] = 0;
        g_state = ST_RULES;
        g_dirty = 1;
        break;
    default:
        break;
    }
}

static void handle_event(screen_event event)
{
    if (event != EV_ALT && g_alt_armed) {
        g_alt_armed = 0;
        g_dirty = 1;
    }
    switch (g_state) {
    case ST_CHOOSE:         on_choose_event(event); break;
    case ST_DRAFT:          on_draft_event(event); break;
    case ST_DECK:           on_deck_event(event); break;
    case ST_REWARD:         on_reward_event(event); break;
    case ST_REWARD_TYPE:    on_reward_type_event(event); break;
    case ST_REWARD_REMOVE:  on_reward_remove_event(event); break;
    case ST_REWARD_CONFIRM: on_reward_confirm_event(event); break;
    case ST_ROULETTE:       on_roulette_event(event); break;
    case ST_SWEEP:          on_sweep_event(event); break;
    case ST_RULES:          on_rules_event(event); break;
    case ST_RULES_RESTORE:  on_rules_restore_event(event); break;
    case ST_SETTINGS:       on_settings_event(event); break;
    case ST_SETTINGS_CONFIRM: on_settings_confirm_event(event); break;
    case ST_SCORE:          on_score_event(event); break;
    case ST_GALLERY_KINDS:
    case ST_GALLERY_CHARACTERS:
    case ST_GALLERY_CARDS:
        on_gallery_event(event);
        break;
    case ST_CLOSED:         break;
    }
}

int screen_is_open(void)
{
    return g_state != ST_CLOSED;
}

void screen_open(void)
{
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

int repeats_while_held(int held)
{
    if (held < 0)
        held = -held;
    return held == 1 || (held >= 30 && held % 6 == 0);
}

/* Esc goes back as the cancel button does, on the frame it goes down and only while the game's
 * window has focus. It is followed even while the seed's text box has it, so the press that closes
 * the box does not also leave the screen. */
static int g_escape_was_down = 0;

static int escape_pressed(void)
{
    DWORD process = 0;
    int down;

    GetWindowThreadProcessId(GetForegroundWindow(), &process);
    down = process == GetCurrentProcessId() && (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    if (down && !g_escape_was_down) {
        g_escape_was_down = 1;
        return 1;
    }
    g_escape_was_down = down;
    return 0;
}

void screen_update(const SokuLib::KeyInput &input)
{
    int escape;

    tick_timers();
    card_sounds_tick();
    escape = escape_pressed();
    if (seed_entry_open()) {
        seed_entry_update();
        return;
    }
    if (input.horizontalAxis < 0 && repeats_while_held(input.horizontalAxis))
        handle_event(EV_LEFT);
    else if (input.horizontalAxis > 0 && repeats_while_held(input.horizontalAxis))
        handle_event(EV_RIGHT);
    if (input.verticalAxis < 0 && repeats_while_held(input.verticalAxis))
        handle_event(EV_UP);
    else if (input.verticalAxis > 0 && repeats_while_held(input.verticalAxis))
        handle_event(EV_DOWN);
    if (input.a == 1)
        handle_event(EV_CONFIRM);
    else if (input.b == 1 || escape)
        handle_event(EV_CANCEL);
    else if (input.c == 1)
        handle_event(EV_ALT);
    else if (input.d == 1)
        handle_event(EV_TOGGLE);
    card_sounds_follow_offers();
}
