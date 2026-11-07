/* Helpers shared by the test files. */
#include "test_util.h"

int g_run, g_fail;
sts_card_db g_db;

void fill_legal_deck(sts_deck *d, int base)
{
    int i;
    sts_deck_clear(d);
    for (i = 0; i < 20; i++)        /* 20 distinct cards, 1 copy each */
        sts_deck_add(d, (uint16_t)(base + i));
}

void add_copies(sts_deck *d, uint16_t id, int n)
{
    int i;
    for (i = 0; i < n; i++)
        sts_deck_add(d, id);
}

int decks_equal(const sts_deck *a, const sts_deck *b)
{
    return a->count == b->count && memcmp(a->ids, b->ids, (size_t)a->count * 2) == 0;
}

int count_type_in_deck(const sts_deck *d, int character, int type)
{
    int i, n = 0;
    for (i = 0; i < d->count; i++) {
        const sts_card *c = sts_card_db_find(&g_db, character, d->ids[i]);
        if (c && c->type == type) n++;
    }
    return n;
}

sts_settings_status parse_ini(const char *text, sts_settings *s, int *line)
{
    return sts_settings_parse(text, strlen(text), s, line);
}

sts_offer_settings add_settings(void)
{
    sts_offer_settings s;
    s.mode = STS_REWARD_ADD;
    s.offer_count = 3;
    s.copies_min = 1;
    s.copies_max = 1;
    s.type_mask = STS_TYPE_ALL;
    s.replace = STS_REPLACE_CHOOSE;
    return s;
}

sts_rules open_rules(void)
{
    sts_rules r;
    r.deck_min = 0;
    r.deck_max = 40;
    r.copy_limit = 4;
    return r;
}
