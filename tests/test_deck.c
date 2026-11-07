/* Tests: Deck model and validation. */
#include "test_util.h"

void test_rules_and_validation(void)
{
    sts_rules r = sts_rules_vanilla();
    sts_deck d;
    uint16_t bad = 0xFFFF;
    int i;

    CHECK(r.deck_min == 20 && r.deck_max == 20 && r.copy_limit == 4);

    fill_legal_deck(&d, 100);
    CHECK(sts_deck_validate(&d, &r, &bad) == STS_DECK_OK);

    d.count = 19;
    CHECK(sts_deck_validate(&d, &r, &bad) == STS_DECK_TOO_FEW);
    fill_legal_deck(&d, 100);
    sts_deck_add(&d, 200);
    CHECK(sts_deck_validate(&d, &r, &bad) == STS_DECK_TOO_MANY);

    /* 4 copies is fine, 5 is not (the game drops the 5th and then rejects the deck). */
    sts_deck_clear(&d);
    for (i = 0; i < 4; i++) sts_deck_add(&d, 7);
    for (i = 0; i < 16; i++) sts_deck_add(&d, (uint16_t)(100 + i));
    CHECK(sts_deck_validate(&d, &r, &bad) == STS_DECK_OK);
    sts_deck_clear(&d);
    for (i = 0; i < 5; i++) sts_deck_add(&d, 7);
    for (i = 0; i < 15; i++) sts_deck_add(&d, (uint16_t)(100 + i));
    CHECK(sts_deck_validate(&d, &r, &bad) == STS_DECK_COPY_LIMIT && bad == 7);

    /* Rules are settings: other sizes and copy limits are honoured. */
    r.deck_min = 15; r.deck_max = 40; r.copy_limit = 2;
    fill_legal_deck(&d, 100);
    d.count = 15;
    CHECK(sts_deck_validate(&d, &r, &bad) == STS_DECK_OK);
    sts_deck_add(&d, 100);
    sts_deck_add(&d, 100);        /* three copies of 100 now */
    CHECK(sts_deck_validate(&d, &r, &bad) == STS_DECK_COPY_LIMIT && bad == 100);
}

void test_deck_helpers(void)
{
    sts_deck d;
    int i;
    sts_deck_clear(&d);
    sts_deck_add(&d, 205); sts_deck_add(&d, 3); sts_deck_add(&d, 101); sts_deck_add(&d, 3);
    CHECK(sts_deck_count_of(&d, 3) == 2 && sts_deck_count_of(&d, 4) == 0);
    sts_deck_sort(&d);
    CHECK(d.ids[0] == 3 && d.ids[1] == 3 && d.ids[2] == 101 && d.ids[3] == 205);

    sts_deck_clear(&d);
    for (i = 0; i < STS_MAX_DECK; i++) CHECK(sts_deck_add(&d, 1) == 0);
    CHECK(sts_deck_add(&d, 1) == -1 && d.count == STS_MAX_DECK);
}
