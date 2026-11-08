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
    row->help[0] = help;
    row->help_count = 1;
    return row;
}

static void add_help_line(settings_row *row, const char *line)
{
    if (row && row->help_count < SETTINGS_HELP_LINES)
        row->help[row->help_count++] = line;
}

/* The draft and the reward have the same offer settings, except that only the reward's copies can
 * be set: the draft adds one card per pick. */
static void add_offer_rows(const char *section, const char *copies_help)
{
    const char *weight_help = tr("How often this type is offered compared with the others, when weighted.");
    const char *weight_off = tr("0 turns it off.");
    settings_row *row;

    add_row(section, "offers", section[0] == 'd' ? tr("Cards per pick") : tr("Cards per offer"),
            section[0] == 'd' ? tr("How many cards each pick offers. The screen fits up to 3.")
                              : tr("How many cards each offer shows. The screen fits up to 3."));
    if (copies_help) {
        add_row(section, "copies_min", tr("Fewest copies"), copies_help);
        add_row(section, "copies_max", tr("Most copies"), copies_help);
    }
    add_row(section, "types", tr("Card types offered"), tr("Which card types can be offered."));
    row = add_row(section, "offer_style", tr("How types are chosen"),
                  tr("Weighted: each offer's card type is rolled from the weights below."));
    add_help_line(row, tr("One of each: every offer shows a system card, a skill card and a spell card."));
    row = add_row(section, "system_weight", tr("System card weight"), weight_help);
    add_help_line(row, weight_off);
    row = add_row(section, "skill_weight", tr("Skill card weight"), weight_help);
    add_help_line(row, weight_off);
    row = add_row(section, "spell_weight", tr("Spell card weight"), weight_help);
    add_help_line(row, weight_off);
}

const char *settings_menu_name(int menu)
{
    switch (menu) {
    case SETTINGS_MENU_DRAFT:  return tr("Draft");
    case SETTINGS_MENU_REWARD: return tr("Rewards");
    case SETTINGS_MENU_RUN:    return tr("Runs");
    }
    return tr("Save changes");
}

const char *settings_menu_help(int menu)
{
    switch (menu) {
    case SETTINGS_MENU_DRAFT:  return tr("Building a run's deck, one card per pick until it is full: the starting deck, and what each pick offers.");
    case SETTINGS_MENU_REWARD: return tr("The card offer after each match, and what it changes in the deck.");
    case SETTINGS_MENU_RUN:    return tr("What happens to a run when the rules change.");
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
        row = add_row("starter", "enabled", tr("Starting deck"),
                      tr("On: 8 set cards for the character to start, then 12 picks fill the rest."));
        add_help_line(row, tr("Off: each draft starts from an empty deck and takes 20 picks."));
        row = add_row("draft", "show_deck", tr("Show the deck so far"), tr("On: the deck so far is shown beside the offers."));
        add_help_line(row, tr("Off: a blind draft."));
        add_help_line(row, tr("This is not a rule, so runs carry on when it changes."));
        add_offer_rows("draft", NULL);
        break;
    case SETTINGS_MENU_REWARD:
        row = add_row("reward", "mode", tr("Reward mode"), tr("Replace a card: a card comes in and the same number leave."));
        add_help_line(row, tr("No offers: the deck never changes after a match."));
        add_help_line(row, tr("Adding and removing change the deck size, which the mod cannot do yet."));
        row = add_row("reward", "who_picks", tr("Who gets an offer"),
                      tr("Both players: each player gets their own offer after every match."));
        add_help_line(row, tr("Winner only: the loser keeps their deck."));
        add_help_line(row, tr("Loser only: the winner keeps their deck."));
        row = add_row("reward", "replacement", tr("What leaves the deck"),
                      tr("Player chooses cards: pick exactly which cards leave."));
        add_help_line(row, tr("Player chooses a type: random cards of the chosen type leave."));
        add_help_line(row, tr("Random: the cards that leave are chosen at random."));
        add_offer_rows("reward", tr("Each offer gives a random number of copies in this range."));
        break;
    case SETTINGS_MENU_RUN:
        row = add_row("run", "allow_rule_changes", tr("Carry on after rule changes"),
                      tr("On: a run made under other rules carries on if its deck is legal under them."));
        add_help_line(row, tr("Off: a run made under other rules can only start over."));
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
    if (strcmp(raw, "both") == 0) return tr("Both players");
    if (strcmp(raw, "winner") == 0) return tr("Winner only");
    if (strcmp(raw, "loser") == 0) return tr("Loser only");
    if (strcmp(raw, "replace") == 0) return tr("Replace a card");
    if (strcmp(raw, "skip") == 0) return tr("No offers");
    if (strcmp(raw, "add") == 0) return tr("Add a card");
    if (strcmp(raw, "remove") == 0) return tr("Remove a card");
    if (strcmp(raw, "choose") == 0) return tr("Player chooses cards");
    if (strcmp(raw, "by_type") == 0) return tr("Player chooses a type");
    if (strcmp(raw, "random") == 0) return tr("Random");
    if (strcmp(raw, "weighted") == 0) return tr("Weighted");
    if (strcmp(raw, "one_of_each") == 0) return tr("One of each");
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
    if (strcmp(row->key, "types") == 0) {
        /* "system,skill" becomes "System, Skill". */
        size_t used = 0, wau;

        for (wau = 0; raw[wau] && used + 3 < size; wau++) {
            if (raw[wau] == ',') {
                out[used++] = ',';
                out[used++] = ' ';
            } else if (wau == 0 || raw[wau - 1] == ',') {
                out[used++] = (char)(raw[wau] - 'a' + 'A');
            } else {
                out[used++] = raw[wau];
            }
        }
        out[used] = 0;
        return;
    }
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
    /* A random swap for both players needs the opponent's mod to agree, so it is not offered. */
    if (strcmp(row->section, "reward") == 0 && strcmp(row->key, "who_picks") == 0)
        for (wau = 0; wau < 4 && g_settings_edit.who_picks == STS_WHO_NOBODY; wau++)
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
    case STS_PROBLEM_DRAFT_TYPES:
    case STS_PROBLEM_REWARD_TYPES:
        _snprintf(out, size - 1, "%s", tr("At least one card type must be offered."));
        break;
    case STS_PROBLEM_DRAFT_WEIGHTS:
        _snprintf(out, size - 1, "%s", tr("Draft: every card type offered has a weight of 0, so nothing can be offered."));
        break;
    case STS_PROBLEM_REWARD_WEIGHTS:
        _snprintf(out, size - 1, "%s", tr("Rewards: every card type offered has a weight of 0, so nothing can be offered."));
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
static void apply_rules_to_active_run(void)
{
    char path[MAX_PATH + 32];
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
    if (status == STS_RUN_OK && run.phase != STS_PHASE_DRAFT) {
        g_run = run;
        activate_run_deck();
    }
}

/* The characters whose saved runs follow the current rules but would not follow the new ones. */
static int list_affected_runs(uint32_t new_hash)
{
    uint32_t old_hash = sts_settings_hash(&g_settings);
    int character, count = 0;
    size_t used = 0;

    g_settings_affected[0] = 0;
    if (new_hash == old_hash)
        return 0;
    for (character = 0; character < STS_NUM_CHARACTERS; character++) {
        const char *name = character_label(character);

        if (!g_has_save[character] || g_saved_hash[character] != old_hash)
            continue;
        if (used + strlen(name) + 3 < sizeof g_settings_affected)
            used += (size_t)sprintf(g_settings_affected + used, "%s%s", count ? ", " : "", name);
        count++;
    }
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
    {
        static char reason[192];
        sts_settings_problem_info info;
        sts_settings_problem found = sts_settings_check(&g_settings_edit, &info);

        describe_problem(found, &info, reason, sizeof reason);
        problem = found != STS_PROBLEM_NONE ? reason : offers_off_screen(&g_settings_edit);
    }
    compatible = g_settings_edit;
    sts_settings_force_compatible(&compatible);
    /* Cards that do not belong to a character can only come from the file, so its words will do. */
    if (!problem && g_data_ok)
        problem = sts_run_check_starters(&g_db, &compatible);
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
    case EV_CONFIRM: save_settings(0); break;
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
