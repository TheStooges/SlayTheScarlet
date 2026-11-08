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
static int g_rules_can_continue = 0;
int g_rules_start_over_armed = 0;
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

static void describe_deck_change(const sts_deck *before);
static void clear_deck_change(void);

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
        g_saved_rules_changed[character] = g_has_save[character] && tmp.settings_hash != sts_settings_hash(&g_settings);
    }
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
    g_deck_art_dirty = 1;
    if (g_run.phase == STS_PHASE_DRAFT) {
        g_state = ST_DRAFT;
    } else {
        g_state = g_run.phase == STS_PHASE_REWARD ? ST_REWARD : ST_DECK;
        /* Opening a run only looks at it. A run already in use picks up any new deck. */
        if (run_deck_in_use())
            activate_run_deck();
        set_deck_status();
    }
}

/* Runs on the first title or character select frame after a finished match. */
void maybe_start_reward(void)
{
    char path[MAX_PATH + 32];
    sts_reward_outcome outcome = STS_REWARD_NONE;
    sts_run_status status;

    if (!g_match_reward_due)
        return;
    g_match_reward_due = 0;
    if (!g_active || g_state != ST_CLOSED)
        return;

    /* g_run may hold another character if the screen was used since this deck became active. */
    run_path(path, sizeof path, g_active_character);
    status = sts_run_load(path, &g_run);
    if (status == STS_RUN_OK)
        status = sts_run_validate(&g_run, &g_db, &g_settings);
    if (status != STS_RUN_OK) {
        slog("reward skipped, the run could not be loaded: %s", sts_run_status_str(status));
        return;
    }
    load_card_text(g_active_character);
    /* An offer still waiting from before is shown again rather than replaced. */
    if (g_run.phase != STS_PHASE_REWARD) {
        sts_deck before = g_run.deck;

        status = sts_run_begin_reward(&g_run, &g_db, &g_settings, g_match_won, &outcome);
        if (outcome == STS_REWARD_AUTO)
            describe_deck_change(&before);
        slog("reward: won %d, status %s, outcome %d", g_match_won, sts_run_status_str(status), (int)outcome);
        if (status != STS_RUN_OK)
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
static void commit_reward(const sts_replace_choice *choice)
{
    sts_deck before = g_run.deck;
    sts_run_status status = sts_run_reward_pick(&g_run, &g_db, &g_settings, g_pending_offer, choice);

    if (status != STS_RUN_OK) {
        set_error("Reward", sts_run_status_str(status));
        slog("reward pick refused: %s", sts_run_status_str(status));
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

/* Moves through the card grid shared by the deck and removal screens. Left and right wrap around
 * within a row, as on the character menu; a shorter last row wraps at its own end. */
static void move_grid_cursor(screen_event event)
{
    int count = g_run.deck.count < MAX_DECK_ART ? g_run.deck.count : MAX_DECK_ART;
    int row_start, row_length, column;

    if (count <= 0)
        return;
    row_start = g_cursor - g_cursor % DECK_COLS;
    row_length = count - row_start < DECK_COLS ? count - row_start : DECK_COLS;
    column = g_cursor - row_start;
    if (event == EV_RIGHT)
        g_cursor = row_start + (column + 1) % row_length;
    else if (event == EV_LEFT)
        g_cursor = row_start + (column + row_length - 1) % row_length;
    else if (event == EV_DOWN)
        g_cursor = (g_cursor + DECK_COLS) % count;
    else
        g_cursor = (g_cursor + count - DECK_COLS % count) % count;
    SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_MOVE);
    g_dirty = 1;
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

static void start_new_run(int character)
{
    LARGE_INTEGER ticks;
    sts_run_status status;

    /* Offers are only seen by this player, so a local seed is fine. */
    QueryPerformanceCounter(&ticks);
    status = sts_run_start(&g_run, &g_db, &g_settings, character, (uint64_t)ticks.QuadPart);
    if (status != STS_RUN_OK) {
        set_error("Could not start a run", sts_run_status_str(status));
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

int rules_option_count(void)
{
    return g_rules_can_continue ? 3 : 2;
}

rules_option rules_option_at(int index)
{
    return (rules_option)(g_rules_can_continue ? index : index + 1);
}

/* Works out whether g_run, made under other rules, can carry on under the current ones. */
static void enter_rules_screen(void)
{
    static sts_run trial;
    sts_run_status status;

    g_state = ST_RULES;
    g_cursor = 0;
    g_rules_start_over_armed = 0;
    g_rules_problem[0] = 0;
    g_dirty = 1;

    trial = g_run;
    status = sts_run_adopt_rules(&trial, &g_db, &g_settings);
    g_rules_can_continue = g_settings.allow_rule_changes && status == STS_RUN_OK;
    if (!g_settings.allow_rule_changes) {
        sprintf(g_rules_problem, "The current settings do not allow a run to carry on under new rules.");
    } else if (status != STS_RUN_OK) {
        sts_deck_status deck = sts_deck_validate(&g_run.deck, &g_settings.rules, NULL);

        _snprintf(g_rules_problem, sizeof g_rules_problem - 1, "It cannot carry on under them: %s.",
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
    if (status == STS_RUN_OK)
        status = sts_run_validate(&g_run, &g_db, &g_settings);
    if (status == STS_RUN_SETTINGS_CHANGED) {
        enter_rules_screen();
        return;
    }
    if (status != STS_RUN_OK) {
        set_error(sts_character_name(character), sts_run_status_str(status));
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

static void on_choose_event(screen_event event)
{
    switch (event) {
    /* Left and right wrap around within a row, as up and down do within a column. */
    case EV_LEFT:  g_cursor = g_cursor - g_cursor % COLS + (g_cursor % COLS + COLS - 1) % COLS; break;
    case EV_RIGHT: g_cursor = g_cursor - g_cursor % COLS + (g_cursor % COLS + 1) % COLS; break;
    case EV_UP:    g_cursor = (g_cursor + STS_NUM_CHARACTERS - COLS) % STS_NUM_CHARACTERS; break;
    case EV_DOWN:  g_cursor = (g_cursor + COLS) % STS_NUM_CHARACTERS; break;
    case EV_CONFIRM:
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
        if (g_data_ok && !blocking_mod() && alt_twice()) {
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
}

static void take_draft_offer(void)
{
    sts_run_status status = sts_run_draft_pick(&g_run, &g_db, &g_settings, g_cursor);

    if (status != STS_RUN_OK) {
        set_error("Draft", sts_run_status_str(status));
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

static void on_deck_event(screen_event event)
{
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
        set_error("Could not carry on", sts_run_status_str(status));
        return;
    }
    slog("run for character %d now follows the current rules", g_run.character);
    SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CONFIRM);
    save_run();
    /* The run that was in use before the rules changed goes back into use. */
    if (g_rules_changed_character == g_run.character && g_run.phase != STS_PHASE_DRAFT)
        activate_run_deck();
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
    case ST_RULES:          on_rules_event(event); break;
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
