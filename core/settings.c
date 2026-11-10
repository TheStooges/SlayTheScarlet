#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "settings.h"
#include "util.h"

#define MAX_LINE 128
#define VALUE_SIZE 64

typedef struct {
    const char *name;
    int value;
} named_value;

static const named_value g_modes[] = {
    {"add", STS_REWARD_ADD}, {"replace", STS_REWARD_REPLACE},
    {"remove", STS_REWARD_REMOVE}, {"skip", STS_REWARD_SKIP}, {NULL, 0}
};
static const named_value g_who[] = {
    {"both", STS_WHO_BOTH}, {"winner", STS_WHO_WINNER},
    {"loser", STS_WHO_LOSER}, {"random", STS_WHO_RANDOM}, {NULL, 0}
};
static const named_value g_offer_picks[] = {
    {"choose", STS_OFFER_PICK_CHOOSE}, {"random", STS_OFFER_PICK_RANDOM}, {NULL, 0}
};
static const named_value g_replace[] = {
    {"choose", STS_REPLACE_CHOOSE}, {"by_type", STS_REPLACE_BY_TYPE},
    {"random", STS_REPLACE_RANDOM}, {NULL, 0}
};
static const named_value g_styles[] = {
    {"weighted", STS_OFFER_WEIGHTED}, {"one_of_each", STS_OFFER_ONE_OF_EACH}, {NULL, 0}
};
static const named_value g_visibility[] = {
    {"hidden", STS_VISIBILITY_HIDDEN}, {"after_confirm", STS_VISIBILITY_AFTER_CONFIRM},
    {"live", STS_VISIBILITY_LIVE}, {NULL, 0}
};
static const named_value g_opponent[] = {
    {"change", STS_OPPONENT_DECK_CHANGE}, {"full", STS_OPPONENT_DECK_FULL}, {NULL, 0}
};
static const named_value g_bool[] = {
    {"false", 0}, {"true", 1}, {NULL, 0}
};

static void default_offer_style(sts_offer_settings *offer_settings)
{
    offer_settings->style = STS_OFFER_WEIGHTED;
    offer_settings->weights[STS_CARD_SYSTEM] = 1;
    offer_settings->weights[STS_CARD_SKILL] = 1;
    offer_settings->weights[STS_CARD_SPELL] = 1;
}

/* The starter's defaults: two defensive system cards for everyone, then for each character 
 * a default skill and a cheap, useful spell that is worth swapping out later. */
static const uint16_t g_starter_common[] = {0, 0, 13, 13};

typedef struct {
    uint16_t skill;
    uint16_t spell;
} starter_cards;

static const starter_cards g_starter_cards[STS_NUM_CHARACTERS] = {
    {100, 200},    /* Reimu:     d236 Hakurei Amulet                  Spirit Sign "Fantasy Orb"               */
    {102, 212},    /* Marisa:    d22  Grand Stardust                  Light Sign "Earthlight Ray"             */
    {102, 206},    /* Sakuya:    d236 Magic Star Sword                Silver Sign "Silver Bound"              */
    {100, 206},    /* Alice:     d236 Doll Placement                  Spy Sign "Seeker Dolls"                 */
    {103, 212},    /* Patchouli: d214 Autumn Edge                     Earth Metal Sign "Emerald Megalopolis"  */
    {102, 200},    /* Youmu:     d214 Slash of Life and Death         Human Sign "Slash of Present"           */
    {100, 201},    /* Remilia:   d236 Demon Lord Walk                 Critical "Heart Break"                  */
    {101, 200},    /* Yuyuko:    d236 Light of the Undead             Death Sign "Ghastly Dream"              */
    {100, 204},    /* Yukari:    d236 Open, Lamented Box of Urashima  Shikigami "Chen"                        */
    {101, 201},    /* Suika:     d623 Gnome -Dense-                   Drunk Sign "Art of Oni Binding"         */
    {101, 203},    /* Reisen:    d214 Illusionary Blast               Fascination "Crown Vision"              */
    {103, 212},    /* Aya:       d421 Pelting in the Night            Squall "Sarutahiko's Guidance"          */
    {101, 200},    /* Komachi:   d623 Bound Spirits of the Earth      Nautical Sign "Flow of the River"       */
    {101, 206},    /* Iku:       d623 Veils like Water                Lightning Fish "Swimming Thunder Shot"  */
    {100, 209},    /* Tenshi:    d214 Sword of the Earth              Weather "Scarlet Weather Pressure"      */
    {100, 206},    /* Sanae:     d236 Wind-Call                       Miracle "Mid-Day Supernova"             */
    {102, 202},    /* Cirno:     d22  Little Iceberg                  Ice Cubes "Cold Sprinkler"              */
    {101, 202},    /* Meiling:   d623 Red Cannon                      Chi Sign "Star Bullets"                 */
    {102, 205},    /* Utsuho:    d22  Break Sun                       Nuclear Sign "Creeping Sun"             */
    {100, 203},    /* Suwako:    d214 Ancient Geyser                  Native God "Froggy Braves the Elements" */
};

static void default_starter(sts_settings *settings)
{
    int wau, character;

    settings->starter_enabled = 0;
    settings->starter_common.count = 0;
    for (wau = 0; wau < (int)(sizeof g_starter_common / sizeof g_starter_common[0]); wau++)
        settings->starter_common.ids[settings->starter_common.count++] = g_starter_common[wau];
    for (character = 0; character < STS_NUM_CHARACTERS; character++) {
        sts_starter_list *list = &settings->starter[character];

        list->ids[0] = g_starter_cards[character].skill;
        list->ids[1] = g_starter_cards[character].skill;
        list->ids[2] = g_starter_cards[character].spell;
        list->ids[3] = g_starter_cards[character].spell;
        list->count = 4;
    }
}

void sts_settings_defaults(sts_settings *settings)
{
    memset(settings, 0, sizeof *settings);
    settings->rules = sts_rules_vanilla();
    default_offer_style(&settings->draft);
    default_offer_style(&settings->reward);

    settings->draft.mode = STS_REWARD_ADD;
    settings->draft.offer_count = 3;
    settings->draft.copies_min = 1;
    settings->draft.copies_max = 1;
    settings->draft.type_mask = STS_TYPE_ALL;
    settings->draft.replace = STS_REPLACE_RANDOM;

    settings->reward.mode = STS_REWARD_REPLACE;
    settings->reward.offer_count = 3;
    settings->reward.copies_min = 1;
    settings->reward.copies_max = 1;
    settings->reward.type_mask = STS_TYPE_ALL;
    settings->reward.replace = STS_REPLACE_CHOOSE;

    settings->who_picks = STS_WHO_BOTH;
    settings->random_chance = 50;
    settings->pick_visibility = STS_VISIBILITY_AFTER_CONFIRM;
    settings->opponent_deck = STS_OPPONENT_DECK_CHANGE;
    settings->items_enabled = 0;
    settings->allow_rule_changes = 0;
    settings->draft_show_deck = 1;
    default_starter(settings);
}

static int set_int(const char *value, int min, int max, int *out)
{
    int number = 0;
    const char *cursor = value;

    if (!*cursor)
        return 0;
    for (; *cursor; cursor++) {
        if (*cursor < '0' || *cursor > '9')
            return 0;
        number = number * 10 + (*cursor - '0');
        if (number > max)
            return 0;
    }
    if (number < min)
        return 0;
    *out = number;
    return 1;
}

static int find_named(const char *value, const named_value *table, int *out)
{
    for (; table->name; table++) {
        if (strcmp(value, table->name) == 0) {
            *out = table->value;
            return 1;
        }
    }
    return 0;
}

/* "system,skill,spell", any subset; at least one is required. */
static int set_types(char *value, unsigned *out)
{
    unsigned mask = 0;

    for (;;) {
        char *comma = strchr(value, ',');
        if (comma)
            *comma = 0;
        if (strcmp(value, "system") == 0)
            mask |= STS_TYPE_BIT(STS_CARD_SYSTEM);
        else if (strcmp(value, "skill") == 0)
            mask |= STS_TYPE_BIT(STS_CARD_SKILL);
        else if (strcmp(value, "spell") == 0)
            mask |= STS_TYPE_BIT(STS_CARD_SPELL);
        else
            return 0;
        if (!comma)
            break;
        value = comma + 1;
    }
    *out = mask;
    return 1;
}

static char lower_case(char letter)
{
    return letter >= 'A' && letter <= 'Z' ? (char)(letter - 'A' + 'a') : letter;
}

/* Matches a key to a character's name, ignoring case: "reimu" or "Reimu". Returns -1 if none. */
static int character_key(const char *key)
{
    int character;

    for (character = 0; character < STS_NUM_CHARACTERS; character++) {
        const char *name = sts_character_name(character);
        size_t wau = 0;

        while (key[wau] && lower_case(key[wau]) == lower_case(name[wau]))
            wau++;
        if (!key[wau] && !name[wau])
            return character;
    }
    return -1;
}

/* What kind of value a setting holds, and where it lives in sts_settings. Every setting is found
 * through find_setting, so parsing, the settings screen and rewriting the file agree on them. */
typedef enum { KIND_INT, KIND_BOOL, KIND_NAMED, KIND_TYPES } value_kind;

typedef struct {
    value_kind kind;
    void *field;                /* int (or an enum, which is int-sized), or an unsigned mask */
    const named_value *names;   /* KIND_NAMED */
    int step_min, step_max;     /* KIND_INT: the range the settings screen steps through */
    int step_size;              /* KIND_INT: how far one step goes, 0 meaning 1 */
} setting_ref;

/* Enum fields are read and written as int. */
typedef char enum_is_int_sized[sizeof(sts_who_picks) == sizeof(int) && sizeof(sts_reward_mode) == sizeof(int) ? 1 : -1];

static void set_ref(setting_ref *ref, value_kind kind, void *field, const named_value *names, int step_min, int step_max)
{
    ref->kind = kind;
    ref->field = field;
    ref->names = names;
    ref->step_min = step_min;
    ref->step_max = step_max;
}

/* Keys shared by [draft] and [reward]. Fewest and most copies step only as far as each other, so
 * the screen cannot offer a range that is upside down. */
static int find_offer_setting(sts_offer_settings *offer_settings, const sts_rules *rules, const char *key,
                              setting_ref *ref)
{
    int copy_limit = rules->copy_limit < STS_GAME_COPY_LIMIT ? rules->copy_limit : STS_GAME_COPY_LIMIT;

    if (strcmp(key, "offers") == 0)
        set_ref(ref, KIND_INT, &offer_settings->offer_count, NULL, 1, STS_OFFER_MAX);
    else if (strcmp(key, "copies_min") == 0)
        set_ref(ref, KIND_INT, &offer_settings->copies_min, NULL, 1, offer_settings->copies_max);
    else if (strcmp(key, "copies_max") == 0)
        set_ref(ref, KIND_INT, &offer_settings->copies_max, NULL, offer_settings->copies_min, copy_limit);
    else if (strcmp(key, "types") == 0)
        set_ref(ref, KIND_TYPES, &offer_settings->type_mask, NULL, 0, 0);
    else if (strcmp(key, "offer_style") == 0)
        set_ref(ref, KIND_NAMED, &offer_settings->style, g_styles, 0, 0);
    else if (strcmp(key, "system_weight") == 0)
        set_ref(ref, KIND_INT, &offer_settings->weights[STS_CARD_SYSTEM], NULL, 0, 20);
    else if (strcmp(key, "skill_weight") == 0)
        set_ref(ref, KIND_INT, &offer_settings->weights[STS_CARD_SKILL], NULL, 0, 20);
    else if (strcmp(key, "spell_weight") == 0)
        set_ref(ref, KIND_INT, &offer_settings->weights[STS_CARD_SPELL], NULL, 0, 20);
    else
        return 0;
    return 1;
}

/* Finds a setting by its section and key. Returns 0 for an unknown one. */
static int find_setting(sts_settings *settings, const char *section, const char *key, setting_ref *ref)
{
    memset(ref, 0, sizeof *ref);
    if (strcmp(section, "deck") == 0) {
        if (strcmp(key, "size_min") == 0)
            set_ref(ref, KIND_INT, &settings->rules.deck_min, NULL, 1, STS_MAX_DECK);
        else if (strcmp(key, "size_max") == 0)
            set_ref(ref, KIND_INT, &settings->rules.deck_max, NULL, 1, STS_MAX_DECK);
        else if (strcmp(key, "copy_limit") == 0)
            set_ref(ref, KIND_INT, &settings->rules.copy_limit, NULL, 1, STS_GAME_COPY_LIMIT);
        else
            return 0;
        return 1;
    }
    if (strcmp(section, "draft") == 0) {
        /* The draft's copies are always 1. */
        if (strcmp(key, "copies_min") == 0 || strcmp(key, "copies_max") == 0)
            return 0;
        if (strcmp(key, "show_deck") == 0)
            set_ref(ref, KIND_BOOL, &settings->draft_show_deck, g_bool, 0, 0);
        else
            return find_offer_setting(&settings->draft, &settings->rules, key, ref);
        return 1;
    }
    if (strcmp(section, "reward") == 0) {
        if (strcmp(key, "mode") == 0)
            set_ref(ref, KIND_NAMED, &settings->reward.mode, g_modes, 0, 0);
        else if (strcmp(key, "who_picks") == 0)
            set_ref(ref, KIND_NAMED, &settings->who_picks, g_who, 0, 0);
        else if (strcmp(key, "random_chance") == 0) {
            set_ref(ref, KIND_INT, &settings->random_chance, NULL, 0, 100);
            ref->step_size = 25;
        }
        else if (strcmp(key, "offer_pick") == 0)
            set_ref(ref, KIND_NAMED, &settings->offer_pick, g_offer_picks, 0, 0);
        else if (strcmp(key, "replacement") == 0)
            set_ref(ref, KIND_NAMED, &settings->reward.replace, g_replace, 0, 0);
        else
            return find_offer_setting(&settings->reward, &settings->rules, key, ref);
        return 1;
    }
    if (strcmp(section, "netplay") == 0) {
        if (strcmp(key, "pick_visibility") == 0)
            set_ref(ref, KIND_NAMED, &settings->pick_visibility, g_visibility, 0, 0);
        else if (strcmp(key, "opponent_deck") == 0)
            set_ref(ref, KIND_NAMED, &settings->opponent_deck, g_opponent, 0, 0);
        else
            return 0;
        return 1;
    }
    if (strcmp(section, "items") == 0) {
        if (strcmp(key, "enabled") != 0)
            return 0;
        set_ref(ref, KIND_BOOL, &settings->items_enabled, g_bool, 0, 0);
        return 1;
    }
    if (strcmp(section, "starter") == 0) {
        if (strcmp(key, "enabled") != 0)
            return 0;
        set_ref(ref, KIND_BOOL, &settings->starter_enabled, g_bool, 0, 0);
        return 1;
    }
    if (strcmp(section, "run") == 0) {
        if (strcmp(key, "allow_rule_changes") != 0)
            return 0;
        set_ref(ref, KIND_BOOL, &settings->allow_rule_changes, g_bool, 0, 0);
        return 1;
    }
    return 0;
}

static int read_number(const setting_ref *ref)
{
    int number;

    memcpy(&number, ref->field, sizeof number);
    return number;
}

static void write_number(const setting_ref *ref, int number)
{
    memcpy(ref->field, &number, sizeof number);
}

/* Sets a setting from its text in the file. The text may be changed. Returns 1, or 0 for a bad value. */
static int set_from_text(const setting_ref *ref, char *value)
{
    int number;

    switch (ref->kind) {
    case KIND_INT:
        if (!set_int(value, 0, 1000, &number))
            return 0;
        write_number(ref, number);
        return 1;
    case KIND_BOOL:
    case KIND_NAMED:
        if (!find_named(value, ref->names, &number))
            return 0;
        write_number(ref, number);
        return 1;
    case KIND_TYPES:
        return set_types(value, (unsigned *)ref->field);
    }
    return 0;
}

static const char *const g_type_names[3] = {"system", "skill", "spell"};

/* A setting's value as the file writes it. */
static void format_value(const setting_ref *ref, char *out, size_t size)
{
    char text[VALUE_SIZE];
    size_t used = 0;
    int wau;

    text[0] = 0;
    switch (ref->kind) {
    case KIND_INT:
        sprintf(text, "%d", read_number(ref));
        break;
    case KIND_BOOL:
    case KIND_NAMED: {
        const named_value *entry;
        int number = read_number(ref);

        sprintf(text, "%d", number);
        for (entry = ref->names; entry->name; entry++) {
            if (entry->value == number) {
                strcpy(text, entry->name);
                break;
            }
        }
        break;
    }
    case KIND_TYPES:
        for (wau = 0; wau < 3; wau++) {
            if (*(const unsigned *)ref->field & STS_TYPE_BIT(wau))
                used += (size_t)sprintf(text + used, "%s%s", used ? "," : "", g_type_names[wau]);
        }
        break;
    }
    if (size > 0) {
        strncpy(out, text, size - 1);
        out[size - 1] = 0;
    }
}

/* Returns -1 for an unknown key, else success (1) or a bad value (0). */
static int set_key(sts_settings *settings, const char *section, const char *key, char *value)
{
    setting_ref ref;

    if (!find_setting(settings, section, key, &ref))
        return -1;
    return set_from_text(&ref, value);
}

/* Settings that versions up to 0.2 had and that are now fixed: the draft adds one copy per pick
 * until the deck reaches size_min, and the starter's cards are built in. Files from those versions
 * still list them, so they are read and ignored rather than refused as unknown. */
static int obsolete_key(const char *section, const char *key)
{
    if (strcmp(section, "draft") == 0)
        return strcmp(key, "picks") == 0 || strcmp(key, "copies_min") == 0 || strcmp(key, "copies_max") == 0;
    if (strcmp(section, "starter") == 0)
        return strcmp(key, "common") == 0 || character_key(key) >= 0;
    return 0;
}

static int known_section(const char *name)
{
    return strcmp(name, "deck") == 0 || strcmp(name, "draft") == 0 || strcmp(name, "reward") == 0
        || strcmp(name, "netplay") == 0 || strcmp(name, "items") == 0 || strcmp(name, "starter") == 0
        || strcmp(name, "run") == 0;
}

static char *trim(char *text)
{
    char *end;

    while (*text == ' ' || *text == '\t')
        text++;
    end = text + strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r'))
        *--end = 0;
    return text;
}

static sts_settings_status parse_line(char *line, char *section, size_t section_size, sts_settings *out)
{
    char *equals, *key, *value;
    int result;

    line = trim(line);
    if (!*line || *line == '#' || *line == ';')
        return STS_SETTINGS_OK;
    if (*line == '[') {
        size_t count = strlen(line);
        if (line[count - 1] != ']')
            return STS_SETTINGS_BAD_LINE;
        line[count - 1] = 0;
        line++;
        if (!known_section(line))
            return STS_SETTINGS_UNKNOWN_SECTION;
        if (strlen(line) >= section_size)
            return STS_SETTINGS_UNKNOWN_SECTION;
        strcpy(section, line);
        return STS_SETTINGS_OK;
    }
    equals = strchr(line, '=');
    if (!equals)
        return STS_SETTINGS_BAD_LINE;
    *equals = 0;
    key = trim(line);
    value = trim(equals + 1);
    if (!*section)
        return STS_SETTINGS_UNKNOWN_KEY;   /* a key before any section */
    if (obsolete_key(section, key))
        return STS_SETTINGS_OK;
    /* Up to 0.2, who_picks=nobody was an automatic swap. That is offer_pick and replacement now,
     * and in 0.3's first builds it acted as both, so a file that still says it reads as both. */
    if (strcmp(section, "reward") == 0 && strcmp(key, "who_picks") == 0 && strcmp(value, "nobody") == 0)
        strcpy(value, "both");
    result = set_key(out, section, key, value);
    if (result < 0)
        return STS_SETTINGS_UNKNOWN_KEY;
    return result ? STS_SETTINGS_OK : STS_SETTINGS_BAD_VALUE;
}

sts_settings_status sts_settings_parse(const char *text, size_t len, sts_settings *out, int *err_line)
{
    char section[16] = "";
    size_t pos = 0;
    int line_no = 0;

    sts_settings_defaults(out);
    if (err_line)
        *err_line = 0;

    while (pos < len) {
        char line[MAX_LINE];
        size_t start = pos, length;
        sts_settings_status status;

        while (pos < len && text[pos] != '\n')
            pos++;
        length = pos - start;
        pos++;
        line_no++;
        if (length >= MAX_LINE) {
            /* A long comment is harmless; a long setting would be truncated into nonsense. */
            size_t k = start;
            while (k < start + length && (text[k] == ' ' || text[k] == '\t'))
                k++;
            if (text[k] == '#' || text[k] == ';')
                continue;
            if (err_line)
                *err_line = line_no;
            return STS_SETTINGS_BAD_LINE;
        }
        memcpy(line, text + start, length);
        line[length] = 0;
        status = parse_line(line, section, sizeof section, out);
        if (status != STS_SETTINGS_OK) {
            if (err_line)
                *err_line = line_no;
            return status;
        }
    }
    return STS_SETTINGS_OK;
}

sts_settings_status sts_settings_load(const char *path, sts_settings *out, int *err_line)
{
    size_t len = 0;
    char *buf = sts_read_file(path, &len);
    sts_settings_status status;

    if (err_line)
        *err_line = 0;
    if (!buf) {
        sts_settings_defaults(out);
        return STS_SETTINGS_IO;
    }
    status = sts_settings_parse(buf, len, out, err_line);
    free(buf);
    return status;
}

const char *sts_settings_status_str(sts_settings_status status)
{
    switch (status) {
    case STS_SETTINGS_OK:              return "ok";
    case STS_SETTINGS_IO:              return "file could not be read";
    case STS_SETTINGS_BAD_LINE:        return "bad line";
    case STS_SETTINGS_UNKNOWN_SECTION: return "unknown section";
    case STS_SETTINGS_UNKNOWN_KEY:     return "unknown setting";
    case STS_SETTINGS_BAD_VALUE:       return "bad value";
    }
    return "unknown";
}

/* Problems with the offer settings, as offsets from STS_PROBLEM_DRAFT_OFFERS or
 * STS_PROBLEM_REWARD_OFFERS, which list them in the same order. */
static int check_offer(const sts_offer_settings *offer_settings, const sts_rules *rules)
{
    if (offer_settings->offer_count < 1 || offer_settings->offer_count > STS_OFFER_MAX)
        return 0;
    if (offer_settings->copies_min < 1 || offer_settings->copies_max < offer_settings->copies_min)
        return 1;
    if (offer_settings->copies_max > rules->copy_limit)
        return 2;
    if (offer_settings->type_mask == 0 || (offer_settings->type_mask & ~(unsigned)STS_TYPE_ALL))
        return 3;
    if (offer_settings->style == STS_OFFER_WEIGHTED) {
        int type, total = 0;

        for (type = 0; type < 3; type++)
            if (offer_settings->type_mask & STS_TYPE_BIT(type))
                total += offer_settings->weights[type];
        if (total == 0)
            return 4;
    }
    return -1;
}

void sts_settings_starter(const sts_settings *settings, int character, sts_deck *out)
{
    int wau;

    sts_deck_clear(out);
    if (!settings->starter_enabled || character < 0 || character >= STS_NUM_CHARACTERS)
        return;
    for (wau = 0; wau < settings->starter_common.count; wau++)
        sts_deck_add(out, settings->starter_common.ids[wau]);
    for (wau = 0; wau < settings->starter[character].count; wau++)
        sts_deck_add(out, settings->starter[character].ids[wau]);
}

int sts_settings_draft_length(const sts_settings *settings, int character)
{
    sts_deck starter;
    int room = 0;

    sts_settings_starter(settings, character, &starter);
    if (starter.count < settings->rules.deck_min)
        room = settings->rules.deck_min - starter.count;
    return room;
}

/* The draft must be able to finish at a legal size from this character's starter. It adds one
 * copy per pick until the deck reaches size_min, so it does whenever the starter itself fits. */
static sts_settings_problem check_draft_size(const sts_settings *settings, int character, sts_settings_problem_info *info)
{
    const sts_rules *rules = &settings->rules;
    sts_deck starter;
    int wau;

    sts_settings_starter(settings, character, &starter);
    info->character = character;
    if (starter.count > rules->deck_max)
        return STS_PROBLEM_STARTER_TOO_BIG;
    for (wau = 0; wau < starter.count; wau++) {
        if (sts_deck_count_of(&starter, starter.ids[wau]) > rules->copy_limit) {
            info->number = starter.ids[wau];
            return STS_PROBLEM_STARTER_COPIES;
        }
    }
    info->character = -1;
    return STS_PROBLEM_NONE;
}

sts_settings_problem sts_settings_check(const sts_settings *settings, sts_settings_problem_info *info)
{
    const sts_rules *rules = &settings->rules;
    sts_settings_problem_info ignored;
    sts_settings_problem problem;
    int character, offer;

    if (!info)
        info = &ignored;
    info->character = -1;
    info->number = 0;
    if (rules->deck_min < 1 || rules->deck_min > rules->deck_max)
        return STS_PROBLEM_DECK_SIZE;
    if (rules->deck_max > STS_MAX_DECK)
        return STS_PROBLEM_DECK_TOO_BIG;
    if (rules->copy_limit < 1 || rules->copy_limit > STS_GAME_COPY_LIMIT)
        return STS_PROBLEM_COPY_LIMIT;

    offer = check_offer(&settings->draft, rules);
    if (offer >= 0)
        return (sts_settings_problem)(STS_PROBLEM_DRAFT_OFFERS + offer);
    for (character = 0; character < STS_NUM_CHARACTERS; character++) {
        problem = check_draft_size(settings, character, info);
        if (problem != STS_PROBLEM_NONE)
            return problem;
    }

    if (settings->random_chance > 100)
        return STS_PROBLEM_RANDOM_CHANCE;

    if (settings->reward.mode != STS_REWARD_SKIP) {
        offer = check_offer(&settings->reward, rules);
        if (offer >= 0)
            return (sts_settings_problem)(STS_PROBLEM_REWARD_OFFERS + offer);
        if (settings->reward.mode != STS_REWARD_REPLACE && rules->deck_min == rules->deck_max)
            return STS_PROBLEM_REWARD_SIZE;
    }
    return STS_PROBLEM_NONE;
}

const char *sts_settings_validate(const sts_settings *settings)
{
    static char message[128];
    sts_settings_problem_info info;
    const char *name;

    switch (sts_settings_check(settings, &info)) {
    case STS_PROBLEM_NONE:              return NULL;
    case STS_PROBLEM_DECK_SIZE:         return "size_min must be at least 1 and not above size_max";
    case STS_PROBLEM_DECK_TOO_BIG:      return "size_max cannot be above 255";
    case STS_PROBLEM_COPY_LIMIT:        return "copy_limit must be between 1 and 4";
    case STS_PROBLEM_DRAFT_OFFERS:
    case STS_PROBLEM_REWARD_OFFERS:     return "offers must be between 1 and 8";
    case STS_PROBLEM_DRAFT_COPIES:
    case STS_PROBLEM_REWARD_COPIES:     return "copies_min must be at least 1 and not above copies_max";
    case STS_PROBLEM_DRAFT_COPY_LIMIT:
    case STS_PROBLEM_REWARD_COPY_LIMIT: return "copies_max cannot be above copy_limit";
    case STS_PROBLEM_DRAFT_TYPES:
    case STS_PROBLEM_REWARD_TYPES:      return "types must list at least one of system, skill, spell";
    case STS_PROBLEM_DRAFT_WEIGHTS:
    case STS_PROBLEM_REWARD_WEIGHTS:    return "weighted offers need a weight above 0 on a type that can be offered";
    case STS_PROBLEM_REWARD_SIZE:       return "reward mode add or remove needs size_min below size_max";
    case STS_PROBLEM_RANDOM_CHANCE:     return "random_chance cannot be above 100";
    case STS_PROBLEM_STARTER_TOO_BIG:
        name = sts_character_name(info.character);
        sprintf(message, "%s's starter has more cards than size_max", name);
        return message;
    case STS_PROBLEM_STARTER_COPIES:
        name = sts_character_name(info.character);
        sprintf(message, "%s's starter has more than copy_limit copies of card %d", name, info.number);
        return message;
    }
    return "unknown problem";
}

void sts_settings_force_compatible(sts_settings *settings)
{
    sts_settings defaults;

    sts_settings_defaults(&defaults);
    settings->rules = defaults.rules;
    settings->items_enabled = 0;

    /* Add and remove change the deck size, which a vanilla opponent cannot accept. */
    if (settings->reward.mode == STS_REWARD_ADD || settings->reward.mode == STS_REWARD_REMOVE)
        settings->reward.mode = STS_REWARD_REPLACE;

    /* Whatever the player configured for other sizes may not fit; fall back to defaults. */
    if (sts_settings_validate(settings)) {
        settings->draft = defaults.draft;
        settings->reward = defaults.reward;
    }
    /* A starter made for another deck size may not fit either. */
    if (sts_settings_validate(settings))
        settings->starter_enabled = 0;
}

static uint32_t hash_int(uint32_t hash, int number)
{
    unsigned char bytes[4];

    /* Explicit byte order: the fingerprint must match across machines. */
    bytes[0] = (unsigned char)((unsigned)number & 0xFF);
    bytes[1] = (unsigned char)(((unsigned)number >> 8) & 0xFF);
    bytes[2] = (unsigned char)(((unsigned)number >> 16) & 0xFF);
    bytes[3] = (unsigned char)(((unsigned)number >> 24) & 0xFF);
    return sts_fnv1a(hash, bytes, 4);
}

static uint32_t hash_offer(uint32_t hash, const sts_offer_settings *offer_settings)
{
    hash = hash_int(hash, (int)offer_settings->mode);
    hash = hash_int(hash, offer_settings->offer_count);
    hash = hash_int(hash, offer_settings->copies_min);
    hash = hash_int(hash, offer_settings->copies_max);
    hash = hash_int(hash, (int)offer_settings->type_mask);
    hash = hash_int(hash, (int)offer_settings->replace);
    hash = hash_int(hash, (int)offer_settings->style);
    hash = hash_int(hash, offer_settings->weights[STS_CARD_SYSTEM]);
    hash = hash_int(hash, offer_settings->weights[STS_CARD_SKILL]);
    return hash_int(hash, offer_settings->weights[STS_CARD_SPELL]);
}

static uint32_t hash_starter_list(uint32_t hash, const sts_starter_list *list)
{
    int wau;

    hash = hash_int(hash, list->count);
    for (wau = 0; wau < list->count; wau++)
        hash = hash_int(hash, list->ids[wau]);
    return hash;
}

uint32_t sts_settings_hash(const sts_settings *settings)
{
    uint32_t hash = STS_FNV_START;

    hash = hash_int(hash, settings->rules.deck_min);
    hash = hash_int(hash, settings->rules.deck_max);
    hash = hash_int(hash, settings->rules.copy_limit);
    /* Once the picks setting; now always the smallest deck. Still hashed, so runs keep their fingerprint. */
    hash = hash_int(hash, settings->rules.deck_min);
    hash = hash_offer(hash, &settings->draft);
    hash = hash_offer(hash, &settings->reward);
    hash = hash_int(hash, (int)settings->who_picks);
    hash = hash_int(hash, (int)settings->pick_visibility);
    hash = hash_int(hash, (int)settings->opponent_deck);
    hash = hash_int(hash, settings->items_enabled);

    /* The lists only matter while the starter is on. Leaving them out otherwise keeps the
     * fingerprint of runs made before the starter existed. */
    if (settings->starter_enabled) {
        int character;

        hash = hash_int(hash, settings->starter_enabled);
        hash = hash_starter_list(hash, &settings->starter_common);
        for (character = 0; character < STS_NUM_CHARACTERS; character++)
            hash = hash_starter_list(hash, &settings->starter[character]);
    }

    /* Only a rule while offers are at random, so other runs keep their fingerprint. */
    if (settings->who_picks == STS_WHO_RANDOM)
        hash = hash_int(hash, settings->random_chance);
    /* Likewise, so runs from before it existed keep theirs. */
    if (settings->offer_pick != STS_OFFER_PICK_CHOOSE)
        hash = hash_int(hash, (int)settings->offer_pick + 1000);
    return hash;
}

/* Every setting, in the order a new section lists them. */
typedef struct {
    const char *section;
    const char *key;
} setting_key;

static const setting_key g_keys[] = {
    {"deck", "size_min"}, {"deck", "size_max"}, {"deck", "copy_limit"},
    {"draft", "offers"}, {"draft", "show_deck"}, {"draft", "types"}, {"draft", "offer_style"}, {"draft", "system_weight"},
    {"draft", "skill_weight"}, {"draft", "spell_weight"},
    {"starter", "enabled"},
    {"reward", "mode"}, {"reward", "who_picks"}, {"reward", "random_chance"}, {"reward", "offers"}, {"reward", "copies_min"},
    {"reward", "copies_max"}, {"reward", "types"}, {"reward", "offer_style"}, {"reward", "system_weight"},
    {"reward", "skill_weight"}, {"reward", "spell_weight"}, {"reward", "offer_pick"}, {"reward", "replacement"},
    {"netplay", "pick_visibility"}, {"netplay", "opponent_deck"},
    {"items", "enabled"},
    {"run", "allow_rule_changes"},
};
#define KEY_COUNT ((int)(sizeof g_keys / sizeof g_keys[0]))
#define KEY_NAME_SIZE 24

static const char *const g_sections[] = {"deck", "draft", "starter", "reward", "netplay", "items", "run"};
#define SECTION_COUNT ((int)(sizeof g_sections / sizeof g_sections[0]))

/* The index-th setting: its section, and its key copied into key. */
static const char *key_at(int index, char *key)
{
    strcpy(key, g_keys[index].key);
    return g_keys[index].section;
}

static int section_index(const char *name)
{
    int wau;

    for (wau = 0; wau < SECTION_COUNT; wau++)
        if (strcmp(g_sections[wau], name) == 0)
            return wau;
    return -1;
}

int sts_settings_get(const sts_settings *settings, const char *section, const char *key, char *out, size_t size)
{
    setting_ref ref;

    if (size > 0)
        out[0] = 0;
    /* Only read through ref. */
    if (!find_setting((sts_settings *)settings, section, key, &ref))
        return 0;
    format_value(&ref, out, size);
    return 1;
}

sts_settings_status sts_settings_set(sts_settings *settings, const char *section, const char *key, const char *value)
{
    char copy[MAX_LINE];
    setting_ref ref;
    sts_settings trial;

    if (strlen(value) >= sizeof copy)
        return STS_SETTINGS_BAD_VALUE;
    strcpy(copy, value);
    /* A bad value leaves the settings as they were, even one that failed part way. */
    trial = *settings;
    if (!find_setting(&trial, section, key, &ref))
        return STS_SETTINGS_UNKNOWN_KEY;
    if (!set_from_text(&ref, copy))
        return STS_SETTINGS_BAD_VALUE;
    *settings = trial;
    return STS_SETTINGS_OK;
}

/* Type choices in the order the settings screen steps through them: all three first. */
static const unsigned g_type_masks[] = {
    STS_TYPE_ALL,
    STS_TYPE_BIT(STS_CARD_SKILL) | STS_TYPE_BIT(STS_CARD_SPELL),
    STS_TYPE_BIT(STS_CARD_SYSTEM) | STS_TYPE_BIT(STS_CARD_SPELL),
    STS_TYPE_BIT(STS_CARD_SYSTEM) | STS_TYPE_BIT(STS_CARD_SKILL),
    STS_TYPE_BIT(STS_CARD_SPELL),
    STS_TYPE_BIT(STS_CARD_SKILL),
    STS_TYPE_BIT(STS_CARD_SYSTEM),
};
#define TYPE_CHOICES ((int)(sizeof g_type_masks / sizeof g_type_masks[0]))

int sts_settings_step(sts_settings *settings, const char *section, const char *key, int direction)
{
    setting_ref ref;
    int wau, count;

    if (!find_setting(settings, section, key, &ref))
        return 0;
    direction = direction < 0 ? -1 : 1;
    switch (ref.kind) {
    case KIND_INT: {
        int size = ref.step_size > 0 ? ref.step_size : 1;
        int number = read_number(&ref);

        /* A value between steps, from the file, moves onto the next step in that direction. */
        if (number % size != 0)
            number = number - number % size + (direction > 0 ? size : 0);
        else
            number += direction * size;
        if (number > ref.step_max)
            number = ref.step_max;
        if (number < ref.step_min)
            number = ref.step_min;
        write_number(&ref, number);
        return 1;
    }
    case KIND_BOOL:
    case KIND_NAMED: {
        int number = read_number(&ref), current = 0;

        for (count = 0; ref.names[count].name; count++)
            if (ref.names[count].value == number)
                current = count;
        write_number(&ref, ref.names[(current + direction + count) % count].value);
        return 1;
    }
    case KIND_TYPES: {
        unsigned *mask = (unsigned *)ref.field;
        int current = 0;

        for (wau = 0; wau < TYPE_CHOICES; wau++)
            if (g_type_masks[wau] == *mask)
                current = wau;
        *mask = g_type_masks[(current + direction + TYPE_CHOICES) % TYPE_CHOICES];
        return 1;
    }
    }
    return 0;
}

int sts_settings_equal(const sts_settings *first, const sts_settings *second)
{
    char key[KEY_NAME_SIZE], first_value[VALUE_SIZE], second_value[VALUE_SIZE];
    int wau;

    for (wau = 0; wau < KEY_COUNT; wau++) {
        const char *section = key_at(wau, key);

        sts_settings_get(first, section, key, first_value, sizeof first_value);
        sts_settings_get(second, section, key, second_value, sizeof second_value);
        if (strcmp(first_value, second_value) != 0)
            return 0;
    }
    return 1;
}

/* One line of the text being rewritten: [start, end) without the newline, next is where the
 * following line starts. */
typedef struct {
    size_t start, end, next;
} text_line;

static void line_at(const char *text, size_t len, size_t pos, text_line *line)
{
    line->start = pos;
    while (pos < len && text[pos] != '\n')
        pos++;
    line->end = pos;
    line->next = pos < len ? pos + 1 : len;
}

/* Reads a line as the parser does. Returns 1 for a section header (its index in *section, -1 if
 * unknown), 2 for a key line (its key in key), 0 for anything else. */
static int classify_line(const char *text, const text_line *line, int *section, char *key)
{
    char copy[MAX_LINE];
    char *trimmed, *equals;
    size_t length = line->end - line->start;

    if (length >= MAX_LINE)
        return 0;   /* only comments may be this long, or the text would not have parsed */
    memcpy(copy, text + line->start, length);
    copy[length] = 0;
    trimmed = trim(copy);
    if (!*trimmed || *trimmed == '#' || *trimmed == ';')
        return 0;
    if (*trimmed == '[') {
        size_t count = strlen(trimmed);

        trimmed[count - 1] = 0;
        *section = section_index(trimmed + 1);
        return 1;
    }
    equals = strchr(trimmed, '=');
    if (!equals)
        return 0;
    *equals = 0;
    strncpy(key, trim(trimmed), KEY_NAME_SIZE - 1);
    key[KEY_NAME_SIZE - 1] = 0;
    return 2;
}

/* Which of the KEY_COUNT settings a key line in this section is, or -1, matched by the field it
 * sets. Obsolete settings are none of them, so their lines are left as they are. */
static int key_index_of(sts_settings *scratch, const char *section, const char *key)
{
    setting_ref ref, other;
    char name[KEY_NAME_SIZE];
    int wau;

    if (!find_setting(scratch, section, key, &ref))
        return -1;
    for (wau = 0; wau < KEY_COUNT; wau++) {
        const char *other_section = key_at(wau, name);

        if (strcmp(other_section, section) == 0 && find_setting(scratch, other_section, name, &other)
            && other.field == ref.field)
            return wau;
    }
    return -1;
}

typedef struct {
    char *text;
    size_t used;
} output_text;

static void put_bytes(output_text *out, const char *bytes, size_t count)
{
    memcpy(out->text + out->used, bytes, count);
    out->used += count;
}

static void put_text(output_text *out, const char *text)
{
    put_bytes(out, text, strlen(text));
}

/* Adds the settings of one section that the file does not list and that differ from it. */
static void put_missing_keys(output_text *out, int section, const int *present, const sts_settings *original,
                             const sts_settings *settings, const char *newline)
{
    char key[KEY_NAME_SIZE], old_value[VALUE_SIZE], new_value[VALUE_SIZE];
    int wau;

    for (wau = 0; wau < KEY_COUNT; wau++) {
        const char *key_section = key_at(wau, key);

        if (present[wau] || strcmp(key_section, g_sections[section]) != 0)
            continue;
        sts_settings_get(original, key_section, key, old_value, sizeof old_value);
        sts_settings_get(settings, key_section, key, new_value, sizeof new_value);
        if (strcmp(old_value, new_value) == 0)
            continue;
        put_text(out, key);
        put_text(out, "=");
        put_text(out, new_value);
        put_text(out, newline);
    }
}

static int section_has_missing(int section, const int *present, const sts_settings *original,
                               const sts_settings *settings)
{
    char key[KEY_NAME_SIZE], old_value[VALUE_SIZE], new_value[VALUE_SIZE];
    int wau;

    for (wau = 0; wau < KEY_COUNT; wau++) {
        const char *key_section = key_at(wau, key);

        if (present[wau] || strcmp(key_section, g_sections[section]) != 0)
            continue;
        sts_settings_get(original, key_section, key, old_value, sizeof old_value);
        sts_settings_get(settings, key_section, key, new_value, sizeof new_value);
        if (strcmp(old_value, new_value) != 0)
            return 1;
    }
    return 0;
}

char *sts_settings_rewrite(const char *text, size_t len, const sts_settings *settings, size_t *out_len)
{
    static sts_settings original;
    int present[KEY_COUNT];
    size_t section_end[SECTION_COUNT];
    int section_seen[SECTION_COUNT];
    const char *newline;
    output_text out;
    size_t pos;
    int section = -1, wau;

    if (!text)
        len = 0;
    if (sts_settings_parse(text ? text : "", len, &original, NULL) != STS_SETTINGS_OK)
        return NULL;
    newline = len > 0 && memchr(text, '\r', len) ? "\r\n" : "\n";
    memset(present, 0, sizeof present);
    memset(section_seen, 0, sizeof section_seen);

    /* First pass: which settings the file lists, and where each section's last setting ends. */
    for (pos = 0; pos < len;) {
        text_line line;
        char key[KEY_NAME_SIZE];
        int kind;

        line_at(text, len, pos, &line);
        kind = classify_line(text, &line, &section, key);
        if (kind == 1 && section >= 0 && !section_seen[section]) {
            section_seen[section] = 1;
            section_end[section] = line.next;
        } else if (kind == 2 && section >= 0) {
            int index = key_index_of(&original, g_sections[section], key);

            if (index >= 0)
                present[index] = 1;
            section_end[section] = line.next;
        }
        pos = line.next;
    }

    out.text = (char *)malloc(len + (size_t)KEY_COUNT * (MAX_LINE + 8) + (size_t)SECTION_COUNT * 32 + 16);
    if (!out.text)
        return NULL;
    out.used = 0;

    /* Second pass: copy the file, changing only values that differ, and add missing settings
     * after the last setting of their section. */
    section = -1;
    for (pos = 0; pos < len;) {
        text_line line;
        char key[KEY_NAME_SIZE];
        int kind, index = -1;

        line_at(text, len, pos, &line);
        kind = classify_line(text, &line, &section, key);
        if (kind == 2 && section >= 0)
            index = key_index_of(&original, g_sections[section], key);
        if (index >= 0) {
            char name[KEY_NAME_SIZE], old_value[VALUE_SIZE], new_value[VALUE_SIZE];
            const char *key_section = key_at(index, name);

            sts_settings_get(&original, key_section, name, old_value, sizeof old_value);
            sts_settings_get(settings, key_section, name, new_value, sizeof new_value);
            if (strcmp(old_value, new_value) != 0) {
                /* Keep everything up to the value: indentation, the key as written, spaces. */
                size_t value_start = line.start;

                while (text[value_start] != '=')
                    value_start++;
                value_start++;
                while (value_start < line.end && (text[value_start] == ' ' || text[value_start] == '\t'))
                    value_start++;
                put_bytes(&out, text + line.start, value_start - line.start);
                put_text(&out, new_value);
                if (line.end > line.start && text[line.end - 1] == '\r')
                    put_text(&out, "\r");
                put_bytes(&out, text + line.end, line.next - line.end);
                index = -2;   /* written */
            }
        }
        if (index != -2)
            put_bytes(&out, text + line.start, line.next - line.start);
        for (wau = 0; wau < SECTION_COUNT; wau++) {
            if (section_seen[wau] && section_end[wau] == line.next
                && section_has_missing(wau, present, &original, settings)) {
                if (line.next == line.end)
                    put_text(&out, newline);   /* the last line had no newline */
                put_missing_keys(&out, wau, present, &original, settings, newline);
            }
        }
        pos = line.next;
    }

    /* Sections the file does not have at all go at the end. */
    for (wau = 0; wau < SECTION_COUNT; wau++) {
        if (section_seen[wau] || !section_has_missing(wau, present, &original, settings))
            continue;
        if (out.used > 0 && out.text[out.used - 1] != '\n')
            put_text(&out, newline);
        if (out.used > 0)
            put_text(&out, newline);
        put_text(&out, "[");
        put_text(&out, g_sections[wau]);
        put_text(&out, "]");
        put_text(&out, newline);
        put_missing_keys(&out, wau, present, &original, settings, newline);
    }
    out.text[out.used] = 0;
    if (out_len)
        *out_len = out.used;
    return out.text;
}
