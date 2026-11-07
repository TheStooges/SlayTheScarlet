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

void sts_settings_defaults(sts_settings *s)
{
    memset(s, 0, sizeof *s);
    s->rules = sts_rules_vanilla();

    s->draft_picks = 20;
    s->draft.mode = STS_REWARD_ADD;
    s->draft.offer_count = 3;
    s->draft.copies_min = 1;
    s->draft.copies_max = 1;
    s->draft.type_mask = STS_TYPE_ALL;
    s->draft.replace = STS_REPLACE_RANDOM;

    s->reward.mode = STS_REWARD_REPLACE;
    s->reward.offer_count = 3;
    s->reward.copies_min = 1;
    s->reward.copies_max = 1;
    s->reward.type_mask = STS_TYPE_ALL;
    s->reward.replace = STS_REPLACE_CHOOSE;

    s->who_picks = STS_WHO_BOTH;
    s->pick_visibility = STS_VISIBILITY_AFTER_CONFIRM;
    s->opponent_deck = STS_OPPONENT_DECK_CHANGE;
    s->items_enabled = 0;
    s->allow_rule_changes = 0;
}

static int set_int(const char *value, int min, int max, int *out)
{
    int v = 0;
    const char *p = value;

    if (!*p)
        return 0;
    for (; *p; p++) {
        if (*p < '0' || *p > '9')
            return 0;
        v = v * 10 + (*p - '0');
        if (v > max)
            return 0;
    }
    if (v < min)
        return 0;
    *out = v;
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
static int set_offer_key(sts_offer_settings *o, const char *key, char *value)
{
    if (strcmp(key, "offers") == 0)
        return set_int(value, 0, 1000, &o->offer_count);
    if (strcmp(key, "copies_min") == 0)
        return set_int(value, 0, 1000, &o->copies_min);
    if (strcmp(key, "copies_max") == 0)
        return set_int(value, 0, 1000, &o->copies_max);
    if (strcmp(key, "types") == 0)
        return set_types(value, &o->type_mask);
    return -1;
}

/* Returns -1 for an unknown key, else success (1) or a bad value (0). */
static int set_key(sts_settings *s, const char *section, const char *key, char *value)
{
    int v, ok;

    if (strcmp(section, "deck") == 0) {
        if (strcmp(key, "size_min") == 0)
            return set_int(value, 0, 1000, &s->rules.deck_min);
        if (strcmp(key, "size_max") == 0)
            return set_int(value, 0, 1000, &s->rules.deck_max);
        if (strcmp(key, "copy_limit") == 0)
            return set_int(value, 0, 1000, &s->rules.copy_limit);
    } else if (strcmp(section, "draft") == 0) {
        if (strcmp(key, "picks") == 0)
            return set_int(value, 0, 1000, &s->draft_picks);
        return set_offer_key(&s->draft, key, value);
    } else if (strcmp(section, "reward") == 0) {
        if (strcmp(key, "mode") == 0) {
            ok = find_named(value, g_modes, &v);
            if (ok) s->reward.mode = (sts_reward_mode)v;
            return ok;
        }
        if (strcmp(key, "who_picks") == 0) {
            ok = find_named(value, g_who, &v);
            if (ok) s->who_picks = (sts_who_picks)v;
            return ok;
        }
        if (strcmp(key, "replacement") == 0) {
            ok = find_named(value, g_replace, &v);
            if (ok) s->reward.replace = (sts_replace_control)v;
            return ok;
        }
        return set_offer_key(&s->reward, key, value);
    } else if (strcmp(section, "netplay") == 0) {
        if (strcmp(key, "pick_visibility") == 0) {
            ok = find_named(value, g_visibility, &v);
            if (ok) s->pick_visibility = (sts_pick_visibility)v;
            return ok;
        }
        if (strcmp(key, "opponent_deck") == 0) {
            ok = find_named(value, g_opponent, &v);
            if (ok) s->opponent_deck = (sts_opponent_deck)v;
            return ok;
        }
    } else if (strcmp(section, "items") == 0) {
        if (strcmp(key, "enabled") == 0)
            return find_named(value, g_bool, &s->items_enabled);
    } else if (strcmp(section, "run") == 0) {
        if (strcmp(key, "allow_rule_changes") == 0)
            return find_named(value, g_bool, &s->allow_rule_changes);
    }
    return -1;
}

static int known_section(const char *name)
{
    return strcmp(name, "deck") == 0 || strcmp(name, "draft") == 0 || strcmp(name, "reward") == 0
        || strcmp(name, "netplay") == 0 || strcmp(name, "items") == 0 || strcmp(name, "run") == 0;
}

static char *trim(char *s)
{
    char *end;

    while (*s == ' ' || *s == '\t')
        s++;
    end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r'))
        *--end = 0;
    return s;
}

static sts_settings_status parse_line(char *line, char *section, size_t section_size, sts_settings *out)
{
    char *eq, *key, *value;
    int r;

    line = trim(line);
    if (!*line || *line == '#' || *line == ';')
        return STS_SETTINGS_OK;
    if (*line == '[') {
        size_t n = strlen(line);
        if (line[n - 1] != ']')
            return STS_SETTINGS_BAD_LINE;
        line[n - 1] = 0;
        line++;
        if (!known_section(line))
            return STS_SETTINGS_UNKNOWN_SECTION;
        if (strlen(line) >= section_size)
            return STS_SETTINGS_UNKNOWN_SECTION;
        strcpy(section, line);
        return STS_SETTINGS_OK;
    }
    eq = strchr(line, '=');
    if (!eq)
        return STS_SETTINGS_BAD_LINE;
    *eq = 0;
    key = trim(line);
    value = trim(eq + 1);
    if (!*section)
        return STS_SETTINGS_UNKNOWN_KEY;   /* a key before any section */
    r = set_key(out, section, key, value);
    if (r < 0)
        return STS_SETTINGS_UNKNOWN_KEY;
    return r ? STS_SETTINGS_OK : STS_SETTINGS_BAD_VALUE;
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
        size_t start = pos, n;
        sts_settings_status st;

        while (pos < len && text[pos] != '\n')
            pos++;
        n = pos - start;
        pos++;
        line_no++;
        if (n >= MAX_LINE) {
            /* A long comment is harmless; a long setting would be truncated into nonsense. */
            size_t k = start;
            while (k < start + n && (text[k] == ' ' || text[k] == '\t'))
                k++;
            if (text[k] == '#' || text[k] == ';')
                continue;
            if (err_line)
                *err_line = line_no;
            return STS_SETTINGS_BAD_LINE;
        }
        memcpy(line, text + start, n);
        line[n] = 0;
        st = parse_line(line, section, sizeof section, out);
        if (st != STS_SETTINGS_OK) {
            if (err_line)
                *err_line = line_no;
            return st;
        }
    }
    return STS_SETTINGS_OK;
}

sts_settings_status sts_settings_load(const char *path, sts_settings *out, int *err_line)
{
    size_t len = 0;
    char *buf = sts_read_file(path, &len);
    sts_settings_status st;

    if (err_line)
        *err_line = 0;
    if (!buf) {
        sts_settings_defaults(out);
        return STS_SETTINGS_IO;
    }
    st = sts_settings_parse(buf, len, out, err_line);
    free(buf);
    return st;
}

const char *sts_settings_status_str(sts_settings_status s)
{
    switch (s) {
    case STS_SETTINGS_OK:              return "ok";
    case STS_SETTINGS_IO:              return "file could not be read";
    case STS_SETTINGS_BAD_LINE:        return "bad line";
    case STS_SETTINGS_UNKNOWN_SECTION: return "unknown section";
    case STS_SETTINGS_UNKNOWN_KEY:     return "unknown setting";
    case STS_SETTINGS_BAD_VALUE:       return "bad value";
    }
    return "unknown";
}

static const char *validate_offer(const sts_offer_settings *o, const sts_rules *r)
{
    if (o->offer_count < 1 || o->offer_count > STS_OFFER_MAX)
        return "offers must be between 1 and 8";
    if (o->copies_min < 1 || o->copies_max < o->copies_min)
        return "copies_min must be at least 1 and not above copies_max";
    if (o->copies_max > r->copy_limit)
        return "copies_max cannot be above copy_limit";
    if (o->type_mask == 0 || (o->type_mask & ~(unsigned)STS_TYPE_ALL))
        return "types must list at least one of system, skill, spell";
    return NULL;
}

const char *sts_settings_validate(const sts_settings *s)
{
    const sts_rules *r = &s->rules;
    const char *msg;

    if (r->deck_min < 1 || r->deck_min > r->deck_max)
        return "size_min must be at least 1 and not above size_max";
    if (r->deck_max > STS_MAX_DECK)
        return "size_max cannot be above 255";
    if (r->copy_limit < 1 || r->copy_limit > STS_GAME_COPY_LIMIT)
        return "copy_limit must be between 1 and 4";

    if (s->draft_picks < 1)
        return "picks must be at least 1";
    msg = validate_offer(&s->draft, r);
    if (msg)
        return msg;
    /* The draft must always be able to finish at a legal size. With copies_min 1 a pick
     * fits whenever the deck has room, so enough picks always reach size_min. Otherwise
     * only a fixed count can be guaranteed: a varying one could stop just short, with
     * no room left for another minimum-sized pick. */
    if (s->draft.copies_min == 1) {
        if (s->draft_picks < r->deck_min)
            return "picks is too low to reach size_min";
    } else {
        int total = s->draft_picks * s->draft.copies_min;
        if (s->draft.copies_min != s->draft.copies_max)
            return "a draft with copies_min above 1 needs copies_min equal to copies_max";
        if (total < r->deck_min || total > r->deck_max)
            return "picks times copies must land between size_min and size_max";
    }

    if (s->reward.mode != STS_REWARD_SKIP) {
        msg = validate_offer(&s->reward, r);
        if (msg)
            return msg;
        if (s->reward.mode != STS_REWARD_REPLACE && r->deck_min == r->deck_max)
            return "reward mode add or remove needs size_min below size_max";
    }
    return NULL;
}

void sts_settings_force_compatible(sts_settings *s)
{
    sts_settings d;

    sts_settings_defaults(&d);
    s->rules = d.rules;
    s->items_enabled = 0;
    s->pick_visibility = STS_VISIBILITY_HIDDEN;
    s->opponent_deck = STS_OPPONENT_DECK_CHANGE;

    /* Add and remove change the deck size, which a vanilla opponent cannot accept. */
    if (s->reward.mode == STS_REWARD_ADD || s->reward.mode == STS_REWARD_REMOVE)
        s->reward.mode = STS_REWARD_REPLACE;

    /* Whatever the player configured for other sizes may not fit; fall back to defaults. */
    if (sts_settings_validate(s)) {
        s->draft_picks = d.draft_picks;
        s->draft = d.draft;
        s->reward = d.reward;
    }
}

static uint32_t hash_int(uint32_t h, int v)
{
    unsigned char b[4];

    /* Explicit byte order: the fingerprint must match across machines. */
    b[0] = (unsigned char)((unsigned)v & 0xFF);
    b[1] = (unsigned char)(((unsigned)v >> 8) & 0xFF);
    b[2] = (unsigned char)(((unsigned)v >> 16) & 0xFF);
    b[3] = (unsigned char)(((unsigned)v >> 24) & 0xFF);
    return sts_fnv1a(h, b, 4);
}

static uint32_t hash_offer(uint32_t h, const sts_offer_settings *o)
{
    h = hash_int(h, (int)o->mode);
    h = hash_int(h, o->offer_count);
    h = hash_int(h, o->copies_min);
    h = hash_int(h, o->copies_max);
    h = hash_int(h, (int)o->type_mask);
    return hash_int(h, (int)o->replace);
}

uint32_t sts_settings_hash(const sts_settings *s)
{
    uint32_t h = STS_FNV_START;

    h = hash_int(h, s->rules.deck_min);
    h = hash_int(h, s->rules.deck_max);
    h = hash_int(h, s->rules.copy_limit);
    h = hash_int(h, s->draft_picks);
    h = hash_offer(h, &s->draft);
    h = hash_offer(h, &s->reward);
    h = hash_int(h, (int)s->who_picks);
    h = hash_int(h, (int)s->pick_visibility);
    h = hash_int(h, (int)s->opponent_deck);
    return hash_int(h, s->items_enabled);
}
