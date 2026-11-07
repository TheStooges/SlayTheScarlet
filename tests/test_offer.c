/* Tests: Offer generation, and applying a taken offer. */
#include "test_util.h"

void test_offer_basics(void)
{
    sts_rules r = sts_rules_vanilla();
    sts_offer_settings s = add_settings();
    sts_offer o[8], o2[8];
    sts_deck d;
    sts_rng rng, rng2;
    int n, i, j;

    /* Three distinct offers the character can actually use. */
    sts_deck_clear(&d);
    sts_rng_seed(&rng, 1, 1);
    n = sts_offer_generate(&g_db, 0, &d, &r, &s, &rng, o, 8);
    CHECK(n == 3);
    for (i = 0; i < n; i++) {
        const sts_card *c = sts_card_db_find(&g_db, 0, o[i].id);
        CHECK(c != NULL && c->type == o[i].type && o[i].copies == 1);
        for (j = 0; j < i; j++)
            CHECK(o[i].id != o[j].id);
    }

    /* The same seed gives the same offers. */
    sts_rng_seed(&rng, 99, 3);
    sts_rng_seed(&rng2, 99, 3);
    CHECK(sts_offer_generate(&g_db, 7, &d, &r, &s, &rng, o, 8) == 3);
    CHECK(sts_offer_generate(&g_db, 7, &d, &r, &s, &rng2, o2, 8) == 3);
    for (i = 0; i < 3; i++)
        CHECK(o[i].id == o2[i].id && o[i].copies == o2[i].copies);

    /* The output buffer limit is respected. */
    sts_rng_seed(&rng, 1, 1);
    CHECK(sts_offer_generate(&g_db, 0, &d, &r, &s, &rng, o, 2) == 2);

    /* offer_count 0 is allowed, invalid settings are not. */
    s.offer_count = 0;
    CHECK(sts_offer_generate(&g_db, 0, &d, &r, &s, &rng, o, 8) == 0);
    s = add_settings();
    CHECK(sts_offer_generate(&g_db, -1, &d, &r, &s, &rng, o, 8) == -1);
    CHECK(sts_offer_generate(&g_db, STS_NUM_CHARACTERS, &d, &r, &s, &rng, o, 8) == -1);
    CHECK(sts_offer_generate(&g_db, 0, &d, &r, &s, &rng, o, -1) == -1);
    s.copies_min = 0;
    CHECK(sts_offer_generate(&g_db, 0, &d, &r, &s, &rng, o, 8) == -1);
    s.copies_min = 3; s.copies_max = 2;
    CHECK(sts_offer_generate(&g_db, 0, &d, &r, &s, &rng, o, 8) == -1);
    s = add_settings();
    s.offer_count = -1;
    CHECK(sts_offer_generate(&g_db, 0, &d, &r, &s, &rng, o, 8) == -1);

    /* SKIP never offers anything. */
    s = add_settings();
    s.mode = STS_REWARD_SKIP;
    CHECK(sts_offer_generate(&g_db, 0, &d, &r, &s, &rng, o, 8) == 0);

    /* The type mask limits offers, across enough rolls to catch a leak. */
    s = add_settings();
    s.type_mask = STS_TYPE_BIT(STS_CARD_SPELL);
    for (j = 0; j < 200; j++) {
        n = sts_offer_generate(&g_db, 0, &d, &r, &s, &rng, o, 8);
        CHECK(n == 3);
        for (i = 0; i < n; i++)
            CHECK(o[i].type == STS_CARD_SPELL);
    }
    s.type_mask = 0;
    CHECK(sts_offer_generate(&g_db, 0, &d, &r, &s, &rng, o, 8) == 0);

    /* Fewer eligible cards than requested: return what exists rather than failing.
     * Reimu has 10 spells; with 8 of them at the limit only 2 can still be offered. */
    {
        const sts_card_list *l = &g_db.characters[0];
        int spells = 0;
        sts_rules open = open_rules();
        sts_deck_clear(&d);
        for (i = 0; i < l->count && spells < 8; i++) {
            if (l->cards[i].type == STS_CARD_SPELL) {
                add_copies(&d, l->cards[i].id, open.copy_limit);
                spells++;
            }
        }
        s = add_settings();
        s.type_mask = STS_TYPE_BIT(STS_CARD_SPELL);
        CHECK(sts_offer_generate(&g_db, 0, &d, &open, &s, &rng, o, 8) == 2);
        for (i = 0; i < 2; i++)
            CHECK(sts_deck_count_of(&d, o[i].id) == 0);
    }
}

/* The core rule: limits are applied before the roll. */
void test_offer_limits(void)
{
    sts_rules r = open_rules();
    sts_offer_settings s = add_settings();
    sts_offer o[8];
    sts_deck d;
    sts_rng rng;
    int i, j, n;

    sts_rng_seed(&rng, 5, 5);

    /* A card at the copy limit is never offered. A full deck offers nothing. */
    sts_deck_clear(&d);
    add_copies(&d, 100, r.copy_limit);
    for (j = 0; j < 500; j++) {
        n = sts_offer_generate(&g_db, 0, &d, &r, &s, &rng, o, 8);
        for (i = 0; i < n; i++)
            CHECK(o[i].id != 100);
    }
    r.deck_max = d.count;
    CHECK(sts_offer_generate(&g_db, 0, &d, &r, &s, &rng, o, 8) == 0);

    /* Copies shrink to fit: only 1 slot is left in the deck, though 4 were allowed. */
    r = open_rules();
    r.deck_max = 21;
    sts_deck_clear(&d);
    for (i = 0; i < 20; i++) sts_deck_add(&d, 0);    /* deliberately over the copy limit */
    s.copies_min = 1;
    s.copies_max = 4;
    for (j = 0; j < 200; j++) {
        n = sts_offer_generate(&g_db, 0, &d, &r, &s, &rng, o, 8);
        CHECK(n == 3);
        for (i = 0; i < n; i++) {
            CHECK(o[i].copies == 1);
            CHECK(o[i].id != 0);
        }
    }

    /* copies_min that no longer fits filters every card out; it is not clamped below min. */
    s.copies_min = 2;
    CHECK(sts_offer_generate(&g_db, 0, &d, &r, &s, &rng, o, 8) == 0);

    /* With 3 of a card and a limit of 4, a 2-copy minimum excludes that card. */
    r = open_rules();
    sts_deck_clear(&d);
    add_copies(&d, 100, 3);
    s.copies_min = 2;
    s.copies_max = 2;
    for (j = 0; j < 500; j++) {
        n = sts_offer_generate(&g_db, 0, &d, &r, &s, &rng, o, 8);
        for (i = 0; i < n; i++) {
            CHECK(o[i].id != 100);
            CHECK(o[i].copies == 2);
        }
    }

    /* Random copy counts reach every value in the range (2x A / 4x B / 1x C). */
    {
        int seen[5] = {0, 0, 0, 0, 0};
        sts_deck_clear(&d);
        s.copies_min = 1;
        s.copies_max = 4;
        for (j = 0; j < 200; j++) {
            n = sts_offer_generate(&g_db, 0, &d, &r, &s, &rng, o, 8);
            for (i = 0; i < n; i++) {
                CHECK(o[i].copies >= 1 && o[i].copies <= 4);
                if (o[i].copies >= 1 && o[i].copies <= 4) seen[o[i].copies] = 1;
            }
        }
        CHECK(seen[1] && seen[2] && seen[3] && seen[4]);
    }

    /* REPLACE: never more copies than the deck can give up; an empty deck gets nothing. */
    s = add_settings();
    s.mode = STS_REWARD_REPLACE;
    s.copies_max = 4;
    sts_deck_clear(&d);
    CHECK(sts_offer_generate(&g_db, 0, &d, &r, &s, &rng, o, 8) == 0);
    sts_deck_add(&d, 100);
    sts_deck_add(&d, 101);
    for (j = 0; j < 200; j++) {
        n = sts_offer_generate(&g_db, 0, &d, &r, &s, &rng, o, 8);
        CHECK(n == 3);
        for (i = 0; i < n; i++)
            CHECK(o[i].copies <= 2);
    }

    /* REMOVE: only cards in the deck, and never below deck_min. */
    s = add_settings();
    s.mode = STS_REWARD_REMOVE;
    s.copies_max = 4;
    r = open_rules();
    r.deck_min = 3;
    sts_deck_clear(&d);
    add_copies(&d, 100, 2);
    add_copies(&d, 200, 2);
    for (j = 0; j < 200; j++) {
        n = sts_offer_generate(&g_db, 0, &d, &r, &s, &rng, o, 8);
        CHECK(n == 2);
        for (i = 0; i < n; i++) {
            CHECK(o[i].id == 100 || o[i].id == 200);
            CHECK(o[i].copies == 1);     /* 4 cards, deck_min 3: only 1 may go */
        }
    }
    r.deck_min = 4;
    CHECK(sts_offer_generate(&g_db, 0, &d, &r, &s, &rng, o, 8) == 0);
}

/* Every eligible card turns up, at a roughly fair rate. */
void test_offer_fairness(void)
{
    enum { ROLLS = 20000 };
    sts_rules r = open_rules();
    sts_offer_settings s = add_settings();
    sts_offer o[8];
    sts_deck d;
    sts_rng rng;
    int hits[300];
    int c = 12;                  /* Komachi */
    int cards, expected, i, j, n;
    const sts_card_list *l = &g_db.characters[c];

    memset(hits, 0, sizeof hits);
    sts_deck_clear(&d);
    sts_rng_seed(&rng, 2024, 11);
    for (j = 0; j < ROLLS; j++) {
        n = sts_offer_generate(&g_db, c, &d, &r, &s, &rng, o, 8);
        for (i = 0; i < n; i++)
            hits[o[i].id]++;
    }
    cards = g_db.system.count + l->count;
    expected = ROLLS * 3 / cards;
    for (i = 0; i < g_db.system.count; i++) {
        int h = hits[g_db.system.cards[i].id];
        CHECK(h > expected * 9 / 10 && h < expected * 11 / 10);
    }
    for (i = 0; i < l->count; i++) {
        int h = hits[l->cards[i].id];
        CHECK(h > expected * 9 / 10 && h < expected * 11 / 10);
    }
}

static void apply_offer(sts_deck *d, const sts_offer *o, sts_reward_mode mode)
{
    int i, j;

    if (mode == STS_REWARD_REMOVE || mode == STS_REWARD_REPLACE) {
        /* REMOVE takes the offered card; REPLACE stands in for a player's choice by
         * dropping from the front of the deck. */
        for (i = 0; i < o->copies; i++) {
            int at = 0;
            if (mode == STS_REWARD_REMOVE)
                for (at = 0; at < d->count && d->ids[at] != o->id; at++) {}
            for (j = at; j + 1 < d->count; j++)
                d->ids[j] = d->ids[j + 1];
            d->count--;
        }
    }
    if (mode == STS_REWARD_ADD || mode == STS_REWARD_REPLACE)
        for (i = 0; i < o->copies; i++)
            sts_deck_add(d, o->id);
}

/* Whole runs: a 20-pick draft followed by rewards, for every character and mode,
 * must stay legal at every step without ever needing a retry. */
void test_offer_runs(void)
{
    static const sts_reward_mode modes[3] = { STS_REWARD_ADD, STS_REWARD_REPLACE, STS_REWARD_REMOVE };
    sts_rules vanilla = sts_rules_vanilla();
    sts_rules bounds;
    sts_offer_settings draft = add_settings(), s;
    sts_offer o[8];
    sts_deck d;
    sts_rng rng;
    int c, m, pick, seed, n, violations = 0, short_draft = 0;

    bounds.deck_min = 15;
    bounds.deck_max = 30;
    bounds.copy_limit = vanilla.copy_limit;

    for (seed = 0; seed < 50; seed++) {
        for (c = 0; c < STS_NUM_CHARACTERS; c++) {
            sts_rng_seed(&rng, (uint64_t)seed, (uint64_t)c);
            sts_deck_clear(&d);
            for (pick = 0; pick < 20; pick++) {
                n = sts_offer_generate(&g_db, c, &d, &vanilla, &draft, &rng, o, 8);
                if (n != 3)
                    short_draft++;
                if (n > 0)
                    apply_offer(&d, &o[sts_rng_below(&rng, (uint32_t)n)], STS_REWARD_ADD);
            }
            if (sts_deck_validate(&d, &vanilla, NULL) != STS_DECK_OK || !sts_deck_cards_legal(&g_db, c, &d, NULL))
                violations++;

            for (m = 0; m < 3; m++) {
                s = add_settings();
                s.mode = modes[m];
                s.copies_max = 4;
                for (pick = 0; pick < 40; pick++) {
                    n = sts_offer_generate(&g_db, c, &d, &bounds, &s, &rng, o, 8);
                    if (n > 0)
                        apply_offer(&d, &o[sts_rng_below(&rng, (uint32_t)n)], modes[m]);
                    if (sts_deck_validate(&d, &bounds, NULL) != STS_DECK_OK)
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
    sts_offer o;
    o.id = id;
    o.type = STS_CARD_SKILL;
    o.copies = copies;
    return o;
}

void test_offer_apply(void)
{
    sts_rules r = open_rules();
    sts_rules fixed = sts_rules_vanilla();
    sts_offer o;
    sts_deck d, before;
    sts_rng rng;
    uint16_t ids[4];
    int i, j;

    sts_rng_seed(&rng, 3, 3);

    /* ADD: allowed below deck_min, sorted afterwards, atomic when refused. */
    fixed.deck_min = 20;
    sts_deck_clear(&d);
    sts_deck_add(&d, 200);
    o = make_offer(100, 2);
    CHECK(sts_offer_apply_add(&d, &o, &fixed) == STS_APPLY_OK);
    CHECK(d.count == 3 && d.ids[0] == 100 && d.ids[1] == 100 && d.ids[2] == 200);
    add_copies(&d, 100, 2);
    before = d;
    o = make_offer(100, 1);    /* a 5th copy */
    CHECK(sts_offer_apply_add(&d, &o, &fixed) == STS_APPLY_ILLEGAL);
    CHECK(decks_equal(&d, &before));
    o = make_offer(101, 0);
    CHECK(sts_offer_apply_add(&d, &o, &fixed) == STS_APPLY_ILLEGAL);
    r.deck_max = d.count;
    o = make_offer(101, 1);    /* over deck_max */
    CHECK(sts_offer_apply_add(&d, &o, &r) == STS_APPLY_ILLEGAL);
    CHECK(decks_equal(&d, &before));

    /* REMOVE: takes only cards that exist, and respects deck_min. */
    r = open_rules();
    sts_deck_clear(&d);
    add_copies(&d, 100, 2);
    add_copies(&d, 101, 2);
    before = d;
    o = make_offer(200, 1);
    CHECK(sts_offer_apply_remove(&d, &o, &r) == STS_APPLY_BAD_REMOVAL);
    o = make_offer(100, 3);
    CHECK(sts_offer_apply_remove(&d, &o, &r) == STS_APPLY_BAD_REMOVAL);
    CHECK(decks_equal(&d, &before));
    r.deck_min = 4;
    o = make_offer(100, 1);
    CHECK(sts_offer_apply_remove(&d, &o, &r) == STS_APPLY_ILLEGAL);
    CHECK(decks_equal(&d, &before));
    r.deck_min = 3;
    CHECK(sts_offer_apply_remove(&d, &o, &r) == STS_APPLY_OK);
    CHECK(d.count == 3 && sts_deck_count_of(&d, 100) == 1);

    /* REPLACE chosen: exact count, cards must exist, duplicates need real copies. */
    r = open_rules();
    sts_deck_clear(&d);
    add_copies(&d, 100, 2);
    add_copies(&d, 101, 2);
    before = d;
    o = make_offer(200, 2);
    ids[0] = 100; ids[1] = 300;
    CHECK(sts_offer_replace_chosen(&d, &o, &r, ids, 2) == STS_APPLY_BAD_REMOVAL);
    CHECK(sts_offer_replace_chosen(&d, &o, &r, ids, 1) == STS_APPLY_BAD_REMOVAL);
    o = make_offer(200, 3);
    ids[0] = 100; ids[1] = 100; ids[2] = 100;   /* only 2 copies exist */
    CHECK(sts_offer_replace_chosen(&d, &o, &r, ids, 3) == STS_APPLY_BAD_REMOVAL);
    CHECK(decks_equal(&d, &before));
    o = make_offer(200, 2);
    ids[0] = 100; ids[1] = 100;
    CHECK(sts_offer_replace_chosen(&d, &o, &r, ids, 2) == STS_APPLY_OK);
    CHECK(d.count == 4 && sts_deck_count_of(&d, 100) == 0 && sts_deck_count_of(&d, 200) == 2);

    /* Replacing into the copy limit is refused and changes nothing. */
    sts_deck_clear(&d);
    add_copies(&d, 100, 4);
    add_copies(&d, 101, 1);
    before = d;
    o = make_offer(100, 1);
    ids[0] = 101;
    CHECK(sts_offer_replace_chosen(&d, &o, &r, ids, 1) == STS_APPLY_ILLEGAL);
    CHECK(decks_equal(&d, &before));

    /* REPLACE by type removes only that type, whichever copies the roll lands on. */
    sts_deck_clear(&d);
    add_copies(&d, 0, 3);       /* system */
    add_copies(&d, 100, 2);     /* skill */
    add_copies(&d, 200, 1);     /* spell */
    before = d;
    o = make_offer(1, 2);
    CHECK(sts_offer_replace_by_type(&d, &o, &r, &g_db, 0, STS_CARD_SPELL, &rng) == STS_APPLY_BAD_REMOVAL);
    CHECK(decks_equal(&d, &before));
    CHECK(sts_offer_replace_by_type(&d, &o, &r, &g_db, 0, 99, &rng) == STS_APPLY_BAD_REMOVAL);
    CHECK(sts_offer_replace_by_type(&d, &o, &r, &g_db, 0, STS_CARD_SKILL, &rng) == STS_APPLY_OK);
    CHECK(d.count == 6 && sts_deck_count_of(&d, 100) == 0 && sts_deck_count_of(&d, 1) == 2);
    CHECK(sts_deck_count_of(&d, 0) == 3 && sts_deck_count_of(&d, 200) == 1);

    /* The types a BY_TYPE screen can offer. */
    CHECK(sts_replace_types_available(&g_db, 0, &before, 1) == STS_TYPE_ALL);
    CHECK(sts_replace_types_available(&g_db, 0, &before, 2)
          == (STS_TYPE_BIT(STS_CARD_SYSTEM) | STS_TYPE_BIT(STS_CARD_SKILL)));
    CHECK(sts_replace_types_available(&g_db, 0, &before, 4) == 0);

    /* REPLACE random can land on any card, and never needs more than the deck has. */
    {
        int removed_a = 0, removed_b = 0;
        for (i = 0; i < 200; i++) {
            sts_deck_clear(&d);
            sts_deck_add(&d, 100);
            sts_deck_add(&d, 101);
            o = make_offer(200, 1);
            CHECK(sts_offer_replace_random(&d, &o, &r, &rng) == STS_APPLY_OK);
            CHECK(d.count == 2 && sts_deck_count_of(&d, 200) == 1);
            if (!sts_deck_count_of(&d, 100)) removed_a++;
            if (!sts_deck_count_of(&d, 101)) removed_b++;
        }
        CHECK(removed_a > 50 && removed_b > 50 && removed_a + removed_b == 200);
        o = make_offer(200, 3);
        CHECK(sts_offer_replace_random(&d, &o, &r, &rng) == STS_APPLY_BAD_REMOVAL);
    }

    /* BY_TYPE offers fit what one type can give up: every type below 3 means no 3-copy
     * offers, though the deck as a whole has 6 cards. */
    {
        sts_offer_settings s = add_settings();
        sts_offer out[8];
        s.mode = STS_REWARD_REPLACE;
        s.replace = STS_REPLACE_BY_TYPE;
        s.copies_min = 3;
        s.copies_max = 3;
        sts_deck_clear(&d);
        add_copies(&d, 0, 2);
        add_copies(&d, 100, 2);
        add_copies(&d, 200, 2);
        CHECK(sts_offer_generate(&g_db, 0, &d, &r, &s, &rng, out, 8) == 0);
        s.replace = STS_REPLACE_RANDOM;
        CHECK(sts_offer_generate(&g_db, 0, &d, &r, &s, &rng, out, 8) == 3);
        /* Clamped copies stay within the largest type too. */
        s.replace = STS_REPLACE_BY_TYPE;
        s.copies_min = 1;
        s.copies_max = 4;
        for (j = 0; j < 100; j++) {
            int n = sts_offer_generate(&g_db, 0, &d, &r, &s, &rng, out, 8);
            CHECK(n == 3);
            for (i = 0; i < n; i++)
                CHECK(out[i].copies <= 2);
        }
    }
}
