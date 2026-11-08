#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pool.h"
#include "util.h"

#define MAX_LINE 128
#define NUM_FIELDS 4

/* Digits only: atoi() would accept "12x" and hide a damaged data file. */
static int parse_uint(const char *text, int max, int *out)
{
    int value = 0;

    if (!*text)
        return 0;
    for (; *text; text++) {
        if (*text < '0' || *text > '9')
            return 0;
        value = value * 10 + (*text - '0');
        if (value > max)
            return 0;
    }
    *out = value;
    return 1;
}

static sts_card_list *list_for_owner(sts_card_db *db, const char *owner)
{
    int wau;

    if (strcmp(owner, "system") == 0)
        return &db->system;
    for (wau = 0; wau < STS_NUM_CHARACTERS; wau++)
        if (strcmp(owner, sts_character_name(wau)) == 0)
            return &db->characters[wau];
    return NULL;
}

static int parse_type(const char *text, uint8_t *out)
{
    if (strcmp(text, "system") == 0)
        *out = STS_CARD_SYSTEM;
    else if (strcmp(text, "skill") == 0)
        *out = STS_CARD_SKILL;
    else if (strcmp(text, "spell") == 0)
        *out = STS_CARD_SPELL;
    else
        return 0;
    return 1;
}

/* Splits line in place on commas. Returns the number of fields found. */
static int split_fields(char *line, char *fields[NUM_FIELDS])
{
    int count = 0;
    char *cursor = line;

    for (;;) {
        char *comma = strchr(cursor, ',');
        if (count == NUM_FIELDS)
            return NUM_FIELDS + 1;
        fields[count++] = cursor;
        if (!comma)
            return count;
        *comma = 0;
        cursor = comma + 1;
    }
}

static sts_db_status parse_line(char *line, sts_card_db *db)
{
    char *fields[NUM_FIELDS];
    sts_card_list *list;
    sts_card card;
    int id, cost, wau;

    if (split_fields(line, fields) != NUM_FIELDS)
        return STS_DB_BAD_LINE;
    list = list_for_owner(db, fields[0]);
    if (!list || !parse_uint(fields[1], 0xFFFF, &id) || !parse_type(fields[2], &card.type)
        || !parse_uint(fields[3], 0xFF, &cost))
        return STS_DB_BAD_LINE;
    card.id = (uint16_t)id;
    card.cost_or_slot = (uint8_t)cost;

    for (wau = 0; wau < list->count; wau++)
        if (list->cards[wau].id == card.id)
            return STS_DB_DUPLICATE;
    if (list->count >= STS_POOL_MAX_CARDS)
        return STS_DB_OVERFLOW;
    list->cards[list->count++] = card;
    return STS_DB_OK;
}

sts_db_status sts_card_db_parse(const char *text, size_t len, sts_card_db *out, int *err_line)
{
    size_t pos = 0;
    int line_no = 0, wau;

    memset(out, 0, sizeof *out);
    if (err_line)
        *err_line = 0;

    while (pos < len) {
        char line[MAX_LINE];
        size_t start = pos, length;
        sts_db_status status;

        while (pos < len && text[pos] != '\n')
            pos++;
        length = pos - start;
        pos++;              /* past the newline */
        line_no++;
        if (length > 0 && text[start + length - 1] == '\r')
            length--;
        if (length == 0 || text[start] == '#')
            continue;
        if (length >= MAX_LINE) {
            if (err_line)
                *err_line = line_no;
            return STS_DB_BAD_LINE;
        }
        memcpy(line, text + start, length);
        line[length] = 0;
        status = parse_line(line, out);
        if (status != STS_DB_OK) {
            if (err_line)
                *err_line = line_no;
            return status;
        }
    }

    if (out->system.count == 0)
        return STS_DB_INCOMPLETE;
    for (wau = 0; wau < STS_NUM_CHARACTERS; wau++)
        if (out->characters[wau].count == 0)
            return STS_DB_INCOMPLETE;
    return STS_DB_OK;
}

sts_db_status sts_card_db_load(const char *path, sts_card_db *out, int *err_line)
{
    size_t size = 0;
    char *buf = sts_read_file(path, &size);
    sts_db_status status;

    if (err_line)
        *err_line = 0;
    if (!buf)
        return STS_DB_IO;
    status = sts_card_db_parse(buf, size, out, err_line);
    free(buf);
    return status;
}

const char *sts_db_status_str(sts_db_status status)
{
    switch (status) {
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
    int wau;

    for (wau = 0; wau < list->count; wau++)
        if (list->cards[wau].id == id)
            return &list->cards[wau];
    return NULL;
}

const sts_card *sts_card_db_find(const sts_card_db *db, int character, uint16_t id)
{
    const sts_card *card;

    if (character < 0 || character >= STS_NUM_CHARACTERS)
        return NULL;
    card = find_in_list(&db->system, id);
    return card ? card : find_in_list(&db->characters[character], id);
}

int sts_skill_label(const sts_card_db *db, int character, uint16_t id, char *out)
{
    /* Indexed by a skill's cost_or_slot value, which is the same for all its upgrades. */
    static const char *const inputs[] = { "236", "214", "623", "421", "22" };
    const sts_card *card = sts_card_db_find(db, character, id);
    const sts_card_list *list;
    int wau, is_default = 1;

    out[0] = 0;
    if (!card || card->type != STS_CARD_SKILL)
        return 0;
    if (card->cost_or_slot >= sizeof inputs / sizeof inputs[0])
        return 0;

    /* The default skill is the lowest ID on its input. */
    list = &db->characters[character];
    for (wau = 0; wau < list->count; wau++) {
        const sts_card *other = &list->cards[wau];
        if (other->type == STS_CARD_SKILL && other->cost_or_slot == card->cost_or_slot && other->id < id)
            is_default = 0;
    }
    sprintf(out, "%c%s", is_default ? 'd' : 'a', inputs[card->cost_or_slot]);
    return 1;
}

int sts_deck_cards_legal(const sts_card_db *db, int character, const sts_deck *deck, uint16_t *bad_id)
{
    int wau;

    for (wau = 0; wau < deck->count; wau++) {
        if (!sts_card_db_find(db, character, deck->ids[wau])) {
            if (bad_id)
                *bad_id = deck->ids[wau];
            return 0;
        }
    }
    return 1;
}
