/* The settings screen: settings.ini as a list to change in game, opened from the character menu.
 * It edits a copy of the file's settings and writes back only the values that changed, so the
 * file's comments and layout survive and it can still be shared as a rules file. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "internal.h"

settings_row g_settings_rows[MAX_SETTINGS_ROWS];
int g_settings_row_count = 0;
int g_settings_top = 0;
int g_settings_leave_armed = 0;
int g_settings_menu = -1;
char g_settings_affected[192];
sts_settings g_settings_edit;

/* The file as it was when the screen opened. Saving checks nobody changed it in the meantime. */
static sts_settings g_settings_file;

/* Rows are built into whichever list is being filled: the open menu's, or a scratch one. */
static settings_row *g_build_rows;
static int g_build_count;

/* Adds a setting with the first line of its help. Settings with a choice of values explain each
 * value on a line of its own, added with add_help_line. */
static settings_row *add_row(const char *section, const char *key, const char *label, const char *help)
{
    settings_row *row;

    if (g_build_count >= MAX_SETTINGS_ROWS)
        return NULL;
    row = &g_build_rows[g_build_count++];
    memset(row, 0, sizeof *row);
    row->section = section;
    row->key = key;
    row->label = label;
    if (help)
        row->help[row->help_count++] = help;
    return row;
}

/* A help line shown whatever the value. */
static void add_help_line(settings_row *row, const char *line)
{
    if (row && row->help_count < SETTINGS_HELP_LINES)
        row->help[row->help_count++] = line;
}

/* A help line about one value, shown only while it is selected. */
static void add_value_help(settings_row *row, const char *value, const char *line)
{
    if (row && row->help_count < SETTINGS_HELP_LINES) {
        row->help_value[row->help_count] = value;
        row->help[row->help_count++] = line;
    }
}

int settings_help_shown(const settings_row *row, int line)
{
    char value[64];

    if (!row->help_value[line])
        return 1;
    sts_settings_get(&g_settings_edit, row->section, row->key, value, sizeof value);
    return strcmp(value, row->help_value[line]) == 0;
}

/* The draft and the reward have the same offer settings, except that only the reward's copies can
 * be set: the draft adds one card per pick. */
static void add_offer_rows(const char *section, const char *copies_help)
{
    settings_row *row;

    add_row(section, "offers", section[0] == 'd' ? tr("Cards per pick") : tr("Cards per offer"),
            section[0] == 'd' ? tr("How many cards each pick offers.") : tr("How many cards each offer shows."));
    if (copies_help) {
        add_row(section, "copies_min", tr("Fewest copies"), copies_help);
        add_row(section, "copies_max", tr("Most copies"), copies_help);
    }
    row = add_row(section, "card_types", tr("Card types"), NULL);
    add_value_help(row, "random", tr("Random: every card is equally likely."));
    add_value_help(row, "weighted", tr("Weighted: each type comes up about as often as the others."));
    add_value_help(row, "mostly_system", tr("Mostly System: system cards come up more often than the others."));
    add_value_help(row, "mostly_skill", tr("Mostly Skill: skill cards come up more often than the others."));
    add_value_help(row, "mostly_spell", tr("Mostly Spell: spell cards come up more often than the others."));
    add_value_help(row, "one_of_each", tr("One of each: every offer shows a system card, a skill card, and a spell card."));
}

const char *settings_menu_name(int menu)
{
    switch (menu) {
    case SETTINGS_MENU_DRAFT:  return tr("Draft");
    case SETTINGS_MENU_REWARD: return tr("Rewards");
    case SETTINGS_MENU_RUN:    return tr("Runs");
    case SETTINGS_MENU_SOUND:  return tr("Sounds");
    }
    return tr("Save changes");
}

const char *settings_menu_help(int menu)
{
    switch (menu) {
    case SETTINGS_MENU_DRAFT:  return tr("Building a run's deck, one card per pick until it is full: the starting deck, and what each pick offers.");
    case SETTINGS_MENU_REWARD: return tr("The card offer after each match, and what it changes in the deck.");
    case SETTINGS_MENU_RUN:    return tr("How long a run lasts, and what happens to it when the rules change.");
    case SETTINGS_MENU_SOUND:  return tr("The sounds cards make on the mod's screens.");
    }
    return tr("Writes the changes from every menu to settings.ini.");
}

/* The settings of one menu, built into rows. Returns how many. Settings with no effect yet (deck
 * size, copy limit, netplay and items) are left out until they do something. */
static int build_menu_rows(int menu, settings_row *rows)
{
    settings_row *row;

    g_build_rows = rows;
    g_build_count = 0;
    switch (menu) {
    case SETTINGS_MENU_DRAFT:
        row = add_row("starter", "enabled", tr("Starting deck"), NULL);
        add_value_help(row, "true", tr("On: 8 set cards for the character to start, then 12 picks fill the rest."));
        add_value_help(row, "false", tr("Off: each draft starts from an empty deck and takes 20 picks."));
        row = add_row("draft", "show_deck", tr("Show the deck so far"), NULL);
        add_value_help(row, "true", tr("On: the deck so far is shown beside the offers."));
        add_value_help(row, "false", tr("Off: a blind draft."));
        add_help_line(row, tr("This is not a rule, so runs carry on when it changes."));
        add_offer_rows("draft", NULL);
        break;
    case SETTINGS_MENU_REWARD:
        row = add_row("reward", "mode", tr("Reward mode"), NULL);
        add_value_help(row, "replace", tr("Replace a card: a card comes in and the same number leave."));
        add_value_help(row, "skip", tr("No offers: the deck never changes after a match."));
        add_help_line(row, tr("Adding and removing change the deck size, which the mod cannot do yet."));
        row = add_row("reward", "who_picks", tr("When you get an offer"), NULL);
        add_value_help(row, "both", tr("After every match: you get an offer, win or lose."));
        add_value_help(row, "winner", tr("Only if you win: no offer after a loss."));
        add_value_help(row, "loser", tr("Only if you lose: no offer after a win."));
        add_value_help(row, "random", tr("Random: a set chance of an offer after each match, win or lose."));
        if (g_settings_edit.who_picks == STS_WHO_RANDOM)
            add_row("reward", "random_chance", tr("Chance of an offer"),
                    tr("Out of 100: how likely an offer is after each match."));
        row = add_row("reward", "offer_pick", tr("What comes into the deck"), NULL);
        add_value_help(row, "choose", tr("Player chooses a card: pick one of the cards offered."));
        add_value_help(row, "random", tr("Random: one of the cards offered is picked for you."));
        add_help_line(row, tr("With \"What leaves the deck\" on Random too, the swap happens by itself."));
        row = add_row("reward", "replacement", tr("What leaves the deck"), NULL);
        add_value_help(row, "choose", tr("Player chooses cards: pick exactly which cards leave."));
        add_value_help(row, "by_type", tr("Player chooses a type: random cards of the chosen type leave."));
        add_value_help(row, "random", tr("Random: the cards that leave are chosen at random."));
        row = add_row("reward", "time_limit", tr("Time limit"),
                      tr("Seconds to take an offer. When it runs out, what is left is chosen at random."));
        add_value_help(row, "0", tr("No limit: take as long as you like."));
        row = add_row("reward", "roulette", tr("Show random picks"), NULL);
        add_value_help(row, "true", tr("On: a pick made at random runs through the offers, or the deck, before it lands."));
        add_value_help(row, "false", tr("Off: a pick made at random is shown straight away."));
        add_help_line(row, tr("This is not a rule, so runs carry on when it changes."));
        add_offer_rows("reward", tr("Each offer gives a random number of copies in this range."));
        break;
    case SETTINGS_MENU_RUN:
        row = add_row("run", "seed", tr("Set seed"), NULL);
        add_value_help(row, "", tr("Off: each draft is random."));
        add_help_line(row, tr("A set seed fixes the draft and rewards."));
        add_help_line(row, tr("Accepts letters, digits, hyphens, and underscores."));
        row = add_row("run", "matches", tr("Match limit"), tr("How many matches a run lasts."));
        add_value_help(row, "0", tr("Endless: the run goes on until you start over."));
        add_help_line(row, tr("The last match gives no offer: the run ends and shows its score."));
        row = add_row("run", "allow_rule_changes", tr("Carry on after rule changes"), NULL);
        add_value_help(row, "true", tr("On: a run made under other rules carries on if its deck is legal under them."));
        add_value_help(row, "false", tr("Off: a run made under other rules can only start over."));
        break;
    case SETTINGS_MENU_SOUND:
        row = add_row("sound", "card_sounds", tr("Card sounds"), NULL);
        add_value_help(row, "true", tr("On: the card under the cursor plays its sounds, on the offers and in the gallery."));
        add_value_help(row, "false", tr("Off: cards make no sounds."));
        add_help_line(row, tr("This is not a rule, so runs carry on when it changes."));
        break;
    }
    return g_build_count;
}

static void build_rows(void)
{
    g_settings_row_count = g_settings_menu >= 0 ? build_menu_rows(g_settings_menu, g_settings_rows) : 0;
}

int settings_row_changed(const settings_row *row);

int settings_menu_changed(int menu)
{
    static settings_row rows[MAX_SETTINGS_ROWS];
    int count = build_menu_rows(menu, rows), wau;

    for (wau = 0; wau < count; wau++)
        if (settings_row_changed(&rows[wau]))
            return 1;
    return 0;
}

/* The words for a choice, as the screen shows them. Card types stay English, as in the game. */
static const char *choice_text(const char *raw)
{
    if (strcmp(raw, "true") == 0) return tr("On");
    if (strcmp(raw, "false") == 0) return tr("Off");
    if (strcmp(raw, "both") == 0) return tr("After every match");
    if (strcmp(raw, "winner") == 0) return tr("Only if you win");
    if (strcmp(raw, "loser") == 0) return tr("Only if you lose");
    if (strcmp(raw, "replace") == 0) return tr("Replace a card");
    if (strcmp(raw, "skip") == 0) return tr("No offers");
    if (strcmp(raw, "add") == 0) return tr("Add a card");
    if (strcmp(raw, "remove") == 0) return tr("Remove a card");
    if (strcmp(raw, "choose") == 0) return tr("Player chooses cards");
    if (strcmp(raw, "by_type") == 0) return tr("Player chooses a type");
    if (strcmp(raw, "random") == 0) return tr("Random");
    if (strcmp(raw, "weighted") == 0) return tr("Weighted");
    if (strcmp(raw, "one_of_each") == 0) return tr("One of each");
    if (strcmp(raw, "mostly_system") == 0) return tr("Mostly System");
    if (strcmp(raw, "mostly_skill") == 0) return tr("Mostly Skill");
    if (strcmp(raw, "mostly_spell") == 0) return tr("Mostly Spell");
    if (strcmp(raw, "custom") == 0) return tr("Custom");
    return NULL;
}

void settings_value_text(const settings_row *row, char *out, size_t size)
{
    char raw[256];
    const char *words;

    out[0] = 0;
    if (!row->key)
        return;
    sts_settings_get(&g_settings_edit, row->section, row->key, raw, sizeof raw);
    /* The other "choose" is about the cards that leave. */
    if (strcmp(row->key, "offer_pick") == 0 && strcmp(raw, "choose") == 0)
        words = tr("Player chooses a card");
    else if (strcmp(row->key, "matches") == 0 && strcmp(raw, "0") == 0)
        words = tr("Endless");
    else if (strcmp(row->key, "time_limit") == 0 && strcmp(raw, "0") == 0)
        words = tr("No limit");
    else if (strcmp(row->key, "seed") == 0)
        words = raw[0] ? raw : tr("Off");
    else
        words = choice_text(raw);
    _snprintf(out, size - 1, "%s", words ? words : raw);
    out[size - 1] = 0;
}

int settings_row_changed(const settings_row *row)
{
    char before[256], now[256];

    if (!row->key)
        return 0;
    sts_settings_get(&g_settings_file, row->section, row->key, before, sizeof before);
    sts_settings_get(&g_settings_edit, row->section, row->key, now, sizeof now);
    return strcmp(before, now) != 0;
}

static int has_unsaved_changes(void)
{
    return !sts_settings_equal(&g_settings_edit, &g_settings_file);
}

static void settings_path(char *out)
{
    sprintf(out, "%ssettings.ini", g_dir);
}

/* Reads settings.ini as it is now. A missing file is the defaults. Returns 0 if it does not parse. */
static int read_settings_file(sts_settings *out)
{
    char path[MAX_PATH + 32];
    int line = 0;
    sts_settings_status status;

    settings_path(path);
    status = sts_settings_load(path, out, &line);
    if (status == STS_SETTINGS_OK || status == STS_SETTINGS_IO)
        return 1;
    {
        char where[64];
        sprintf(where, tr("settings.ini line %d"), line);
        set_error(where, sts_settings_status_str(status));
    }
    return 0;
}

static void scroll_to_cursor(void)
{
    if (g_cursor < g_settings_top)
        g_settings_top = g_cursor;
    if (g_cursor >= g_settings_top + SETTINGS_VISIBLE_ROWS)
        g_settings_top = g_cursor - SETTINGS_VISIBLE_ROWS + 1;
}

void open_settings(void)
{
    g_error[0] = 0;
    g_status[0] = 0;
    if (!read_settings_file(&g_settings_file)) {
        SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CANCEL);
        g_dirty = 1;
        return;
    }
    g_settings_edit = g_settings_file;
    g_settings_menu = -1;
    build_rows();
    g_state = ST_SETTINGS;
    g_cursor = 0;
    g_settings_top = 0;
    g_settings_leave_armed = 0;
    g_dirty = 1;
    SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CONFIRM);
}

/* Rebuilds the list, which depends on the settings, keeping the cursor on the same setting. */
static void rebuild_rows_keeping_cursor(void)
{
    const char *section = g_settings_rows[g_cursor].section;
    const char *key = g_settings_rows[g_cursor].key;
    int wau;

    build_rows();
    for (wau = 0; wau < g_settings_row_count; wau++) {
        if (g_settings_rows[wau].key && strcmp(g_settings_rows[wau].section, section) == 0
            && strcmp(g_settings_rows[wau].key, key) == 0) {
            g_cursor = wau;
            break;
        }
    }
    if (g_cursor >= g_settings_row_count)
        g_cursor = g_settings_row_count - 1;
    scroll_to_cursor();
}

static void move_settings_cursor(int direction)
{
    /* The list of menus ends with Save changes. */
    int count = g_settings_menu < 0 ? SETTINGS_MENU_COUNT + 1 : g_settings_row_count;

    if (count <= 0)
        return;
    g_cursor = (g_cursor + direction + count) % count;
    scroll_to_cursor();
    SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_MOVE);
    g_dirty = 1;
}

/* Without the mod on both sides, a reward cannot change the deck size, so the reward only
 * steps between replacing a card and no offers. */
static int reward_mode_allowed(void)
{
    return g_settings_edit.reward.mode == STS_REWARD_REPLACE || g_settings_edit.reward.mode == STS_REWARD_SKIP;
}

static void change_setting(int direction)
{
    const settings_row *row = &g_settings_rows[g_cursor];
    int wau;

    if (g_settings_menu < 0)
        return;
    if (!row->key || !sts_settings_step(&g_settings_edit, row->section, row->key, direction)) {
        SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CANCEL);
        return;
    }
    if (strcmp(row->section, "reward") == 0 && strcmp(row->key, "mode") == 0)
        for (wau = 0; wau < 4 && !reward_mode_allowed(); wau++)
            sts_settings_step(&g_settings_edit, row->section, row->key, direction);
    /* No more offers than the screen fits. */
    if (g_settings_edit.draft.offer_count > OFFERS_ON_SCREEN)
        g_settings_edit.draft.offer_count = OFFERS_ON_SCREEN;
    if (g_settings_edit.reward.offer_count > OFFERS_ON_SCREEN)
        g_settings_edit.reward.offer_count = OFFERS_ON_SCREEN;
    rebuild_rows_keeping_cursor();
    g_error[0] = 0;
    g_status[0] = 0;
    g_settings_leave_armed = 0;
    SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_MOVE);
    g_dirty = 1;
}

static void reset_setting(void)
{
    const settings_row *row = &g_settings_rows[g_cursor];
    sts_settings defaults;
    char value[256];

    if (g_settings_menu < 0)
        return;
    if (!row->key) {
        SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CANCEL);
        return;
    }
    sts_settings_defaults(&defaults);
    sts_settings_get(&defaults, row->section, row->key, value, sizeof value);
    sts_settings_set(&g_settings_edit, row->section, row->key, value);
    rebuild_rows_keeping_cursor();
    g_error[0] = 0;
    g_status[0] = 0;
    SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CONFIRM);
    g_dirty = 1;
}

const char *offers_off_screen(const sts_settings *settings)
{
    static char message[160];

    if (settings->draft.offer_count <= OFFERS_ON_SCREEN
        && (settings->reward.mode == STS_REWARD_SKIP || settings->reward.offer_count <= OFFERS_ON_SCREEN))
        return NULL;
    _snprintf(message, sizeof message - 1, tr("Cards per pick and cards per offer can be at most %d, as many as the screen fits."),
              OFFERS_ON_SCREEN);
    message[sizeof message - 1] = 0;
    return message;
}

/* Why the settings cannot be saved, in the words the screen uses for each setting. */
static void describe_problem(sts_settings_problem problem, const sts_settings_problem_info *info, char *out,
                             size_t size)
{
    const sts_rules *rules = &g_settings_edit.rules;

    out[0] = 0;
    switch (problem) {
    case STS_PROBLEM_NONE:
        return;
    case STS_PROBLEM_DECK_SIZE:
    case STS_PROBLEM_DECK_TOO_BIG:
    case STS_PROBLEM_COPY_LIMIT:
    case STS_PROBLEM_REWARD_SIZE:
        _snprintf(out, size - 1, "%s", tr("The deck size or copy limit in settings.ini does not work. Change it in the file."));
        break;
    case STS_PROBLEM_DRAFT_OFFERS:
    case STS_PROBLEM_REWARD_OFFERS:
        _snprintf(out, size - 1, "%s", offers_off_screen(&g_settings_edit) ? offers_off_screen(&g_settings_edit)
                                                                           : tr("Cards per pick or per offer must be at least 1."));
        break;
    case STS_PROBLEM_DRAFT_COPIES:
    case STS_PROBLEM_REWARD_COPIES:
        _snprintf(out, size - 1, "%s", tr("Fewest copies cannot be above most copies."));
        break;
    case STS_PROBLEM_DRAFT_COPY_LIMIT:
    case STS_PROBLEM_REWARD_COPY_LIMIT:
        _snprintf(out, size - 1, tr("Most copies cannot be above %d, the copies of one card a deck may hold."),
                  rules->copy_limit);
        break;
    case STS_PROBLEM_RANDOM_CHANCE:
        _snprintf(out, size - 1, "%s", tr("The chance of an offer cannot be above 100."));
        break;
    case STS_PROBLEM_MATCH_LIMIT:
        _snprintf(out, size - 1, tr("A run cannot last more than %d matches."), STS_MATCH_LIMIT_MAX);
        break;
    case STS_PROBLEM_STARTER_TOO_BIG:
        _snprintf(out, size - 1, tr("%s's starting cards are more than a deck can hold."), character_label(info->character));
        break;
    case STS_PROBLEM_STARTER_COPIES:
        _snprintf(out, size - 1, tr("%s's starting cards have too many copies of one card."), character_label(info->character));
        break;
    }
    out[size - 1] = 0;
}

/* After new rules: the run in use stops if they changed its rules, as it does at start-up, and
 * a run stopped that way comes back if they match its rules again. */
static void apply_rules_to_player_run(void)
{
    char path[MAX_PATH + 96];
    static sts_run run;
    sts_run_status status;
    int character = g_active ? g_active_character : g_rules_changed_character;

    if (character < 0 || !g_data_ok)
        return;
    run_path(path, sizeof path, character);
    status = sts_run_load(path, &run);
    if (status == STS_RUN_OK)
        status = sts_run_validate(&run, &g_db, &g_settings);
    if (g_active) {
        if (status == STS_RUN_SETTINGS_CHANGED)
            suspend_run_deck_for_rules();
        return;
    }
    if (status == STS_RUN_OK && run.phase != STS_PHASE_DRAFT && run.phase != STS_PHASE_FINISHED) {
        g_run = run;
        activate_run_deck();
    }
}

/* New rules reach the run each player has in use: it stops if it no longer follows them, and a run
 * stopped for its rules comes back if they are changed back. */
static void apply_rules_to_active_run(void)
{
    int selected = g_player, player;

    for (player = 0; player < RUN_PLAYERS; player++) {
        if (!player_available(player))
            continue;
        select_player(player);
        apply_rules_to_player_run();
    }
    select_player(selected);
}

/* The characters whose saved runs follow the current rules but would not follow the new ones, for both
 * players: player 2's are named as theirs. */
static int list_affected_runs(uint32_t new_hash)
{
    uint32_t old_hash = sts_settings_hash(&g_settings);
    int character, count = 0, selected = g_player, player;
    size_t used = 0;

    g_settings_affected[0] = 0;
    if (new_hash == old_hash)
        return 0;
    for (player = 0; player < RUN_PLAYERS; player++) {
        if (!player_available(player))
            continue;
        select_player(player);
        scan_saves();
        for (character = 0; character < STS_NUM_CHARACTERS; character++) {
            char name[96];

            /* A finished run is not played any more, so its rules do not matter. */
            if (!g_has_save[character] || g_saved_hash[character] != old_hash
                || g_saved_phase[character] == STS_PHASE_FINISHED)
                continue;
            if (player == 1)
                _snprintf(name, sizeof name - 1, tr("%s (player 2)"), character_label(character));
            else
                _snprintf(name, sizeof name - 1, "%s", character_label(character));
            name[sizeof name - 1] = 0;
            if (used + strlen(name) + 3 < sizeof g_settings_affected)
                used += (size_t)sprintf(g_settings_affected + used, "%s%s", count ? ", " : "", name);
            count++;
        }
    }
    select_player(selected);
    scan_saves();
    return count;
}

static int write_settings_file(void)
{
    char path[MAX_PATH + 32], temporary[MAX_PATH + 40];
    size_t length = 0, rewritten_length = 0;
    char *text, *rewritten;
    sts_settings on_disk;
    FILE *file;
    int ok;

    settings_path(path);
    /* Someone may have edited the file since the screen opened; saving would undo their edit. */
    if (!read_settings_file(&on_disk))
        return 0;
    if (!sts_settings_equal(&on_disk, &g_settings_file)) {
        set_error(tr("Cannot save"), tr("settings.ini was changed outside the game. Leave and open Settings again."));
        return 0;
    }
    text = sts_read_file(path, &length);
    rewritten = sts_settings_rewrite(text, text ? length : 0, &g_settings_edit, &rewritten_length);
    free(text);
    if (!rewritten) {
        set_error(tr("Cannot save"), tr("settings.ini could not be read"));
        return 0;
    }
    /* A new file replaces the old one in one step, so a failed write cannot leave half a file. */
    sprintf(temporary, "%s.new", path);
    file = fopen(temporary, "wb");
    ok = file && fwrite(rewritten, 1, rewritten_length, file) == rewritten_length;
    if (file)
        ok = fclose(file) == 0 && ok;
    free(rewritten);
    if (ok)
        ok = MoveFileExA(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) {
        remove(temporary);
        set_error(tr("Cannot save"), tr("settings.ini could not be written"));
        return 0;
    }
    return 1;
}

/* Checks the new settings, asks first if they change the rules of saved runs, then saves. */
/* Why the settings being edited cannot be saved, or NULL: a rule out of bounds, more offers than fit on
 * screen, or a starting deck card a character cannot use. */
static const char *reason_not_to_save(const sts_settings *compatible)
{
    static char reason[192];
    sts_settings_problem_info info;
    sts_settings_problem found = sts_settings_check(&g_settings_edit, &info);
    const char *problem;

    describe_problem(found, &info, reason, sizeof reason);
    problem = found != STS_PROBLEM_NONE ? reason : offers_off_screen(&g_settings_edit);
    /* Cards that do not belong to a character can only come from the file, so its words will do. */
    if (!problem && g_data_ok)
        problem = sts_run_check_starters(&g_db, compatible);
    return problem;
}

static void save_settings(int confirmed)
{
    static sts_settings compatible;
    const char *problem;

    g_error[0] = 0;
    g_status[0] = 0;
    g_dirty = 1;
    if (!has_unsaved_changes()) {
        SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CANCEL);
        _snprintf(g_status, sizeof g_status - 1, "%s", tr("Nothing has changed."));
        return;
    }
    compatible = g_settings_edit;
    sts_settings_force_compatible(&compatible);
    problem = reason_not_to_save(&compatible);
    if (problem) {
        SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CANCEL);
        set_error(tr("Cannot save"), problem);
        g_state = ST_SETTINGS;
        return;
    }
    if (!confirmed && list_affected_runs(sts_settings_hash(&compatible)) > 0) {
        SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CONFIRM);
        g_state = ST_SETTINGS_CONFIRM;
        return;
    }
    g_state = ST_SETTINGS;
    if (!write_settings_file()) {
        SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CANCEL);
        return;
    }
    slog("settings.ini saved from the settings screen");
    g_settings_file = g_settings_edit;

    /* The new rules apply straight away. */
    g_data_ok = 0;
    if (!load_data()) {
        SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CANCEL);
        return;
    }
    apply_rules_to_active_run();
    _snprintf(g_status, sizeof g_status - 1, "%s", tr("Saved."));
    g_status[sizeof g_status - 1] = 0;
    SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CONFIRM);
}

/* settings.ini as it is now, with these rules in place of its own. The settings that are not rules
 * keep the file's values. Returns 0, with g_error set, if the file cannot be read. */
static int file_with_rules(const sts_settings *rules, sts_settings *out)
{
    char rule[64];
    int wau;

    if (!read_settings_file(out))
        return 0;
    for (wau = 0; wau < sts_settings_rule_count(); wau++)
        if (sts_settings_rule_text(rules, wau, rule, sizeof rule))
            sts_settings_set_rule(out, rule);
    return 1;
}

int runs_following_other_rules(const sts_settings *rules)
{
    static sts_settings changed;

    g_settings_affected[0] = 0;
    if (!file_with_rules(rules, &changed))
        return 0;
    sts_settings_force_compatible(&changed);
    return list_affected_runs(sts_settings_hash(&changed));
}

rule_change g_rule_changes[RULE_CHANGE_MAX];
int g_rule_change_count = 0;
int g_rule_changes_hidden = 0;

int rule_table_rows(void)
{
    return 16 - (g_rule_changes_hidden > 0) - 2 * (g_rules_restore_others > 0);
}

int rule_table_line_count(void)
{
    int wau, count = 0;

    for (wau = 0; wau < g_rule_change_count; wau++)
        count += 1 + (wau == 0 || g_rule_changes[wau].menu != g_rule_changes[wau - 1].menu);
    return count;
}

/* Whether a setting is among those already looked at, by section and key. */
static int already_listed(const char *const *sections, const char *const *keys, int count, const settings_row *row)
{
    int wau;

    for (wau = 0; wau < count; wau++)
        if (strcmp(sections[wau], row->section) == 0 && strcmp(keys[wau], row->key) == 0)
            return 1;
    return 0;
}

static void add_rule_change(int menu, const settings_row *row, const char *now, const char *restored)
{
    rule_change *change = &g_rule_changes[g_rule_change_count++];

    change->menu = menu;
    change->label = row->label;
    _snprintf(change->now, sizeof change->now - 1, "%s", now);
    change->now[sizeof change->now - 1] = 0;
    _snprintf(change->restored, sizeof change->restored - 1, "%s", restored);
    change->restored[sizeof change->restored - 1] = 0;
}

void describe_rule_changes(const sts_settings *rules)
{
    static sts_settings current, changed;
    static settings_row rows[MAX_SETTINGS_ROWS];
    const char *seen_section[SETTINGS_MENU_COUNT * MAX_SETTINGS_ROWS], *seen_key[SETTINGS_MENU_COUNT * MAX_SETTINGS_ROWS];
    int seen_count = 0, described = 0, differing = 0;
    int menu, pass, wau;

    g_rule_change_count = 0;
    g_rule_changes_hidden = 0;
    if (!read_settings_file(&current) || !file_with_rules(rules, &changed))
        return;
    /* Some rows only show for some values (the chance of an offer), so the rows under both settings. */
    for (menu = 0; menu < SETTINGS_MENU_COUNT; menu++) {
        for (pass = 0; pass < 2; pass++) {
            int count;

            g_settings_edit = pass == 0 ? current : changed;
            count = build_menu_rows(menu, rows);
            for (wau = 0; wau < count && g_rule_change_count < RULE_CHANGE_MAX; wau++) {
                char before[128], after[128];

                if (!rows[wau].key || already_listed(seen_section, seen_key, seen_count, &rows[wau]))
                    continue;
                seen_section[seen_count] = rows[wau].section;
                seen_key[seen_count++] = rows[wau].key;
                /* Each value as the screen shows it, under the settings in the file and under the run's. */
                g_settings_edit = current;
                settings_value_text(&rows[wau], before, sizeof before);
                g_settings_edit = changed;
                settings_value_text(&rows[wau], after, sizeof after);
                g_settings_edit = pass == 0 ? current : changed;
                if (strcmp(before, after) == 0)
                    continue;
                add_rule_change(menu, &rows[wau], before, after);
                described++;
            }
        }
    }
    for (wau = 0; wau < sts_settings_rule_count(); wau++) {
        char before[64], after[64];

        sts_settings_rule_text(&current, wau, before, sizeof before);
        sts_settings_rule_text(&changed, wau, after, sizeof after);
        differing += strcmp(before, after) != 0;
    }
    g_rule_changes_hidden = differing > described ? differing - described : 0;
    g_settings_edit = current;
}

int restore_rules(const sts_settings *rules)
{
    const char *problem;

    g_error[0] = 0;
    if (!file_with_rules(rules, &g_settings_edit))
        return 0;
    if (!read_settings_file(&g_settings_file))
        return 0;
    problem = sts_settings_validate(&g_settings_edit);
    if (!problem)
        problem = offers_off_screen(&g_settings_edit);
    if (problem) {
        set_error(tr("Cannot go back to these rules"), problem);
        return 0;
    }
    if (!write_settings_file())
        return 0;
    slog("settings.ini now holds the rules of the run for character %d", g_run.character);
    g_settings_file = g_settings_edit;
    g_data_ok = 0;
    if (!load_data())
        return 0;
    apply_rules_to_active_run();
    return 1;
}

static void leave_settings(void)
{
    if (has_unsaved_changes() && !g_settings_leave_armed) {
        g_settings_leave_armed = 1;
        SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CANCEL);
        g_dirty = 1;
        return;
    }
    g_settings_leave_armed = 0;
    g_error[0] = 0;
    g_status[0] = 0;
    SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CANCEL);
    go_to_chooser_at(CHOOSE_SETTINGS);
}

static void open_menu(int menu)
{
    g_settings_menu = menu;
    build_rows();
    g_cursor = 0;
    g_settings_top = 0;
    g_error[0] = 0;
    g_status[0] = 0;
    SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CONFIRM);
    g_dirty = 1;
}

/* Back from a menu to the list of menus, keeping its changes until they are saved. */
static void close_menu(void)
{
    g_cursor = g_settings_menu;
    g_settings_menu = -1;
    build_rows();
    g_settings_top = 0;
    g_error[0] = 0;
    g_status[0] = 0;
    SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CANCEL);
    g_dirty = 1;
}

/* ---------- the seed's text box ---------- */

static int g_seed_entry_is_open = 0;
char g_seed_entry[STS_SEED_SIZE];
static unsigned char g_key_was_down[256];
static int g_backspace_frames = 0;   /* how long Backspace has been held, for deleting while held */
static int g_backspace_waiting = 0;  /* held when the box opened, so not counted until let go */

int seed_entry_open(void)
{
    return g_seed_entry_is_open;
}

static int key_down(int key)
{
    return (GetAsyncKeyState(key) & 0x8000) != 0;
}

/* The keys held when the box opens (the confirm key, which may be a letter) only count once let go. */
static void open_seed_entry(void)
{
    int key;

    _snprintf(g_seed_entry, sizeof g_seed_entry - 1, "%s", g_settings_edit.seed);
    g_seed_entry[sizeof g_seed_entry - 1] = 0;
    for (key = 0; key < 256; key++)
        g_key_was_down[key] = (unsigned char)key_down(key);
    /* Held already, it waits to be let go, as the other keys do. */
    g_backspace_frames = 0;
    g_backspace_waiting = key_down(VK_BACK);
    g_seed_entry_is_open = 1;
    g_error[0] = 0;
    g_status[0] = 0;
    g_dirty = 1;
    SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CONFIRM);
}

/* 1 on the frame a key goes down. */
static int key_pressed(int key)
{
    int down = key_down(key), pressed = down && !g_key_was_down[key];

    g_key_was_down[key] = (unsigned char)down;
    return pressed;
}

static void add_seed_letter(char letter)
{
    size_t length = strlen(g_seed_entry);

    if (length + 1 >= sizeof g_seed_entry) {
        SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CANCEL);
        return;
    }
    g_seed_entry[length] = letter;
    g_seed_entry[length + 1] = 0;
    SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_MOVE);
    g_dirty = 1;
}

/* Letters as typed (seeds tell capitals from small letters), digits, - and _. Enter keeps it, Esc
 * leaves it as it was. */
void seed_entry_update(void)
{
    DWORD process = 0;
    int key;

    GetWindowThreadProcessId(GetForegroundWindow(), &process);
    if (process != GetCurrentProcessId())
        return;
    for (key = 'A'; key <= 'Z'; key++) {
        int capital = key_down(VK_SHIFT) != ((GetKeyState(VK_CAPITAL) & 1) != 0);

        if (key_pressed(key))
            add_seed_letter((char)(capital ? key : key - 'A' + 'a'));
    }
    for (key = '0'; key <= '9'; key++)
        if (key_pressed(key))
            add_seed_letter((char)key);
    for (key = VK_NUMPAD0; key <= VK_NUMPAD9; key++)
        if (key_pressed(key))
            add_seed_letter((char)('0' + key - VK_NUMPAD0));
    if (key_pressed(VK_OEM_MINUS))
        add_seed_letter(key_down(VK_SHIFT) ? '_' : '-');
    if (key_pressed(VK_SUBTRACT))
        add_seed_letter('-');
    /* Held, it goes on deleting at the pace a held direction moves through a menu. */
    if (!key_down(VK_BACK))
        g_backspace_waiting = 0;
    g_backspace_frames = key_down(VK_BACK) && !g_backspace_waiting ? g_backspace_frames + 1 : 0;
    if (g_backspace_frames > 0 && repeats_while_held(g_backspace_frames) && g_seed_entry[0]) {
        g_seed_entry[strlen(g_seed_entry) - 1] = 0;
        SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_MOVE);
        g_dirty = 1;
    }
    if (key_pressed(VK_RETURN)) {
        sts_settings_set(&g_settings_edit, "run", "seed", g_seed_entry);
        g_seed_entry_is_open = 0;
        rebuild_rows_keeping_cursor();
        SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CONFIRM);
        g_dirty = 1;
    } else if (key_pressed(VK_ESCAPE)) {
        g_seed_entry_is_open = 0;
        SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CANCEL);
        g_dirty = 1;
    }
}

static int on_seed_row(void)
{
    return g_settings_menu >= 0 && g_cursor < g_settings_row_count && g_settings_rows[g_cursor].key
        && strcmp(g_settings_rows[g_cursor].key, "seed") == 0;
}

int settings_on_seed_row(void)
{
    return on_seed_row();
}

void on_settings_event(screen_event event)
{
    if (event != EV_CANCEL && g_settings_leave_armed) {
        g_settings_leave_armed = 0;
        g_dirty = 1;
    }
    if (g_settings_menu < 0) {
        switch (event) {
        case EV_UP:    move_settings_cursor(-1); break;
        case EV_DOWN:  move_settings_cursor(1); break;
        case EV_CONFIRM:
            if (g_cursor < SETTINGS_MENU_COUNT)
                open_menu(g_cursor);
            else
                save_settings(0);
            break;
        case EV_CANCEL: leave_settings(); break;
        default: break;
        }
        return;
    }
    switch (event) {
    case EV_UP:    move_settings_cursor(-1); break;
    case EV_DOWN:  move_settings_cursor(1); break;
    case EV_LEFT:  change_setting(-1); break;
    case EV_RIGHT: change_setting(1); break;
    case EV_CONFIRM:
        /* On the seed, the confirm button types one instead of saving. */
        if (on_seed_row())
            open_seed_entry();
        else
            save_settings(0);
        break;
    case EV_CANCEL:  close_menu(); break;
    case EV_ALT:     reset_setting(); break;
    default: break;
    }
}

void on_settings_confirm_event(screen_event event)
{
    switch (event) {
    case EV_CONFIRM:
        save_settings(1);
        break;
    case EV_CANCEL:
        SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CANCEL);
        g_state = ST_SETTINGS;
        g_dirty = 1;
        break;
    default:
        break;
    }
}
