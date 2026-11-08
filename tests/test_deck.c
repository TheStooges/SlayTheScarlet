/* Tests: Deck model and validation. */
#include "test_util.h"

void test_rules_and_validation(void)
{
    sts_rules rules = sts_rules_vanilla();
    sts_deck deck;
    uint16_t bad = 0xFFFF;
    int wau;

    CHECK(rules.deck_min == 20 && rules.deck_max == 20 && rules.copy_limit == 4);

    fill_legal_deck(&deck, 100);
    CHECK(sts_deck_validate(&deck, &rules, &bad) == STS_DECK_OK);

    deck.count = 19;
    CHECK(sts_deck_validate(&deck, &rules, &bad) == STS_DECK_TOO_FEW);
    fill_legal_deck(&deck, 100);
    sts_deck_add(&deck, 200);
    CHECK(sts_deck_validate(&deck, &rules, &bad) == STS_DECK_TOO_MANY);

    /* 4 copies is fine, 5 is not (the game drops the 5th and then rejects the deck). */
    sts_deck_clear(&deck);
    for (wau = 0; wau < 4; wau++) sts_deck_add(&deck, 7);
    for (wau = 0; wau < 16; wau++) sts_deck_add(&deck, (uint16_t)(100 + wau));
    CHECK(sts_deck_validate(&deck, &rules, &bad) == STS_DECK_OK);
    sts_deck_clear(&deck);
    for (wau = 0; wau < 5; wau++) sts_deck_add(&deck, 7);
    for (wau = 0; wau < 15; wau++) sts_deck_add(&deck, (uint16_t)(100 + wau));
    CHECK(sts_deck_validate(&deck, &rules, &bad) == STS_DECK_COPY_LIMIT && bad == 7);

    /* Rules are settings: other sizes and copy limits are honoured. */
    rules.deck_min = 15; rules.deck_max = 40; rules.copy_limit = 2;
    fill_legal_deck(&deck, 100);
    deck.count = 15;
    CHECK(sts_deck_validate(&deck, &rules, &bad) == STS_DECK_OK);
    sts_deck_add(&deck, 100);
    sts_deck_add(&deck, 100);        /* three copies of 100 now */
    CHECK(sts_deck_validate(&deck, &rules, &bad) == STS_DECK_COPY_LIMIT && bad == 100);
}

void test_deck_helpers(void)
{
    sts_deck deck;
    int wau;
    sts_deck_clear(&deck);
    sts_deck_add(&deck, 205); sts_deck_add(&deck, 3); sts_deck_add(&deck, 101); sts_deck_add(&deck, 3);
    CHECK(sts_deck_count_of(&deck, 3) == 2 && sts_deck_count_of(&deck, 4) == 0);
    sts_deck_sort(&deck);
    CHECK(deck.ids[0] == 3 && deck.ids[1] == 3 && deck.ids[2] == 101 && deck.ids[3] == 205);

    sts_deck_clear(&deck);
    for (wau = 0; wau < STS_MAX_DECK; wau++) CHECK(sts_deck_add(&deck, 1) == 0);
    CHECK(sts_deck_add(&deck, 1) == -1 && deck.count == STS_MAX_DECK);
}
