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

static void deck_from(sts_deck *deck, const uint16_t *ids, int count)
{
    int wau;

    sts_deck_clear(deck);
    for (wau = 0; wau < count; wau++)
        sts_deck_add(deck, ids[wau]);
}

/* What a spectator shows between matches: the cards each deck gained and lost, copies counted. */
void test_deck_changes(void)
{
    /* Two decks a spectator saw (Aya), in the order the packet sent them: two copies of 201 left,
     * and two of 200 came in. */
    static const uint16_t first[20] = {12, 12, 13, 20, 101, 103, 110, 110, 201, 201, 201, 202, 202, 202, 203,
                                       206, 206, 207, 207, 208};
    static const uint16_t second[20] = {12, 12, 13, 20, 101, 103, 110, 110, 200, 200, 201, 202, 202, 202, 203,
                                        206, 206, 207, 207, 208};
    static const uint16_t shuffled[4] = {205, 3, 101, 3};
    static const uint16_t sorted[4] = {3, 3, 101, 205};
    static const uint16_t more[5] = {3, 3, 3, 101, 205};
    sts_deck before, after, added, removed;

    deck_from(&before, first, 20);
    deck_from(&after, second, 20);
    sts_deck_changes(&before, &after, &added, &removed);
    CHECK(added.count == 2 && added.ids[0] == 200 && added.ids[1] == 200);
    CHECK(removed.count == 2 && removed.ids[0] == 201 && removed.ids[1] == 201);

    /* The same cards in another order are no change. */
    deck_from(&before, shuffled, 4);
    deck_from(&after, sorted, 4);
    sts_deck_changes(&before, &after, &added, &removed);
    CHECK(added.count == 0 && removed.count == 0);

    /* A copy more, or a copy fewer, of a card already there. */
    deck_from(&after, more, 5);
    sts_deck_changes(&before, &after, &added, &removed);
    CHECK(added.count == 1 && added.ids[0] == 3 && removed.count == 0);
    sts_deck_changes(&after, &before, &added, &removed);
    CHECK(added.count == 0 && removed.count == 1 && removed.ids[0] == 3);

    /* Everything changed, or nothing was there before. */
    sts_deck_clear(&before);
    sts_deck_changes(&before, &after, &added, &removed);
    CHECK(added.count == 5 && removed.count == 0);
}
