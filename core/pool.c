#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pool.h"
#include "util.h"

#define MAX_LINE 128
#define NUM_FIELDS 4

/* Digits only: atoi() would accept "12x" and hide a damaged data file. */
static int parse_uint(const char *s, int max, int *out)
{
    int v = 0;

    if (!*s)
        return 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9')
            return 0;
        v = v * 10 + (*s - '0');
        if (v > max)
            return 0;
    }
    *out = v;
    return 1;
}

static sts_card_list *list_for_owner(sts_card_db *db, const char *owner)
{
    int i;

    if (strcmp(owner, "system") == 0)
        return &db->system;
    for (i = 0; i < STS_NUM_CHARACTERS; i++)
        if (strcmp(owner, sts_character_name(i)) == 0)
            return &db->characters[i];
    return NULL;
}

static int parse_type(const char *s, uint8_t *out)
{
    if (strcmp(s, "system") == 0)
        *out = STS_CARD_SYSTEM;
    else if (strcmp(s, "skill") == 0)
        *out = STS_CARD_SKILL;
    else if (strcmp(s, "spell") == 0)
        *out = STS_CARD_SPELL;
    else
        return 0;
    return 1;
}

/* Splits line in place on commas. Returns the number of fields found. */
static int split_fields(char *line, char *fields[NUM_FIELDS])
{
    int n = 0;
    char *p = line;

    for (;;) {
        char *comma = strchr(p, ',');
        if (n == NUM_FIELDS)
            return NUM_FIELDS + 1;
        fields[n++] = p;
        if (!comma)
            return n;
        *comma = 0;
        p = comma + 1;
    }
}

static sts_db_status parse_line(char *line, sts_card_db *db)
{
    char *fields[NUM_FIELDS];
    sts_card_list *list;
    sts_card card;
    int id, cost, i;

    if (split_fields(line, fields) != NUM_FIELDS)
        return STS_DB_BAD_LINE;
    list = list_for_owner(db, fields[0]);
    if (!list || !parse_uint(fields[1], 0xFFFF, &id) || !parse_type(fields[2], &card.type)
        || !parse_uint(fields[3], 0xFF, &cost))
        return STS_DB_BAD_LINE;
    card.id = (uint16_t)id;
    card.cost_or_slot = (uint8_t)cost;

    for (i = 0; i < list->count; i++)
        if (list->cards[i].id == card.id)
            return STS_DB_DUPLICATE;
    if (list->count >= STS_POOL_MAX_CARDS)
        return STS_DB_OVERFLOW;
    list->cards[list->count++] = card;
    return STS_DB_OK;
}

sts_db_status sts_card_db_parse(const char *text, size_t len, sts_card_db *out, int *err_line)
{
    size_t pos = 0;
    int line_no = 0, i;

    memset(out, 0, sizeof *out);
    if (err_line)
        *err_line = 0;

    while (pos < len) {
        char line[MAX_LINE];
        size_t start = pos, n;
        sts_db_status st;

        while (pos < len && text[pos] != '\n')
            pos++;
        n = pos - start;
        pos++;              /* past the newline */
        line_no++;
        if (n > 0 && text[start + n - 1] == '\r')
            n--;
        if (n == 0 || text[start] == '#')
            continue;
        if (n >= MAX_LINE) {
            if (err_line)
                *err_line = line_no;
            return STS_DB_BAD_LINE;
        }
        memcpy(line, text + start, n);
        line[n] = 0;
        st = parse_line(line, out);
        if (st != STS_DB_OK) {
            if (err_line)
                *err_line = line_no;
            return st;
        }
    }

    if (out->system.count == 0)
        return STS_DB_INCOMPLETE;
    for (i = 0; i < STS_NUM_CHARACTERS; i++)
        if (out->characters[i].count == 0)
            return STS_DB_INCOMPLETE;
    return STS_DB_OK;
}

sts_db_status sts_card_db_load(const char *path, sts_card_db *out, int *err_line)
{
    size_t size = 0;
    char *buf = sts_read_file(path, &size);
    sts_db_status st;

    if (err_line)
        *err_line = 0;
    if (!buf)
        return STS_DB_IO;
    st = sts_card_db_parse(buf, size, out, err_line);
    free(buf);
    return st;
}

const char *sts_db_status_str(sts_db_status s)
{
    switch (s) {
    case STS_DB_OK:         return "ok";
    case STS_DB_IO:         return "file could not be read";
    case STS_DB_BAD_LINE:   return "bad line";
    case STS_DB_DUPLICATE:  return "duplicate card";
    case STS_DB_OVERFLOW:   return "too many cards for one owner";
    case STS_DB_INCOMPLETE: return "an owner has no cards";
    }
    return "unknown";
}

static const sts_card *find_in_list(const sts_card_list *list, uint16_t id)
{
    int i;

    for (i = 0; i < list->count; i++)
        if (list->cards[i].id == id)
            return &list->cards[i];
    return NULL;
}

const sts_card *sts_card_db_find(const sts_card_db *db, int character, uint16_t id)
{
    const sts_card *c;

    if (character < 0 || character >= STS_NUM_CHARACTERS)
        return NULL;
    c = find_in_list(&db->system, id);
    return c ? c : find_in_list(&db->characters[character], id);
}

int sts_skill_label(const sts_card_db *db, int character, uint16_t id, char *out)
{
    /* Indexed by a skill's cost_or_slot value, which is the same for all its upgrades. */
    static const char *const inputs[] = { "236", "214", "623", "421", "22" };
    const sts_card *card = sts_card_db_find(db, character, id);
    const sts_card_list *list;
    int i, is_default = 1;

    out[0] = 0;
    if (!card || card->type != STS_CARD_SKILL)
        return 0;
    if (card->cost_or_slot >= sizeof inputs / sizeof inputs[0])
        return 0;

    /* The default skill is the lowest ID on its input. */
    list = &db->characters[character];
    for (i = 0; i < list->count; i++) {
        const sts_card *other = &list->cards[i];
        if (other->type == STS_CARD_SKILL && other->cost_or_slot == card->cost_or_slot && other->id < id)
            is_default = 0;
    }
    sprintf(out, "%c%s", is_default ? 'd' : 'a', inputs[card->cost_or_slot]);
    return 1;
}

int sts_deck_cards_legal(const sts_card_db *db, int character, const sts_deck *d, uint16_t *bad_id)
{
    int i;

    for (i = 0; i < d->count; i++) {
        if (!sts_card_db_find(db, character, d->ids[i])) {
            if (bad_id)
                *bad_id = d->ids[i];
            return 0;
        }
    }
    return 1;
}
