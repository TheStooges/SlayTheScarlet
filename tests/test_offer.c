/* Tests: Offer generation, and applying a taken offer. */
#include "test_util.h"

void test_offer_basics(void)
{
    sts_rules rules = sts_rules_vanilla();
    sts_offer_settings offer_settings = add_settings();
    sts_offer offer[8], second_offers[8];
    sts_deck deck;
    sts_rng rng, rng2;
    int count, wau, j;

    /* Three distinct offers the character can actually use. */
    sts_deck_clear(&deck);
    sts_rng_seed(&rng, 1, 1);
    count = sts_offer_generate(&g_db, 0, &deck, &rules, &offer_settings, &rng, offer, 8);
    CHECK(count == 3);
    for (wau = 0; wau < count; wau++) {
        const sts_card *card = sts_card_db_find(&g_db, 0, offer[wau].id);
        CHECK(card != NULL && card->type == offer[wau].type && offer[wau].copies == 1);
        for (j = 0; j < wau; j++)
            CHECK(offer[wau].id != offer[j].id);
    }

    /* The same seed gives the same offers. */
    sts_rng_seed(&rng, 99, 3);
    sts_rng_seed(&rng2, 99, 3);
    CHECK(sts_offer_generate(&g_db, 7, &deck, &rules, &offer_settings, &rng, offer, 8) == 3);
    CHECK(sts_offer_generate(&g_db, 7, &deck, &rules, &offer_settings, &rng2, second_offers, 8) == 3);
    for (wau = 0; wau < 3; wau++)
        CHECK(offer[wau].id == second_offers[wau].id && offer[wau].copies == second_offers[wau].copies);

    /* The output buffer limit is respected. */
    sts_rng_seed(&rng, 1, 1);
    CHECK(sts_offer_generate(&g_db, 0, &deck, &rules, &offer_settings, &rng, offer, 2) == 2);

    /* offer_count 0 is allowed, invalid settings are not. */
    offer_settings.offer_count = 0;
    CHECK(sts_offer_generate(&g_db, 0, &deck, &rules, &offer_settings, &rng, offer, 8) == 0);
    offer_settings = add_settings();
    CHECK(sts_offer_generate(&g_db, -1, &deck, &rules, &offer_settings, &rng, offer, 8) == -1);
    CHECK(sts_offer_generate(&g_db, STS_NUM_CHARACTERS, &deck, &rules, &offer_settings, &rng, offer, 8) == -1);
    CHECK(sts_offer_generate(&g_db, 0, &deck, &rules, &offer_settings, &rng, offer, -1) == -1);
    offer_settings.copies_min = 0;
    CHECK(sts_offer_generate(&g_db, 0, &deck, &rules, &offer_settings, &rng, offer, 8) == -1);
    offer_settings.copies_min = 3; offer_settings.copies_max = 2;
    CHECK(sts_offer_generate(&g_db, 0, &deck, &rules, &offer_settings, &rng, offer, 8) == -1);
    offer_settings = add_settings();
    offer_settings.offer_count = -1;
    CHECK(sts_offer_generate(&g_db, 0, &deck, &rules, &offer_settings, &rng, offer, 8) == -1);

    /* SKIP never offers anything. */
    offer_settings = add_settings();
    offer_settings.mode = STS_REWARD_SKIP;
    CHECK(sts_offer_generate(&g_db, 0, &deck, &rules, &offer_settings, &rng, offer, 8) == 0);

    /* The type mask limits offers, across enough rolls to catch a leak. */
    offer_settings = add_settings();
    offer_settings.type_mask = STS_TYPE_BIT(STS_CARD_SPELL);
    for (j = 0; j < 200; j++) {
        count = sts_offer_generate(&g_db, 0, &deck, &rules, &offer_settings, &rng, offer, 8);
        CHECK_EACH(count == 3);
        for (wau = 0; wau < count; wau++)
            CHECK_EACH(offer[wau].type == STS_CARD_SPELL);
    }
    CHECK_EACH_DONE();
    offer_settings.type_mask = 0;
    CHECK(sts_offer_generate(&g_db, 0, &deck, &rules, &offer_settings, &rng, offer, 8) == 0);

    /* Fewer eligible cards than requested: return what exists rather than failing.
     * Reimu has 10 spells; with 8 of them at the limit only 2 can still be offered. */
    {
        const sts_card_list *list = &g_db.characters[0];
        int spells = 0;
        sts_rules open = open_rules();
        sts_deck_clear(&deck);
        for (wau = 0; wau < list->count && spells < 8; wau++) {
            if (list->cards[wau].type == STS_CARD_SPELL) {
                add_copies(&deck, list->cards[wau].id, open.copy_limit);
                spells++;
            }
        }
        offer_settings = add_settings();
        offer_settings.type_mask = STS_TYPE_BIT(STS_CARD_SPELL);
        CHECK(sts_offer_generate(&g_db, 0, &deck, &open, &offer_settings, &rng, offer, 8) == 2);
        for (wau = 0; wau < 2; wau++)
            CHECK(sts_deck_count_of(&deck, offer[wau].id) == 0);
    }
}

/* The core rule: limits are applied before the roll. */
void test_offer_limits(void)
{
    sts_rules rules = open_rules();
    sts_offer_settings offer_settings = add_settings();
    sts_offer offer[8];
    sts_deck deck;
    sts_rng rng;
    int wau, j, count;

    sts_rng_seed(&rng, 5, 5);

    /* A card at the copy limit is never offered. A full deck offers nothing. */
    sts_deck_clear(&deck);
    add_copies(&deck, 100, rules.copy_limit);
    for (j = 0; j < 500; j++) {
        count = sts_offer_generate(&g_db, 0, &deck, &rules, &offer_settings, &rng, offer, 8);
        for (wau = 0; wau < count; wau++)
            CHECK_EACH(offer[wau].id != 100);
    }
    CHECK_EACH_DONE();
    rules.deck_max = deck.count;
    CHECK(sts_offer_generate(&g_db, 0, &deck, &rules, &offer_settings, &rng, offer, 8) == 0);

    /* Copies shrink to fit: only 1 slot is left in the deck, though 4 were allowed. */
    rules = open_rules();
    rules.deck_max = 21;
    sts_deck_clear(&deck);
    for (wau = 0; wau < 20; wau++) sts_deck_add(&deck, 0);    /* deliberately over the copy limit */
    offer_settings.copies_min = 1;
    offer_settings.copies_max = 4;
    for (j = 0; j < 200; j++) {
        count = sts_offer_generate(&g_db, 0, &deck, &rules, &offer_settings, &rng, offer, 8);
        CHECK_EACH(count == 3);
        for (wau = 0; wau < count; wau++) {
            CHECK_EACH(offer[wau].copies == 1);
            CHECK_EACH(offer[wau].id != 0);
        }
    }
    CHECK_EACH_DONE();

    /* copies_min that no longer fits filters every card out; it is not clamped below min. */
    offer_settings.copies_min = 2;
    CHECK(sts_offer_generate(&g_db, 0, &deck, &rules, &offer_settings, &rng, offer, 8) == 0);

    /* With 3 of a card and a limit of 4, a 2-copy minimum excludes that card. */
    rules = open_rules();
    sts_deck_clear(&deck);
    add_copies(&deck, 100, 3);
    offer_settings.copies_min = 2;
    offer_settings.copies_max = 2;
    for (j = 0; j < 500; j++) {
        count = sts_offer_generate(&g_db, 0, &deck, &rules, &offer_settings, &rng, offer, 8);
        for (wau = 0; wau < count; wau++) {
            CHECK_EACH(offer[wau].id != 100);
            CHECK_EACH(offer[wau].copies == 2);
        }
    }
    CHECK_EACH_DONE();

    /* Random copy counts reach every value in the range (2x A / 4x B / 1x C). */
    {
        int seen[5] = {0, 0, 0, 0, 0};
        sts_deck_clear(&deck);
        offer_settings.copies_min = 1;
        offer_settings.copies_max = 4;
        for (j = 0; j < 200; j++) {
            count = sts_offer_generate(&g_db, 0, &deck, &rules, &offer_settings, &rng, offer, 8);
            for (wau = 0; wau < count; wau++) {
                CHECK_EACH(offer[wau].copies >= 1 && offer[wau].copies <= 4);
                if (offer[wau].copies >= 1 && offer[wau].copies <= 4) seen[offer[wau].copies] = 1;
            }
        }
        CHECK_EACH_DONE();
        CHECK(seen[1] && seen[2] && seen[3] && seen[4]);
    }

    /* REPLACE: never more copies than the deck can give up; an empty deck gets nothing. */
    offer_settings = add_settings();
    offer_settings.mode = STS_REWARD_REPLACE;
    offer_settings.copies_max = 4;
    sts_deck_clear(&deck);
    CHECK(sts_offer_generate(&g_db, 0, &deck, &rules, &offer_settings, &rng, offer, 8) == 0);
    sts_deck_add(&deck, 100);
    sts_deck_add(&deck, 101);
    for (j = 0; j < 200; j++) {
        count = sts_offer_generate(&g_db, 0, &deck, &rules, &offer_settings, &rng, offer, 8);
        CHECK_EACH(count == 3);
        for (wau = 0; wau < count; wau++)
            CHECK_EACH(offer[wau].copies <= 2);
    }
    CHECK_EACH_DONE();

    /* REMOVE: only cards in the deck, and never below deck_min. */
    offer_settings = add_settings();
    offer_settings.mode = STS_REWARD_REMOVE;
    offer_settings.copies_max = 4;
    rules = open_rules();
    rules.deck_min = 3;
    sts_deck_clear(&deck);
    add_copies(&deck, 100, 2);
    add_copies(&deck, 200, 2);
    for (j = 0; j < 200; j++) {
        count = sts_offer_generate(&g_db, 0, &deck, &rules, &offer_settings, &rng, offer, 8);
        CHECK_EACH(count == 2);
        for (wau = 0; wau < count; wau++) {
            CHECK_EACH(offer[wau].id == 100 || offer[wau].id == 200);
            CHECK_EACH(offer[wau].copies == 1);     /* 4 cards, deck_min 3: only 1 may go */
        }
    }
    CHECK_EACH_DONE();
    rules.deck_min = 4;
    CHECK(sts_offer_generate(&g_db, 0, &deck, &rules, &offer_settings, &rng, offer, 8) == 0);
}

/* Every eligible card turns up, at a roughly fair rate. */
void test_offer_fairness(void)
{
    enum { ROLLS = 20000 };
    sts_rules rules = open_rules();
    sts_offer_settings offer_settings = add_settings();
    sts_offer offer[8];
    sts_deck deck;
    sts_rng rng;
    int hits[300];
    int type_hits[3];
    int type_cards[3];
    int character = 12;                  /* Komachi: 21 system, 12 skill, 9 spell */
    int expected, wau, j, count, type;
    const sts_card_list *list = &g_db.characters[character];

    memset(hits, 0, sizeof hits);
    memset(type_hits, 0, sizeof type_hits);
    sts_deck_clear(&deck);
    sts_rng_seed(&rng, 2024, 11);
    for (j = 0; j < ROLLS; j++) {
        count = sts_offer_generate(&g_db, character, &deck, &rules, &offer_settings, &rng, offer, 8);
        for (wau = 0; wau < count; wau++) {
            hits[offer[wau].id]++;
            type_hits[offer[wau].type]++;
        }
    }

    /* Equal weights: each type gets a third of the offers, however many cards it has. */
    for (type = 0; type < 3; type++)
        CHECK(type_hits[type] > ROLLS * 9 / 10 && type_hits[type] < ROLLS * 11 / 10);

    /* Within a type, every card is equally likely. */
    type_cards[STS_CARD_SYSTEM] = g_db.system.count;
    type_cards[STS_CARD_SKILL] = 12;
    type_cards[STS_CARD_SPELL] = 9;
    for (wau = 0; wau < g_db.system.count; wau++) {
        int card_hits = hits[g_db.system.cards[wau].id];
        expected = type_hits[STS_CARD_SYSTEM] / type_cards[STS_CARD_SYSTEM];
        CHECK(card_hits > expected * 8 / 10 && card_hits < expected * 12 / 10);
    }
    for (wau = 0; wau < list->count; wau++) {
        int card_hits = hits[list->cards[wau].id];
        expected = type_hits[list->cards[wau].type] / type_cards[list->cards[wau].type];
        CHECK(card_hits > expected * 8 / 10 && card_hits < expected * 12 / 10);
    }

    /* Weights shift the shares: 2/1/1 gives system cards half of them. */
    offer_settings.weights[STS_CARD_SYSTEM] = 2;
    memset(type_hits, 0, sizeof type_hits);
    for (j = 0; j < ROLLS; j++) {
        count = sts_offer_generate(&g_db, character, &deck, &rules, &offer_settings, &rng, offer, 8);
        for (wau = 0; wau < count; wau++)
            type_hits[offer[wau].type]++;
    }
    CHECK(type_hits[STS_CARD_SYSTEM] > ROLLS * 3 / 2 * 9 / 10 && type_hits[STS_CARD_SYSTEM] < ROLLS * 3 / 2 * 11 / 10);
    CHECK(type_hits[STS_CARD_SKILL] > ROLLS * 3 / 4 * 9 / 10 && type_hits[STS_CARD_SKILL] < ROLLS * 3 / 4 * 11 / 10);

    /* By card, every card is as likely as any other, so each type comes up in proportion to its cards:
     * for Komachi 21 system, 12 skill and 9 spell cards out of 42. The first offer of each set is the
     * only one with the full pool to choose from, so only it is counted. */
    offer_settings.weights[STS_CARD_SYSTEM] = 1;
    offer_settings.style = STS_OFFER_BY_CARD;
    memset(type_hits, 0, sizeof type_hits);
    for (j = 0; j < ROLLS; j++) {
        count = sts_offer_generate(&g_db, character, &deck, &rules, &offer_settings, &rng, offer, 8);
        CHECK_EACH(count == 3);
        type_hits[offer[0].type]++;
    }
    CHECK_EACH_DONE();
    CHECK(type_hits[STS_CARD_SYSTEM] > ROLLS * 21 / 42 * 9 / 10 && type_hits[STS_CARD_SYSTEM] < ROLLS * 21 / 42 * 11 / 10);
    CHECK(type_hits[STS_CARD_SKILL] > ROLLS * 12 / 42 * 9 / 10 && type_hits[STS_CARD_SKILL] < ROLLS * 12 / 42 * 11 / 10);
    CHECK(type_hits[STS_CARD_SPELL] > ROLLS * 9 / 42 * 9 / 10 && type_hits[STS_CARD_SPELL] < ROLLS * 9 / 42 * 11 / 10);
    offer_settings.style = STS_OFFER_WEIGHTED;
    offer_settings.weights[STS_CARD_SYSTEM] = 2;

    /* A weight of 0 turns a type off, and a type with no cards left hands over to the others. */
    offer_settings.weights[STS_CARD_SYSTEM] = 0;
    for (j = 0; j < 500; j++) {
        count = sts_offer_generate(&g_db, character, &deck, &rules, &offer_settings, &rng, offer, 8);
        CHECK_EACH(count == 3);
        for (wau = 0; wau < count; wau++)
            CHECK_EACH(offer[wau].type != STS_CARD_SYSTEM);
    }
    CHECK_EACH_DONE();
    offer_settings.weights[STS_CARD_SKILL] = 0;
    offer_settings.weights[STS_CARD_SPELL] = 0;
    CHECK(sts_offer_generate(&g_db, character, &deck, &rules, &offer_settings, &rng, offer, 8) == 0);
    offer_settings.weights[STS_CARD_SKILL] = 1;
    offer_settings.type_mask = STS_TYPE_BIT(STS_CARD_SPELL) | STS_TYPE_BIT(STS_CARD_SYSTEM);
    CHECK(sts_offer_generate(&g_db, character, &deck, &rules, &offer_settings, &rng, offer, 8) == 0);
}

static sts_offer make_offer(uint16_t id, int copies);

/* Swapping a card for itself would waste the reward, in every replacement mode. */
void test_offer_no_self_replace(void)
{
    sts_rules rules = open_rules();
    sts_offer_settings offer_settings = add_settings();
    sts_offer out[8], offer;
    sts_deck deck, before;
    sts_rng rng;
    uint16_t ids[2];
    int wau, j, count;

    sts_rng_seed(&rng, 13, 13);
    sts_deck_clear(&deck);
    add_copies(&deck, 100, 2);
    sts_deck_add(&deck, 101);
    sts_deck_sort(&deck);
    before = deck;
    offer = make_offer(100, 1);

    /* Chosen: naming a copy of the incoming card is refused, and changes nothing. */
    ids[0] = 100;
    CHECK(sts_offer_replace_chosen(&deck, &offer, &rules, ids, 1) == STS_APPLY_BAD_REMOVAL);
    CHECK(decks_equal(&deck, &before));
    ids[0] = 101;
    CHECK(sts_offer_replace_chosen(&deck, &offer, &rules, ids, 1) == STS_APPLY_OK);
    CHECK(sts_deck_count_of(&deck, 100) == 3 && sts_deck_count_of(&deck, 101) == 0);

    /* Random: only the other card can ever leave, on every roll. */
    for (j = 0; j < 100; j++) {
        deck = before;
        CHECK_EACH(sts_offer_replace_random(&deck, &offer, &rules, &rng) == STS_APPLY_OK);
        CHECK_EACH(sts_deck_count_of(&deck, 100) == 3 && sts_deck_count_of(&deck, 101) == 0);
    }
    CHECK_EACH_DONE();
    deck = before;
    offer.copies = 2;      /* only one other card exists to give up */
    CHECK(sts_offer_replace_random(&deck, &offer, &rules, &rng) == STS_APPLY_BAD_REMOVAL);
    CHECK(decks_equal(&deck, &before));

    /* By type: the same, within the type. */
    offer.copies = 1;
    for (j = 0; j < 100; j++) {
        deck = before;
        CHECK_EACH(sts_offer_replace_by_type(&deck, &offer, &rules, &g_db, 0, STS_CARD_SKILL, &rng) == STS_APPLY_OK);
        CHECK_EACH(sts_deck_count_of(&deck, 100) == 3 && sts_deck_count_of(&deck, 101) == 0);
    }
    CHECK_EACH_DONE();
    deck = before;
    offer.copies = 2;
    CHECK(sts_offer_replace_by_type(&deck, &offer, &rules, &g_db, 0, STS_CARD_SKILL, &rng) == STS_APPLY_BAD_REMOVAL);
    CHECK(decks_equal(&deck, &before));

    /* What a by-type screen shows leaves those copies out. */
    CHECK(sts_replace_count_of_type(&g_db, 0, &before, 100, STS_CARD_SKILL) == 1);
    CHECK(sts_replace_count_of_type(&g_db, 0, &before, 101, STS_CARD_SKILL) == 2);
    CHECK(sts_replace_count_of_type(&g_db, 0, &before, 200, STS_CARD_SKILL) == 3);
    CHECK(sts_replace_types_available(&g_db, 0, &before, 100, 1) == STS_TYPE_BIT(STS_CARD_SKILL));
    CHECK(sts_replace_types_available(&g_db, 0, &before, 100, 2) == 0);
    CHECK(sts_replace_types_available(&g_db, 0, &before, 101, 2) == STS_TYPE_BIT(STS_CARD_SKILL));

    /* The generator never offers a swap that cannot be completed. */
    offer_settings.mode = STS_REWARD_REPLACE;
    offer_settings.replace = STS_REPLACE_CHOOSE;
    offer_settings.type_mask = STS_TYPE_BIT(STS_CARD_SKILL);
    sts_deck_clear(&deck);
    add_copies(&deck, 100, 2);
    for (j = 0; j < 500; j++) {
        count = sts_offer_generate(&g_db, 0, &deck, &rules, &offer_settings, &rng, out, 8);
        CHECK_EACH(count == 3);
        for (wau = 0; wau < count; wau++)
            CHECK_EACH(out[wau].id != 100);
    }
    CHECK_EACH_DONE();

    /* Copies shrink to what can really leave: 100 has only one other card to swap for, and
     * under by-type no card of another type. */
    offer_settings.copies_max = 4;
    offer_settings.replace = STS_REPLACE_RANDOM;
    sts_deck_add(&deck, 101);
    offer_settings.type_mask = STS_TYPE_ALL;
    for (j = 0; j < 500; j++) {
        count = sts_offer_generate(&g_db, 0, &deck, &rules, &offer_settings, &rng, out, 8);
        for (wau = 0; wau < count; wau++) {
            if (out[wau].id == 100)
                CHECK_EACH(out[wau].copies == 1);
            else if (out[wau].id == 101)
                CHECK_EACH(out[wau].copies <= 2);
            else
                CHECK_EACH(out[wau].copies <= 3);
        }
    }
    CHECK_EACH_DONE();

    offer_settings.replace = STS_REPLACE_BY_TYPE;
    offer_settings.copies_min = 2;
    offer_settings.copies_max = 2;
    for (j = 0; j < 500; j++) {
        count = sts_offer_generate(&g_db, 0, &deck, &rules, &offer_settings, &rng, out, 8);
        for (wau = 0; wau < count; wau++)
            CHECK_EACH(out[wau].id != 100);
    }
    CHECK_EACH_DONE();

    /* A saved offer is judged by the same rule. */
    offer_settings = add_settings();
    offer_settings.mode = STS_REWARD_REPLACE;
    sts_deck_clear(&deck);
    add_copies(&deck, 100, 2);
    offer = make_offer(100, 1);
    CHECK(sts_offer_is_valid(&g_db, 0, &deck, &rules, &offer_settings, &offer) == 0);
    sts_deck_add(&deck, 101);
    CHECK(sts_offer_is_valid(&g_db, 0, &deck, &rules, &offer_settings, &offer) == 1);
}

void test_offer_one_of_each(void)
{
    sts_rules rules = open_rules();
    sts_offer_settings offer_settings = add_settings();
    sts_offer offer[8];
    sts_deck deck;
    sts_rng rng;
    int character, wau, j, count;

    offer_settings.style = STS_OFFER_ONE_OF_EACH;
    /* The weights play no part in this style. */
    offer_settings.weights[STS_CARD_SYSTEM] = 0;
    offer_settings.weights[STS_CARD_SKILL] = 0;
    offer_settings.weights[STS_CARD_SPELL] = 0;
    sts_deck_clear(&deck);
    sts_rng_seed(&rng, 8, 8);

    /* Three offers are one of each, in a fixed order, for everyone. */
    for (character = 0; character < STS_NUM_CHARACTERS; character++) {
        for (j = 0; j < 50; j++) {
            count = sts_offer_generate(&g_db, character, &deck, &rules, &offer_settings, &rng, offer, 8);
            CHECK_EACH(count == 3);
            CHECK_EACH(offer[0].type == STS_CARD_SYSTEM && offer[1].type == STS_CARD_SKILL
                       && offer[2].type == STS_CARD_SPELL);
        }
    }
    CHECK_EACH_DONE();

    /* More offers repeat the cycle, never repeating a card. */
    offer_settings.offer_count = 6;
    for (j = 0; j < 100; j++) {
        int first, second;
        count = sts_offer_generate(&g_db, 0, &deck, &rules, &offer_settings, &rng, offer, 8);
        CHECK_EACH(count == 6);
        for (wau = 0; wau < 6; wau++)
            CHECK_EACH(offer[wau].type == wau % 3);
        for (first = 0; first < count; first++)
            for (second = 0; second < first; second++)
                CHECK_EACH(offer[first].id != offer[second].id);
    }
    CHECK_EACH_DONE();

    /* Fewer offers take the first types in the order. */
    offer_settings.offer_count = 2;
    count = sts_offer_generate(&g_db, 0, &deck, &rules, &offer_settings, &rng, offer, 8);
    CHECK(count == 2 && offer[0].type == STS_CARD_SYSTEM && offer[1].type == STS_CARD_SKILL);

    /* A type that cannot be offered hands its place to the next. */
    offer_settings.offer_count = 3;
    offer_settings.type_mask = STS_TYPE_BIT(STS_CARD_SKILL) | STS_TYPE_BIT(STS_CARD_SPELL);
    count = sts_offer_generate(&g_db, 0, &deck, &rules, &offer_settings, &rng, offer, 8);
    CHECK(count == 3);
    for (wau = 0; wau < count; wau++)
        CHECK(offer[wau].type != STS_CARD_SYSTEM);

    /* So does one with nothing eligible: Reimu's 10 spells, all at the copy limit, and a
     * size limit with room left for other cards. */
    rules.deck_max = 60;
    offer_settings.type_mask = STS_TYPE_ALL;
    sts_deck_clear(&deck);
    for (wau = 0; wau < g_db.characters[0].count; wau++)
        if (g_db.characters[0].cards[wau].type == STS_CARD_SPELL)
            add_copies(&deck, g_db.characters[0].cards[wau].id, rules.copy_limit);
    count = sts_offer_generate(&g_db, 0, &deck, &rules, &offer_settings, &rng, offer, 8);
    CHECK(count == 3);
    CHECK(offer[0].type == STS_CARD_SYSTEM && offer[1].type == STS_CARD_SKILL && offer[2].type == STS_CARD_SYSTEM);
}

static void apply_offer(sts_deck *deck, const sts_offer *offer, sts_reward_mode mode)
{
    int wau, j;

    if (mode == STS_REWARD_REMOVE || mode == STS_REWARD_REPLACE) {
        /* REMOVE takes the offered card; REPLACE stands in for a player's choice by
         * dropping from the front of the deck. */
        for (wau = 0; wau < offer->copies; wau++) {
            int position = 0;
            if (mode == STS_REWARD_REMOVE)
                for (position = 0; position < deck->count && deck->ids[position] != offer->id; position++) {}
            for (j = position; j + 1 < deck->count; j++)
                deck->ids[j] = deck->ids[j + 1];
            deck->count--;
        }
    }
    if (mode == STS_REWARD_ADD || mode == STS_REWARD_REPLACE)
        for (wau = 0; wau < offer->copies; wau++)
            sts_deck_add(deck, offer->id);
}

/* Whole runs: a 20-pick draft followed by rewards, for every character and mode,
 * must stay legal at every step without ever needing a retry. */
void test_offer_runs(void)
{
    static const sts_reward_mode modes[3] = { STS_REWARD_ADD, STS_REWARD_REPLACE, STS_REWARD_REMOVE };
    sts_rules vanilla = sts_rules_vanilla();
    sts_rules bounds;
    sts_offer_settings draft = add_settings(), offer_settings;
    sts_offer offer[8];
    sts_deck deck;
    sts_rng rng;
    int character, mode_index, pick, seed, count, violations = 0, short_draft = 0;

    bounds.deck_min = 15;
    bounds.deck_max = 30;
    bounds.copy_limit = vanilla.copy_limit;

    for (seed = 0; seed < 50; seed++) {
        for (character = 0; character < STS_NUM_CHARACTERS; character++) {
            sts_rng_seed(&rng, (uint64_t)seed, (uint64_t)character);
            sts_deck_clear(&deck);
            for (pick = 0; pick < 20; pick++) {
                count = sts_offer_generate(&g_db, character, &deck, &vanilla, &draft, &rng, offer, 8);
                if (count != 3)
                    short_draft++;
                if (count > 0)
                    apply_offer(&deck, &offer[sts_rng_below(&rng, (uint32_t)count)], STS_REWARD_ADD);
            }
            if (sts_deck_validate(&deck, &vanilla, NULL) != STS_DECK_OK
                || !sts_deck_cards_legal(&g_db, character, &deck, NULL))
                violations++;

            for (mode_index = 0; mode_index < 3; mode_index++) {
                offer_settings = add_settings();
                offer_settings.mode = modes[mode_index];
                offer_settings.copies_max = 4;
                for (pick = 0; pick < 40; pick++) {
                    count = sts_offer_generate(&g_db, character, &deck, &bounds, &offer_settings, &rng, offer, 8);
                    if (count > 0)
                        apply_offer(&deck, &offer[sts_rng_below(&rng, (uint32_t)count)], modes[mode_index]);
                    if (sts_deck_validate(&deck, &bounds, NULL) != STS_DECK_OK)
                        violations++;
                }
            }
        }
    }
    CHECK(short_draft == 0);
    CHECK(violations == 0);
}

static sts_offer make_offer(uint16_t id, int copies)
{
    sts_offer offer;
    offer.id = id;
    offer.type = STS_CARD_SKILL;
    offer.copies = copies;
    return offer;
}

void test_offer_apply(void)
{
    sts_rules rules = open_rules();
    sts_rules fixed = sts_rules_vanilla();
    sts_offer offer;
    sts_deck deck, before;
    sts_rng rng;
    uint16_t ids[4];
    int wau, j;

    sts_rng_seed(&rng, 3, 3);

    /* ADD: allowed below deck_min, sorted afterwards, atomic when refused. */
    fixed.deck_min = 20;
    sts_deck_clear(&deck);
    sts_deck_add(&deck, 200);
    offer = make_offer(100, 2);
    CHECK(sts_offer_apply_add(&deck, &offer, &fixed) == STS_APPLY_OK);
    CHECK(deck.count == 3 && deck.ids[0] == 100 && deck.ids[1] == 100 && deck.ids[2] == 200);
    add_copies(&deck, 100, 2);
    before = deck;
    offer = make_offer(100, 1);    /* a 5th copy */
    CHECK(sts_offer_apply_add(&deck, &offer, &fixed) == STS_APPLY_ILLEGAL);
    CHECK(decks_equal(&deck, &before));
    offer = make_offer(101, 0);
    CHECK(sts_offer_apply_add(&deck, &offer, &fixed) == STS_APPLY_ILLEGAL);
    rules.deck_max = deck.count;
    offer = make_offer(101, 1);    /* over deck_max */
    CHECK(sts_offer_apply_add(&deck, &offer, &rules) == STS_APPLY_ILLEGAL);
    CHECK(decks_equal(&deck, &before));

    /* REMOVE: takes only cards that exist, and respects deck_min. */
    rules = open_rules();
    sts_deck_clear(&deck);
    add_copies(&deck, 100, 2);
    add_copies(&deck, 101, 2);
    before = deck;
    offer = make_offer(200, 1);
    CHECK(sts_offer_apply_remove(&deck, &offer, &rules) == STS_APPLY_BAD_REMOVAL);
    offer = make_offer(100, 3);
    CHECK(sts_offer_apply_remove(&deck, &offer, &rules) == STS_APPLY_BAD_REMOVAL);
    CHECK(decks_equal(&deck, &before));
    rules.deck_min = 4;
    offer = make_offer(100, 1);
    CHECK(sts_offer_apply_remove(&deck, &offer, &rules) == STS_APPLY_ILLEGAL);
    CHECK(decks_equal(&deck, &before));
    rules.deck_min = 3;
    CHECK(sts_offer_apply_remove(&deck, &offer, &rules) == STS_APPLY_OK);
    CHECK(deck.count == 3 && sts_deck_count_of(&deck, 100) == 1);

    /* REPLACE chosen: exact count, cards must exist, duplicates need real copies. */
    rules = open_rules();
    sts_deck_clear(&deck);
    add_copies(&deck, 100, 2);
    add_copies(&deck, 101, 2);
    before = deck;
    offer = make_offer(200, 2);
    ids[0] = 100; ids[1] = 300;
    CHECK(sts_offer_replace_chosen(&deck, &offer, &rules, ids, 2) == STS_APPLY_BAD_REMOVAL);
    CHECK(sts_offer_replace_chosen(&deck, &offer, &rules, ids, 1) == STS_APPLY_BAD_REMOVAL);
    offer = make_offer(200, 3);
    ids[0] = 100; ids[1] = 100; ids[2] = 100;   /* only 2 copies exist */
    CHECK(sts_offer_replace_chosen(&deck, &offer, &rules, ids, 3) == STS_APPLY_BAD_REMOVAL);
    CHECK(decks_equal(&deck, &before));
    offer = make_offer(200, 2);
    ids[0] = 100; ids[1] = 100;
    CHECK(sts_offer_replace_chosen(&deck, &offer, &rules, ids, 2) == STS_APPLY_OK);
    CHECK(deck.count == 4 && sts_deck_count_of(&deck, 100) == 0 && sts_deck_count_of(&deck, 200) == 2);

    /* Replacing into the copy limit is refused and changes nothing. */
    sts_deck_clear(&deck);
    add_copies(&deck, 100, 4);
    add_copies(&deck, 101, 1);
    before = deck;
    offer = make_offer(100, 1);
    ids[0] = 101;
    CHECK(sts_offer_replace_chosen(&deck, &offer, &rules, ids, 1) == STS_APPLY_ILLEGAL);
    CHECK(decks_equal(&deck, &before));

    /* REPLACE by type removes only that type, whichever copies the roll lands on. */
    sts_deck_clear(&deck);
    add_copies(&deck, 0, 3);       /* system */
    add_copies(&deck, 100, 2);     /* skill */
    add_copies(&deck, 200, 1);     /* spell */
    before = deck;
    offer = make_offer(1, 2);
    CHECK(sts_offer_replace_by_type(&deck, &offer, &rules, &g_db, 0, STS_CARD_SPELL, &rng) == STS_APPLY_BAD_REMOVAL);
    CHECK(decks_equal(&deck, &before));
    CHECK(sts_offer_replace_by_type(&deck, &offer, &rules, &g_db, 0, 99, &rng) == STS_APPLY_BAD_REMOVAL);
    CHECK(sts_offer_replace_by_type(&deck, &offer, &rules, &g_db, 0, STS_CARD_SKILL, &rng) == STS_APPLY_OK);
    CHECK(deck.count == 6 && sts_deck_count_of(&deck, 100) == 0 && sts_deck_count_of(&deck, 1) == 2);
    CHECK(sts_deck_count_of(&deck, 0) == 3 && sts_deck_count_of(&deck, 200) == 1);

    /* The types a BY_TYPE screen can offer. */
    CHECK(sts_replace_types_available(&g_db, 0, &before, 1, 1) == STS_TYPE_ALL);
    CHECK(sts_replace_types_available(&g_db, 0, &before, 1, 2)
          == (STS_TYPE_BIT(STS_CARD_SYSTEM) | STS_TYPE_BIT(STS_CARD_SKILL)));
    CHECK(sts_replace_types_available(&g_db, 0, &before, 1, 4) == 0);

    /* REPLACE random can land on any card, and never needs more than the deck has. */
    {
        int removed_a = 0, removed_b = 0;
        for (wau = 0; wau < 200; wau++) {
            sts_deck_clear(&deck);
            sts_deck_add(&deck, 100);
            sts_deck_add(&deck, 101);
            offer = make_offer(200, 1);
            CHECK_EACH(sts_offer_replace_random(&deck, &offer, &rules, &rng) == STS_APPLY_OK);
            CHECK_EACH(deck.count == 2 && sts_deck_count_of(&deck, 200) == 1);
            if (!sts_deck_count_of(&deck, 100)) removed_a++;
            if (!sts_deck_count_of(&deck, 101)) removed_b++;
        }
        CHECK_EACH_DONE();
        CHECK(removed_a > 50 && removed_b > 50 && removed_a + removed_b == 200);
        offer = make_offer(200, 3);
        CHECK(sts_offer_replace_random(&deck, &offer, &rules, &rng) == STS_APPLY_BAD_REMOVAL);
    }

    /* BY_TYPE offers fit what one type can give up: every type below 3 means no 3-copy
     * offers, though the deck as a whole has 6 cards. */
    {
        sts_offer_settings offer_settings = add_settings();
        sts_offer out[8];
        offer_settings.mode = STS_REWARD_REPLACE;
        offer_settings.replace = STS_REPLACE_BY_TYPE;
        offer_settings.copies_min = 3;
        offer_settings.copies_max = 3;
        sts_deck_clear(&deck);
        add_copies(&deck, 0, 2);
        add_copies(&deck, 100, 2);
        add_copies(&deck, 200, 2);
        CHECK(sts_offer_generate(&g_db, 0, &deck, &rules, &offer_settings, &rng, out, 8) == 0);
        offer_settings.replace = STS_REPLACE_RANDOM;
        CHECK(sts_offer_generate(&g_db, 0, &deck, &rules, &offer_settings, &rng, out, 8) == 3);
        /* Clamped copies stay within the largest type too. */
        offer_settings.replace = STS_REPLACE_BY_TYPE;
        offer_settings.copies_min = 1;
        offer_settings.copies_max = 4;
        for (j = 0; j < 100; j++) {
            int count = sts_offer_generate(&g_db, 0, &deck, &rules, &offer_settings, &rng, out, 8);
            CHECK_EACH(count == 3);
            for (wau = 0; wau < count; wau++)
                CHECK_EACH(out[wau].copies <= 2);
        }
        CHECK_EACH_DONE();
    }
}

/* The tier a card counts in for offers, for this character. */
static int tier_of(int character, uint16_t id)
{
    return sts_card_tier(sts_card_db_find(&g_db, character, id));
}

void test_offer_rarity(void)
{
    enum { ROLLS = 20000 };
    sts_rules rules = open_rules();
    sts_offer_settings offer_settings = add_settings();
    sts_offer offer[8];
    sts_deck deck;
    sts_rng rng;
    int character = 12;    /* Komachi */
    int tier_picks[STS_TIER_COUNT];
    int tier, wau, j, count;

    sts_deck_clear(&deck);
    sts_rng_seed(&rng, 77, 5);
    offer_settings.rarity = 1;

    /* One tier on its own: every offer is of it. */
    for (tier = 0; tier < STS_TIER_COUNT; tier++) {
        offer_settings.rarity_weights[0] = offer_settings.rarity_weights[1] = offer_settings.rarity_weights[2] = 0;
        offer_settings.rarity_weights[tier] = 1;
        for (j = 0; j < 300; j++) {
            count = sts_offer_generate(&g_db, character, &deck, &rules, &offer_settings, &rng, offer, 8);
            CHECK_EACH(count == 3);
            for (wau = 0; wau < count; wau++)
                CHECK_EACH(tier_of(character, offer[wau].id) == tier);
        }
        CHECK_EACH_DONE();
    }

    /* Every offer of a pick shares its tier, and the weights give each tier's share of the picks. */
    offer_settings.rarity_weights[STS_RARITY_COMMON] = 60;
    offer_settings.rarity_weights[STS_RARITY_UNCOMMON] = 30;
    offer_settings.rarity_weights[STS_RARITY_RARE] = 10;
    memset(tier_picks, 0, sizeof tier_picks);
    for (j = 0; j < ROLLS; j++) {
        count = sts_offer_generate(&g_db, character, &deck, &rules, &offer_settings, &rng, offer, 8);
        CHECK_EACH(count == 3);
        tier = tier_of(character, offer[0].id);
        for (wau = 1; wau < count; wau++)
            CHECK_EACH(tier_of(character, offer[wau].id) == tier);
        tier_picks[tier]++;
    }
    CHECK_EACH_DONE();
    CHECK(tier_picks[STS_RARITY_COMMON] > ROLLS * 6 / 10 * 95 / 100 && tier_picks[STS_RARITY_COMMON] < ROLLS * 6 / 10 * 105 / 100);
    CHECK(tier_picks[STS_RARITY_UNCOMMON] > ROLLS * 3 / 10 * 9 / 10 && tier_picks[STS_RARITY_UNCOMMON] < ROLLS * 3 / 10 * 11 / 10);
    CHECK(tier_picks[STS_RARITY_RARE] > ROLLS / 10 * 85 / 100 && tier_picks[STS_RARITY_RARE] < ROLLS / 10 * 115 / 100);

    /* A tier with weight 0 is never offered, and the generator would not have made an offer of it. */
    offer_settings.rarity_weights[STS_RARITY_RARE] = 0;
    for (j = 0; j < 500; j++) {
        count = sts_offer_generate(&g_db, character, &deck, &rules, &offer_settings, &rng, offer, 8);
        for (wau = 0; wau < count; wau++)
            CHECK_EACH(tier_of(character, offer[wau].id) != STS_RARITY_RARE);
    }
    CHECK_EACH_DONE();
    {
        sts_offer coin;

        coin.id = 12;   /* A Penny Saved, rare */
        coin.type = STS_CARD_SYSTEM;
        coin.copies = 1;
        CHECK(!sts_offer_is_valid(&g_db, character, &deck, &rules, &offer_settings, &coin));
        offer_settings.rarity = 0;
        CHECK(sts_offer_is_valid(&g_db, character, &deck, &rules, &offer_settings, &coin));
        offer_settings.rarity = 1;
        offer_settings.rarity_weights[STS_RARITY_RARE] = 10;
        CHECK(sts_offer_is_valid(&g_db, character, &deck, &rules, &offer_settings, &coin));
    }

    /* A tier that runs short hands over to the nearest one with a weight, the lower first: with all
     * but one rare card at the copy limit, a rare pick holds that card and then uncommon ones. */
    {
        const sts_card_list *lists[2];
        uint16_t last_rare = 0;
        int list_index, rare_picks = 0, found = 0;

        lists[0] = &g_db.system;
        lists[1] = &g_db.characters[character];
        for (list_index = 0; list_index < 2; list_index++) {
            for (wau = 0; wau < lists[list_index]->count; wau++) {
                const sts_card *card = &lists[list_index]->cards[wau];

                if (sts_card_tier(card) != STS_RARITY_RARE)
                    continue;
                if (found++)   /* the first is Spiritual Strike Talisman, card 0 */
                    add_copies(&deck, last_rare, rules.copy_limit);
                last_rare = card->id;
            }
        }
        offer_settings.rarity_weights[STS_RARITY_COMMON] = 1;
        offer_settings.rarity_weights[STS_RARITY_UNCOMMON] = 1;
        offer_settings.rarity_weights[STS_RARITY_RARE] = 1;
        for (j = 0; j < 600; j++) {
            count = sts_offer_generate(&g_db, character, &deck, &rules, &offer_settings, &rng, offer, 8);
            CHECK_EACH(count == 3);
            if (offer[0].id != last_rare)
                continue;
            rare_picks++;
            for (wau = 1; wau < count; wau++)
                CHECK_EACH(tier_of(character, offer[wau].id) == STS_RARITY_UNCOMMON);
        }
        CHECK_EACH_DONE();
        CHECK(rare_picks > 100);

        /* With uncommon off, rare falls back to common instead. */
        offer_settings.rarity_weights[STS_RARITY_UNCOMMON] = 0;
        for (j = 0; j < 600; j++) {
            count = sts_offer_generate(&g_db, character, &deck, &rules, &offer_settings, &rng, offer, 8);
            CHECK_EACH(count == 3);
            for (wau = 0; wau < count; wau++)
                CHECK_EACH(offer[wau].id == last_rare || tier_of(character, offer[wau].id) == STS_RARITY_COMMON);
        }
        CHECK_EACH_DONE();

        /* With only rare on, a pick holds what is left of it. */
        offer_settings.rarity_weights[STS_RARITY_COMMON] = 0;
        count = sts_offer_generate(&g_db, character, &deck, &rules, &offer_settings, &rng, offer, 8);
        CHECK(count == 1 && offer[0].id == last_rare);
        add_copies(&deck, last_rare, rules.copy_limit);
        CHECK(sts_offer_generate(&g_db, character, &deck, &rules, &offer_settings, &rng, offer, 8) == 0);
    }

    /* One of each tier: common, uncommon, rare in turn, whatever the weights. */
    sts_deck_clear(&deck);
    offer_settings.rarity = 1;
    offer_settings.one_tier_each = 1;
    offer_settings.rarity_weights[0] = offer_settings.rarity_weights[1] = offer_settings.rarity_weights[2] = 0;
    for (j = 0; j < 300; j++) {
        count = sts_offer_generate(&g_db, character, &deck, &rules, &offer_settings, &rng, offer, 8);
        CHECK_EACH(count == 3);
        for (wau = 0; wau < count; wau++)
            CHECK_EACH(tier_of(character, offer[wau].id) == wau);
    }
    CHECK_EACH_DONE();
    {
        sts_offer coin;

        coin.id = 12;   /* rare, offered though its weight is 0 */
        coin.type = STS_CARD_SYSTEM;
        coin.copies = 1;
        CHECK(sts_offer_is_valid(&g_db, character, &deck, &rules, &offer_settings, &coin));
    }
    offer_settings.one_tier_each = 0;

    /* With rarity off, the weights do nothing. */
    sts_deck_clear(&deck);
    offer_settings.rarity = 0;
    offer_settings.rarity_weights[0] = offer_settings.rarity_weights[1] = offer_settings.rarity_weights[2] = 0;
    CHECK(sts_offer_generate(&g_db, character, &deck, &rules, &offer_settings, &rng, offer, 8) == 3);
}

/* Counts how often each number of copies comes up, over many offers. */
static void count_copies(const sts_rules *rules, const sts_offer_settings *offer_settings, int rolls, int *counts)
{
    sts_offer offer[8];
    sts_deck deck;
    sts_rng rng;
    int wau, j, count;

    memset(counts, 0, sizeof(int) * 5);
    sts_deck_clear(&deck);
    sts_rng_seed(&rng, 31, 7);
    for (j = 0; j < rolls; j++) {
        count = sts_offer_generate(&g_db, 3, &deck, rules, offer_settings, &rng, offer, 8);
        for (wau = 0; wau < count; wau++)
            counts[offer[wau].copies]++;
    }
}

/* 1 if count is within 7% of total * share / parts. */
static int near_share(int count, int total, int share, int parts)
{
    long expected = (long)total * share / parts;
    return count > expected * 93 / 100 && count < expected * 107 / 100;
}

void test_offer_copy_odds(void)
{
    enum { ROLLS = 20000, OFFERS = ROLLS * 3 };
    sts_rules rules = open_rules();
    sts_offer_settings offer_settings = add_settings();
    int counts[5];

    offer_settings.copies_max = 4;

    /* Even: each number a quarter of the offers. */
    count_copies(&rules, &offer_settings, ROLLS, counts);
    CHECK(near_share(counts[1], OFFERS, 1, 4) && near_share(counts[4], OFFERS, 1, 4));

    /* Falling: 4:3:2:1. */
    offer_settings.copy_odds = STS_COPY_ODDS_FALLING;
    count_copies(&rules, &offer_settings, ROLLS, counts);
    CHECK(near_share(counts[1], OFFERS, 4, 10) && near_share(counts[2], OFFERS, 3, 10));
    CHECK(near_share(counts[3], OFFERS, 2, 10) && near_share(counts[4], OFFERS, 1, 10));

    /* Halving: 8:4:2:1. */
    offer_settings.copy_odds = STS_COPY_ODDS_HALVING;
    count_copies(&rules, &offer_settings, ROLLS, counts);
    CHECK(near_share(counts[1], OFFERS, 8, 15) && near_share(counts[2], OFFERS, 4, 15));
    CHECK(near_share(counts[3], OFFERS, 2, 15) && near_share(counts[4], OFFERS, 1, 15));

    /* A card that can take only some of the range keeps the proportions: at most 2 copies fit with a
     * copy limit of 2, so halving gives 1 or 2 copies 2:1, and falling 4:3. */
    rules.copy_limit = 2;
    count_copies(&rules, &offer_settings, ROLLS, counts);
    CHECK(counts[3] == 0 && counts[4] == 0);
    CHECK(near_share(counts[1], OFFERS, 2, 3) && near_share(counts[2], OFFERS, 1, 3));
    offer_settings.copy_odds = STS_COPY_ODDS_FALLING;
    count_copies(&rules, &offer_settings, ROLLS, counts);
    CHECK(near_share(counts[1], OFFERS, 4, 7) && near_share(counts[2], OFFERS, 3, 7));

    /* A fixed count stays fixed, and a range that starts above 1 is weighed from its start. */
    rules.copy_limit = 4;
    offer_settings.copy_odds = STS_COPY_ODDS_HALVING;
    offer_settings.copies_min = offer_settings.copies_max = 2;
    count_copies(&rules, &offer_settings, ROLLS, counts);
    CHECK(counts[2] == OFFERS);
    offer_settings.copies_max = 3;
    count_copies(&rules, &offer_settings, ROLLS, counts);
    CHECK(counts[1] == 0 && counts[4] == 0 && near_share(counts[2], OFFERS, 2, 3));
}
