/* Console tests for core/. No framework: CHECK() counts failures, main() returns non-zero on any.
 *
 * Optional tests against real files (never committed; point to files in reference/):
 *   STS_TEST_PF         a real profile: must parse fully, validate, and round-trip byte for byte
 *   STS_TEST_RANDOM_PF  a DeckRandomizer output: reports any illegal decks it contains
 * Without these variables those tests are skipped and the suite still passes.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "deck.h"
#include "offer.h"
#include "pf.h"
#include "pool.h"
#include "rng.h"

static int g_run, g_fail;

#define CHECK(cond) do { \
    g_run++; \
    if (!(cond)) { g_fail++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static sts_profile g_a, g_b;          /* static: a profile is ~41 KB */
static uint8_t g_buf[64 * 1024];

static void fill_legal_deck(sts_deck *d, int base)
{
    int i;
    sts_deck_clear(d);
    for (i = 0; i < 20; i++)        /* 20 distinct cards, 1 copy each */
        sts_deck_add(d, (uint16_t)(base + i));
}

static void test_rules_and_validation(void)
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

static void test_deck_helpers(void)
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

static void test_pf_synthetic_roundtrip(void)
{
    size_t consumed = 0, size, written;
    int c, s, i;

    memset(&g_a, 0, sizeof g_a);
    for (i = 0; i < STS_PF_HEADER_SIZE; i++) g_a.header[i] = (uint8_t)(i * 3);
    for (c = 0; c < STS_NUM_CHARACTERS; c++)
        for (s = 0; s < STS_NUM_SLOTS; s++)
            fill_legal_deck(&g_a.decks[c][s], 100 + c);
    /* one unsorted deck of a non-standard size, with an ID above 255 to exercise the high byte */
    sts_deck_clear(&g_a.decks[3][2]);
    sts_deck_add(&g_a.decks[3][2], 219);
    sts_deck_add(&g_a.decks[3][2], 1);
    sts_deck_add(&g_a.decks[3][2], 0x0102);

    size = sts_pf_size(&g_a);
    CHECK(size == STS_PF_HEADER_SIZE + (size_t)(79 * (1 + 40)) + (1 + 3 * 2));
    written = sts_pf_serialize(&g_a, g_buf, sizeof g_buf);
    CHECK(written == size);
    CHECK(sts_pf_serialize(&g_a, g_buf, size - 1) == 0);   /* buffer too small */

    CHECK(sts_pf_parse(g_buf, written, &g_b, &consumed) == STS_PF_OK);
    CHECK(consumed == written);
    CHECK(memcmp(g_a.header, g_b.header, STS_PF_HEADER_SIZE) == 0);
    CHECK(g_b.decks[3][2].count == 3);
    CHECK(g_b.decks[3][2].ids[0] == 1 && g_b.decks[3][2].ids[1] == 219 && g_b.decks[3][2].ids[2] == 0x0102);
    CHECK(g_b.decks[19][3].count == 20 && g_b.decks[19][3].ids[0] == 119);

    /* Truncation is detected at every cut point that matters. */
    CHECK(sts_pf_parse(g_buf, STS_PF_HEADER_SIZE - 1, &g_b, NULL) == STS_PF_TRUNCATED);
    CHECK(sts_pf_parse(g_buf, STS_PF_HEADER_SIZE, &g_b, NULL) == STS_PF_TRUNCATED);
    CHECK(sts_pf_parse(g_buf, written - 1, &g_b, NULL) == STS_PF_TRUNCATED);
    CHECK(sts_pf_parse(g_buf, written, &g_b, NULL) == STS_PF_OK);

    /* Trailing bytes are reported, not rejected. */
    g_buf[written] = 0xAA;
    CHECK(sts_pf_parse(g_buf, written + 1, &g_b, &consumed) == STS_PF_OK && consumed == written);

    CHECK(strcmp(sts_character_name(0), "Reimu") == 0);
    CHECK(strcmp(sts_character_name(19), "Suwako") == 0);
    CHECK(strcmp(sts_character_name(20), "?") == 0);
}

static uint8_t *read_all(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *b;
    long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    b = (uint8_t *)malloc((size_t)n);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    *len = b ? (size_t)n : 0;
    return b;
}

static void test_real_profile(void)
{
    const char *path = getenv("STS_TEST_PF");
    sts_rules r = sts_rules_vanilla();
    size_t len = 0, consumed = 0, written;
    uint8_t *orig;
    int c, s, bad = 0;

    if (!path) { printf("SKIP real profile (set STS_TEST_PF)\n"); return; }
    orig = read_all(path, &len);
    CHECK(orig != NULL);
    if (!orig) return;

    CHECK(sts_pf_parse(orig, len, &g_a, &consumed) == STS_PF_OK);
    CHECK(consumed == len);
    for (c = 0; c < STS_NUM_CHARACTERS; c++)
        for (s = 0; s < STS_NUM_SLOTS; s++)
            if (sts_deck_validate(&g_a.decks[c][s], &r, NULL) != STS_DECK_OK) bad++;
    CHECK(bad == 0);
    written = sts_pf_serialize(&g_a, g_buf, sizeof g_buf);
    CHECK(written == len && memcmp(g_buf, orig, len) == 0);   /* byte-identical round trip */
    printf("real profile: %zu bytes, %d illegal decks\n", len, bad);
    free(orig);
}

/* ---------- rng ---------- */

static void test_rng(void)
{
    /* Reference output of the PCG32 demo program for seed 42, sequence 54. If this
     * fails, the generator is not portable. */
    static const uint32_t expected[6] = {
        0xa15c02b7u, 0x7b47f409u, 0xba1d3330u, 0x83d2f293u, 0xbfa4784bu, 0xcbed606eu
    };
    sts_rng a, b;
    int i, seen[7], all_seen = 1;

    sts_rng_seed(&a, 42, 54);
    for (i = 0; i < 6; i++)
        CHECK(sts_rng_next(&a) == expected[i]);

    sts_rng_seed(&a, 7, 1);
    sts_rng_seed(&b, 7, 1);
    for (i = 0; i < 100; i++)
        CHECK(sts_rng_next(&a) == sts_rng_next(&b));

    memset(seen, 0, sizeof seen);
    for (i = 0; i < 1000; i++) {
        uint32_t v = sts_rng_below(&a, 7);
        CHECK(v < 7);
        if (v < 7) seen[v] = 1;
    }
    for (i = 0; i < 7; i++)
        if (!seen[i]) all_seen = 0;
    CHECK(all_seen);
    CHECK(sts_rng_below(&a, 1) == 0);
}

/* ---------- card table ---------- */

static sts_card_db g_db, g_db2;        /* static: a table is ~6 KB */
static char g_text[8 * 1024];

/* Writes the smallest complete table: one system card and one spell per character. */
static size_t minimal_db_text(char *out)
{
    size_t n = 0;
    int c;
    n += (size_t)sprintf(out + n, "system,0,system,1\n");
    for (c = 0; c < STS_NUM_CHARACTERS; c++)
        n += (size_t)sprintf(out + n, "%s,200,spell,3\n", sts_character_name(c));
    return n;
}

/* Parses the minimal table plus one extra line. Returns the status. */
static sts_db_status parse_with_extra(const char *extra, int *err_line)
{
    size_t n = minimal_db_text(g_text);
    n += (size_t)sprintf(g_text + n, "%s", extra);
    return sts_card_db_parse(g_text, n, &g_db2, err_line);
}

static void test_card_db_parse(void)
{
    int err = 0, i;
    size_t n;

    /* Comments, blank lines, CRLF endings and a missing final newline are all fine. */
    n = minimal_db_text(g_text);
    n += (size_t)sprintf(g_text + n, "# comment\n\r\n\nReimu,100,skill,2\r\nCirno,213,spell,5");
    CHECK(sts_card_db_parse(g_text, n, &g_db2, &err) == STS_DB_OK);
    CHECK(g_db2.system.count == 1 && g_db2.characters[0].count == 2 && g_db2.characters[16].count == 2);
    CHECK(sts_card_db_find(&g_db2, 0, 100) != NULL && sts_card_db_find(&g_db2, 0, 100)->cost_or_slot == 2);
    CHECK(sts_card_db_find(&g_db2, 0, 100)->type == STS_CARD_SKILL);

    /* Each kind of damage is reported, on the right line. */
    CHECK(parse_with_extra("Reimu,100,skill\n", &err) == STS_DB_BAD_LINE && err == 22);
    CHECK(parse_with_extra("Reimu,100,skill,1,9\n", &err) == STS_DB_BAD_LINE && err == 22);
    CHECK(parse_with_extra("Nobody,100,skill,1\n", &err) == STS_DB_BAD_LINE);
    CHECK(parse_with_extra("Reimu,100,trap,1\n", &err) == STS_DB_BAD_LINE);
    CHECK(parse_with_extra("Reimu,12x,skill,1\n", &err) == STS_DB_BAD_LINE);
    CHECK(parse_with_extra("Reimu,,skill,1\n", &err) == STS_DB_BAD_LINE);
    CHECK(parse_with_extra("Reimu,70000,skill,1\n", &err) == STS_DB_BAD_LINE);
    CHECK(parse_with_extra("Reimu,100,skill,300\n", &err) == STS_DB_BAD_LINE);
    CHECK(parse_with_extra("Reimu,200,spell,3\n", &err) == STS_DB_DUPLICATE && err == 22);

    /* The same ID under different owners is normal, not a duplicate. */
    CHECK(parse_with_extra("", &err) == STS_DB_OK);

    /* An over-long line is rejected, not truncated into something valid. */
    memset(g_text, 'x', 300);
    g_text[300] = '\n';
    CHECK(sts_card_db_parse(g_text, 301, &g_db2, &err) == STS_DB_BAD_LINE && err == 1);

    /* Too many cards for one owner. */
    n = minimal_db_text(g_text);
    for (i = 0; i < STS_POOL_MAX_CARDS; i++)
        n += (size_t)sprintf(g_text + n, "Reimu,%d,spell,1\n", 300 + i);
    CHECK(sts_card_db_parse(g_text, n, &g_db2, &err) == STS_DB_OVERFLOW);

    /* A table missing a character or the system list is rejected. */
    n = (size_t)sprintf(g_text, "system,0,system,1\nReimu,100,skill,0\n");
    CHECK(sts_card_db_parse(g_text, n, &g_db2, &err) == STS_DB_INCOMPLETE);
    CHECK(sts_card_db_parse("", 0, &g_db2, &err) == STS_DB_INCOMPLETE);

    CHECK(sts_card_db_load("no/such/file.csv", &g_db2, &err) == STS_DB_IO);
    CHECK(strcmp(sts_db_status_str(STS_DB_DUPLICATE), "duplicate card") == 0);
}

static int count_type(const sts_card_list *l, int type)
{
    int i, n = 0;
    for (i = 0; i < l->count; i++)
        if (l->cards[i].type == type) n++;
    return n;
}

/* Checks data/cards.csv against what was read from the game. */
static void test_card_db_file(void)
{
    int err = 0, c, total = 0;
    sts_db_status st = sts_card_db_load(STS_CARDS_PATH, &g_db, &err);
    sts_deck d;
    uint16_t bad = 0;

    if (st != STS_DB_OK)
        printf("cards file: %s (line %d)\n", sts_db_status_str(st), err);
    CHECK(st == STS_DB_OK);
    if (st != STS_DB_OK) return;

    CHECK(g_db.system.count == 21);
    CHECK(count_type(&g_db.system, STS_CARD_SYSTEM) == 21);
    total = g_db.system.count;
    for (c = 0; c < STS_NUM_CHARACTERS; c++) {
        const sts_card_list *l = &g_db.characters[c];
        total += l->count;
        CHECK(count_type(l, STS_CARD_SYSTEM) == 0);
        CHECK(count_type(l, STS_CARD_SKILL) == (c == 4 ? 15 : 12));  /* Patchouli has 15 */
        CHECK(count_type(l, STS_CARD_SPELL) >= 9 && count_type(l, STS_CARD_SPELL) <= 14);
    }
    CHECK(total == 482);

    /* Spell IDs are per character: 213 exists for Patchouli, Cirno and Utsuho only. */
    CHECK(sts_card_db_find(&g_db, 4, 213) && sts_card_db_find(&g_db, 16, 213) && sts_card_db_find(&g_db, 18, 213));
    CHECK(!sts_card_db_find(&g_db, 0, 213));
    /* System cards belong to everyone; other IDs and characters do not resolve. */
    CHECK(sts_card_db_find(&g_db, 19, 20) && sts_card_db_find(&g_db, 19, 20)->type == STS_CARD_SYSTEM);
    CHECK(!sts_card_db_find(&g_db, 0, 112));   /* only Patchouli has a 13th skill */
    CHECK(sts_card_db_find(&g_db, 4, 112) != NULL);
    CHECK(!sts_card_db_find(&g_db, -1, 0) && !sts_card_db_find(&g_db, STS_NUM_CHARACTERS, 0));

    /* A skill family shares one slot value across its upgrades. */
    CHECK(sts_card_db_find(&g_db, 1, 100)->cost_or_slot == sts_card_db_find(&g_db, 1, 104)->cost_or_slot);
    CHECK(sts_card_db_find(&g_db, 1, 100)->cost_or_slot == sts_card_db_find(&g_db, 1, 108)->cost_or_slot);

    sts_deck_clear(&d);
    sts_deck_add(&d, 0);
    sts_deck_add(&d, 100);
    sts_deck_add(&d, 213);
    CHECK(sts_deck_cards_legal(&g_db, 4, &d, &bad));
    CHECK(!sts_deck_cards_legal(&g_db, 0, &d, &bad) && bad == 213);
}

/* ---------- offers ---------- */

static sts_offer_settings add_settings(void)
{
    sts_offer_settings s;
    s.mode = STS_REWARD_ADD;
    s.offer_count = 3;
    s.copies_min = 1;
    s.copies_max = 1;
    s.type_mask = STS_TYPE_ALL;
    return s;
}

static void add_copies(sts_deck *d, uint16_t id, int n)
{
    int i;
    for (i = 0; i < n; i++)
        sts_deck_add(d, id);
}

static sts_rules open_rules(void)
{
    sts_rules r;
    r.deck_min = 0;
    r.deck_max = 40;
    r.copy_limit = 4;
    return r;
}

static void test_offer_basics(void)
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
static void test_offer_limits(void)
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
static void test_offer_fairness(void)
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
static void test_offer_runs(void)
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

/* Every deck in a real profile must use only cards its character owns. */
static void test_real_profile_cards(void)
{
    const char *path = getenv("STS_TEST_PF");
    int c, s, bad = 0;

    if (!path) { printf("SKIP real profile cards (set STS_TEST_PF)\n"); return; }
    CHECK(sts_pf_load(path, &g_a, NULL) == STS_PF_OK);
    for (c = 0; c < STS_NUM_CHARACTERS; c++)
        for (s = 0; s < STS_NUM_SLOTS; s++)
            if (!sts_deck_cards_legal(&g_db, c, &g_a.decks[c][s], NULL)) bad++;
    CHECK(bad == 0);
}

static void test_randomizer_output(void)
{
    const char *path = getenv("STS_TEST_RANDOM_PF");
    sts_rules r = sts_rules_vanilla();
    size_t consumed = 0;
    int c, s, violations = 0;

    if (!path) { printf("SKIP randomizer output (set STS_TEST_RANDOM_PF)\n"); return; }
    CHECK(sts_pf_load(path, &g_a, &consumed) == STS_PF_OK);
    for (c = 0; c < STS_NUM_CHARACTERS; c++) {
        for (s = 0; s < STS_NUM_SLOTS; s++) {
            uint16_t bad = 0;
            sts_deck_status st = sts_deck_validate(&g_a.decks[c][s], &r, &bad);
            CHECK(g_a.decks[c][s].count == 20);
            CHECK(st == STS_DECK_OK || st == STS_DECK_COPY_LIMIT);
            if (st != STS_DECK_OK) {
                violations++;
                printf("  illegal: %s slot %d (%s, card %u x%d)\n", sts_character_name(c), s + 1,
                       sts_deck_status_str(st), bad, sts_deck_count_of(&g_a.decks[c][s], bad));
            }
        }
    }
    printf("randomizer output: %d illegal deck(s)\n", violations);
}

int main(void)
{
    test_rules_and_validation();
    test_deck_helpers();
    test_pf_synthetic_roundtrip();
    test_real_profile();
    test_randomizer_output();
    test_rng();
    test_card_db_parse();
    test_card_db_file();
    test_offer_basics();
    test_offer_limits();
    test_offer_fairness();
    test_offer_runs();
    test_real_profile_cards();
    printf("%d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
