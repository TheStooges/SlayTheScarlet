/* Helpers shared by the test files. */
#include "test_util.h"

int g_run, g_fail;
sts_card_db g_db;

void fill_legal_deck(sts_deck *deck, int base)
{
    int i;
    sts_deck_clear(deck);
    for (i = 0; i < 20; i++)        /* 20 distinct cards, 1 copy each */
        sts_deck_add(deck, (uint16_t)(base + i));
}

void add_copies(sts_deck *deck, uint16_t id, int count)
{
    int i;
    for (i = 0; i < count; i++)
        sts_deck_add(deck, id);
}

int decks_equal(const sts_deck *first, const sts_deck *second)
{
    return first->count == second->count && memcmp(first->ids, second->ids, (size_t)first->count * 2) == 0;
}

int count_type_in_deck(const sts_deck *deck, int character, int type)
{
    int i, count = 0;
    for (i = 0; i < deck->count; i++) {
        const sts_card *card = sts_card_db_find(&g_db, character, deck->ids[i]);
        if (card && card->type == type) count++;
    }
    return count;
}

sts_settings_status parse_ini(const char *text, sts_settings *settings, int *line)
{
    return sts_settings_parse(text, strlen(text), settings, line);
}

sts_offer_settings add_settings(void)
{
    sts_offer_settings offer_settings;
    offer_settings.mode = STS_REWARD_ADD;
    offer_settings.offer_count = 3;
    offer_settings.copies_min = 1;
    offer_settings.copies_max = 1;
    offer_settings.type_mask = STS_TYPE_ALL;
    offer_settings.replace = STS_REPLACE_CHOOSE;
    offer_settings.style = STS_OFFER_WEIGHTED;
    offer_settings.weights[STS_CARD_SYSTEM] = 1;
    offer_settings.weights[STS_CARD_SKILL] = 1;
    offer_settings.weights[STS_CARD_SPELL] = 1;
    return offer_settings;
}

sts_rules open_rules(void)
{
    sts_rules rules;
    rules.deck_min = 0;
    rules.deck_max = 40;
    rules.copy_limit = 4;
    return rules;
}
