#include <stdlib.h>
#include <string.h>
#include "settings.h"
#include "util.h"

#define MAX_LINE 128

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
    {"loser", STS_WHO_LOSER}, {"nobody", STS_WHO_NOBODY}, {NULL, 0}
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

void sts_settings_defaults(sts_settings *settings)
{
    memset(settings, 0, sizeof *settings);
    settings->rules = sts_rules_vanilla();
    default_offer_style(&settings->draft);
    default_offer_style(&settings->reward);

    settings->draft_picks = 20;
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
    settings->pick_visibility = STS_VISIBILITY_AFTER_CONFIRM;
    settings->opponent_deck = STS_OPPONENT_DECK_CHANGE;
    settings->items_enabled = 0;
    settings->allow_rule_changes = 0;
    settings->draft_show_deck = 1;
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

/* Keys shared by [draft] and [reward]. Returns -1 for an unknown key, else success. */
static int set_offer_key(sts_offer_settings *offer_settings, const char *key, char *value)
{
    if (strcmp(key, "offers") == 0)
        return set_int(value, 0, 1000, &offer_settings->offer_count);
    if (strcmp(key, "copies_min") == 0)
        return set_int(value, 0, 1000, &offer_settings->copies_min);
    if (strcmp(key, "copies_max") == 0)
        return set_int(value, 0, 1000, &offer_settings->copies_max);
    if (strcmp(key, "types") == 0)
        return set_types(value, &offer_settings->type_mask);
    if (strcmp(key, "offer_style") == 0) {
        int number;
        int ok = find_named(value, g_styles, &number);
        if (ok)
            offer_settings->style = (sts_offer_style)number;
        return ok;
    }
    if (strcmp(key, "system_weight") == 0)
        return set_int(value, 0, 1000, &offer_settings->weights[STS_CARD_SYSTEM]);
    if (strcmp(key, "skill_weight") == 0)
        return set_int(value, 0, 1000, &offer_settings->weights[STS_CARD_SKILL]);
    if (strcmp(key, "spell_weight") == 0)
        return set_int(value, 0, 1000, &offer_settings->weights[STS_CARD_SPELL]);
    return -1;
}

/* Returns -1 for an unknown key, else success (1) or a bad value (0). */
static int set_key(sts_settings *settings, const char *section, const char *key, char *value)
{
    int number, ok;

    if (strcmp(section, "deck") == 0) {
        if (strcmp(key, "size_min") == 0)
            return set_int(value, 0, 1000, &settings->rules.deck_min);
        if (strcmp(key, "size_max") == 0)
            return set_int(value, 0, 1000, &settings->rules.deck_max);
        if (strcmp(key, "copy_limit") == 0)
            return set_int(value, 0, 1000, &settings->rules.copy_limit);
    } else if (strcmp(section, "draft") == 0) {
        if (strcmp(key, "picks") == 0)
            return set_int(value, 0, 1000, &settings->draft_picks);
        if (strcmp(key, "show_deck") == 0)
            return find_named(value, g_bool, &settings->draft_show_deck);
        return set_offer_key(&settings->draft, key, value);
    } else if (strcmp(section, "reward") == 0) {
        if (strcmp(key, "mode") == 0) {
            ok = find_named(value, g_modes, &number);
            if (ok) settings->reward.mode = (sts_reward_mode)number;
            return ok;
        }
        if (strcmp(key, "who_picks") == 0) {
            ok = find_named(value, g_who, &number);
            if (ok) settings->who_picks = (sts_who_picks)number;
            return ok;
        }
        if (strcmp(key, "replacement") == 0) {
            ok = find_named(value, g_replace, &number);
            if (ok) settings->reward.replace = (sts_replace_control)number;
            return ok;
        }
        return set_offer_key(&settings->reward, key, value);
    } else if (strcmp(section, "netplay") == 0) {
        if (strcmp(key, "pick_visibility") == 0) {
            ok = find_named(value, g_visibility, &number);
            if (ok) settings->pick_visibility = (sts_pick_visibility)number;
            return ok;
        }
        if (strcmp(key, "opponent_deck") == 0) {
            ok = find_named(value, g_opponent, &number);
            if (ok) settings->opponent_deck = (sts_opponent_deck)number;
            return ok;
        }
    } else if (strcmp(section, "items") == 0) {
        if (strcmp(key, "enabled") == 0)
            return find_named(value, g_bool, &settings->items_enabled);
    } else if (strcmp(section, "run") == 0) {
        if (strcmp(key, "allow_rule_changes") == 0)
            return find_named(value, g_bool, &settings->allow_rule_changes);
    }
    return -1;
}

static int known_section(const char *name)
{
    return strcmp(name, "deck") == 0 || strcmp(name, "draft") == 0 || strcmp(name, "reward") == 0
        || strcmp(name, "netplay") == 0 || strcmp(name, "items") == 0 || strcmp(name, "run") == 0;
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

static const char *validate_offer(const sts_offer_settings *offer_settings, const sts_rules *rules)
{
    if (offer_settings->offer_count < 1 || offer_settings->offer_count > STS_OFFER_MAX)
        return "offers must be between 1 and 8";
    if (offer_settings->copies_min < 1 || offer_settings->copies_max < offer_settings->copies_min)
        return "copies_min must be at least 1 and not above copies_max";
    if (offer_settings->copies_max > rules->copy_limit)
        return "copies_max cannot be above copy_limit";
    if (offer_settings->type_mask == 0 || (offer_settings->type_mask & ~(unsigned)STS_TYPE_ALL))
        return "types must list at least one of system, skill, spell";
    if (offer_settings->style == STS_OFFER_WEIGHTED) {
        int type, total = 0;

        for (type = 0; type < 3; type++)
            if (offer_settings->type_mask & STS_TYPE_BIT(type))
                total += offer_settings->weights[type];
        if (total == 0)
            return "weighted offers need a weight above 0 on a type that can be offered";
    }
    return NULL;
}

const char *sts_settings_validate(const sts_settings *settings)
{
    const sts_rules *rules = &settings->rules;
    const char *msg;

    if (rules->deck_min < 1 || rules->deck_min > rules->deck_max)
        return "size_min must be at least 1 and not above size_max";
    if (rules->deck_max > STS_MAX_DECK)
        return "size_max cannot be above 255";
    if (rules->copy_limit < 1 || rules->copy_limit > STS_GAME_COPY_LIMIT)
        return "copy_limit must be between 1 and 4";

    if (settings->draft_picks < 1)
        return "picks must be at least 1";
    msg = validate_offer(&settings->draft, rules);
    if (msg)
        return msg;
    /* The draft must always be able to finish at a legal size. With copies_min 1 a pick
     * fits whenever the deck has room, so enough picks always reach size_min. Otherwise
     * only a fixed count can be guaranteed: a varying one could stop just short, with
     * no room left for another minimum-sized pick. */
    if (settings->draft.copies_min == 1) {
        if (settings->draft_picks < rules->deck_min)
            return "picks is too low to reach size_min";
    } else {
        int total = settings->draft_picks * settings->draft.copies_min;
        if (settings->draft.copies_min != settings->draft.copies_max)
            return "a draft with copies_min above 1 needs copies_min equal to copies_max";
        if (total < rules->deck_min || total > rules->deck_max)
            return "picks times copies must land between size_min and size_max";
    }

    if (settings->reward.mode != STS_REWARD_SKIP) {
        msg = validate_offer(&settings->reward, rules);
        if (msg)
            return msg;
        if (settings->reward.mode != STS_REWARD_REPLACE && rules->deck_min == rules->deck_max)
            return "reward mode add or remove needs size_min below size_max";
    }
    return NULL;
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
        settings->draft_picks = defaults.draft_picks;
        settings->draft = defaults.draft;
        settings->reward = defaults.reward;
    }
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

uint32_t sts_settings_hash(const sts_settings *settings)
{
    uint32_t hash = STS_FNV_START;

    hash = hash_int(hash, settings->rules.deck_min);
    hash = hash_int(hash, settings->rules.deck_max);
    hash = hash_int(hash, settings->rules.copy_limit);
    hash = hash_int(hash, settings->draft_picks);
    hash = hash_offer(hash, &settings->draft);
    hash = hash_offer(hash, &settings->reward);
    hash = hash_int(hash, (int)settings->who_picks);
    hash = hash_int(hash, (int)settings->pick_visibility);
    hash = hash_int(hash, (int)settings->opponent_deck);
    return hash_int(hash, settings->items_enabled);
}
