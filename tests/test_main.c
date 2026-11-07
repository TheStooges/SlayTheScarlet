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
#include "run.h"
#include "settings.h"
#include "util.h"

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

static int count_type_in_deck(const sts_deck *d, int character, int type)
{
    int i, n = 0;
    for (i = 0; i < d->count; i++) {
        const sts_card *c = sts_card_db_find(&g_db, character, d->ids[i]);
        if (c && c->type == type) n++;
    }
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
    s.replace = STS_REPLACE_CHOOSE;
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

/* ---------- applying offers ---------- */

static sts_offer make_offer(uint16_t id, int copies)
{
    sts_offer o;
    o.id = id;
    o.type = STS_CARD_SKILL;
    o.copies = copies;
    return o;
}

static int decks_equal(const sts_deck *a, const sts_deck *b)
{
    return a->count == b->count && memcmp(a->ids, b->ids, (size_t)a->count * 2) == 0;
}

static void test_offer_apply(void)
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

/* ---------- settings ---------- */

static sts_settings_status parse_ini(const char *text, sts_settings *s, int *line)
{
    return sts_settings_parse(text, strlen(text), s, line);
}

static void test_settings(void)
{
    sts_settings s, d;
    int line = 0;

    /* The defaults are legal, and the shipped file states exactly the defaults. */
    sts_settings_defaults(&d);
    CHECK(sts_settings_validate(&d) == NULL);
    CHECK(d.rules.deck_min == 20 && d.rules.deck_max == 20 && d.rules.copy_limit == 4);
    CHECK(d.draft_picks == 20 && d.draft.offer_count == 3 && d.draft.mode == STS_REWARD_ADD);
    CHECK(d.pick_visibility == STS_VISIBILITY_AFTER_CONFIRM && d.items_enabled == 0);
    CHECK(sts_settings_load(STS_SETTINGS_PATH, &s, &line) == STS_SETTINGS_OK);
    CHECK(sts_settings_hash(&s) == sts_settings_hash(&d));

    /* An empty file gives the defaults; a file only needs what it changes. */
    CHECK(parse_ini("", &s, &line) == STS_SETTINGS_OK);
    CHECK(sts_settings_hash(&s) == sts_settings_hash(&d));
    CHECK(parse_ini("# c\n; c\n\n  [deck]  \r\n  size_max = 30  \r\n", &s, &line) == STS_SETTINGS_OK);
    CHECK(s.rules.deck_max == 30 && s.rules.deck_min == 20);

    /* Every setting is reachable. */
    CHECK(parse_ini("[deck]\nsize_min=15\nsize_max=25\ncopy_limit=3\n"
                    "[draft]\npicks=22\noffers=4\ncopies_min=1\ncopies_max=2\ntypes=skill,spell\n"
                    "[reward]\nmode=add\nwho_picks=loser\noffers=5\ncopies_min=2\ncopies_max=3\n"
                    "types=system\nreplacement=by_type\n"
                    "[netplay]\npick_visibility=live\nopponent_deck=full\n"
                    "[items]\nenabled=true\n", &s, &line) == STS_SETTINGS_OK);
    CHECK(s.rules.deck_min == 15 && s.rules.deck_max == 25 && s.rules.copy_limit == 3);
    CHECK(s.draft_picks == 22 && s.draft.offer_count == 4 && s.draft.copies_max == 2);
    CHECK(s.draft.type_mask == (STS_TYPE_BIT(STS_CARD_SKILL) | STS_TYPE_BIT(STS_CARD_SPELL)));
    CHECK(s.reward.mode == STS_REWARD_ADD && s.who_picks == STS_WHO_LOSER && s.reward.offer_count == 5);
    CHECK(s.reward.copies_min == 2 && s.reward.copies_max == 3);
    CHECK(s.reward.type_mask == STS_TYPE_BIT(STS_CARD_SYSTEM) && s.reward.replace == STS_REPLACE_BY_TYPE);
    CHECK(s.pick_visibility == STS_VISIBILITY_LIVE && s.opponent_deck == STS_OPPONENT_DECK_FULL);
    CHECK(s.items_enabled == 1);
    CHECK(sts_settings_validate(&s) == NULL);

    /* A long comment is fine; a long setting is refused rather than truncated. */
    memset(g_text, 'x', 300);
    g_text[0] = ' ';
    g_text[1] = '#';
    g_text[300] = '\n';
    g_text[301] = 0;
    CHECK(parse_ini(g_text, &s, &line) == STS_SETTINGS_OK);
    g_text[1] = 'x';
    CHECK(parse_ini(g_text, &s, &line) == STS_SETTINGS_BAD_LINE && line == 1);

    /* Damage is reported with the right kind and line. */
    CHECK(parse_ini("[deck]\nnonsense\n", &s, &line) == STS_SETTINGS_BAD_LINE && line == 2);
    CHECK(parse_ini("[deck\n", &s, &line) == STS_SETTINGS_BAD_LINE && line == 1);
    CHECK(parse_ini("[nope]\n", &s, &line) == STS_SETTINGS_UNKNOWN_SECTION);
    CHECK(parse_ini("size_min=1\n", &s, &line) == STS_SETTINGS_UNKNOWN_KEY && line == 1);
    CHECK(parse_ini("[deck]\n\n\nsizemin=1\n", &s, &line) == STS_SETTINGS_UNKNOWN_KEY && line == 4);
    CHECK(parse_ini("[draft]\nmode=add\n", &s, &line) == STS_SETTINGS_UNKNOWN_KEY);
    CHECK(parse_ini("[deck]\nsize_min=12x\n", &s, &line) == STS_SETTINGS_BAD_VALUE);
    CHECK(parse_ini("[deck]\nsize_min=\n", &s, &line) == STS_SETTINGS_BAD_VALUE);
    CHECK(parse_ini("[deck]\nsize_min=-1\n", &s, &line) == STS_SETTINGS_BAD_VALUE);
    CHECK(parse_ini("[deck]\nsize_min=99999\n", &s, &line) == STS_SETTINGS_BAD_VALUE);
    CHECK(parse_ini("[reward]\nmode=gamble\n", &s, &line) == STS_SETTINGS_BAD_VALUE);
    CHECK(parse_ini("[reward]\ntypes=system,trap\n", &s, &line) == STS_SETTINGS_BAD_VALUE);
    CHECK(parse_ini("[reward]\ntypes=\n", &s, &line) == STS_SETTINGS_BAD_VALUE);
    CHECK(parse_ini("[items]\nenabled=1\n", &s, &line) == STS_SETTINGS_BAD_VALUE);
    CHECK(sts_settings_load("no/such/file.ini", &s, &line) == STS_SETTINGS_IO);
    CHECK(sts_settings_hash(&s) == sts_settings_hash(&d));    /* defaults even so */
    CHECK(strcmp(sts_settings_status_str(STS_SETTINGS_BAD_VALUE), "bad value") == 0);

    /* Combinations that could never work are refused. */
#define BAD_INI(text) do { \
        CHECK(parse_ini(text, &s, &line) == STS_SETTINGS_OK); \
        CHECK(sts_settings_validate(&s) != NULL); \
    } while (0)
    BAD_INI("[deck]\nsize_min=30\nsize_max=20\n");
    BAD_INI("[deck]\nsize_min=0\n");
    BAD_INI("[deck]\nsize_max=300\n[draft]\npicks=300\n");
    BAD_INI("[deck]\ncopy_limit=5\n");
    BAD_INI("[deck]\ncopy_limit=0\n");
    BAD_INI("[draft]\npicks=19\n");
    BAD_INI("[draft]\npicks=0\n");
    BAD_INI("[draft]\noffers=0\n");
    BAD_INI("[draft]\noffers=9\n");
    BAD_INI("[draft]\ncopies_max=5\n");
    BAD_INI("[draft]\ncopies_min=2\ncopies_max=3\npicks=10\n");
    BAD_INI("[draft]\ncopies_min=2\ncopies_max=2\npicks=9\n");
    BAD_INI("[draft]\ncopies_min=2\ncopies_max=2\npicks=11\n");
    BAD_INI("[reward]\nmode=add\n");
    BAD_INI("[reward]\nmode=remove\n");
    BAD_INI("[reward]\noffers=0\n");
    BAD_INI("[reward]\ncopies_min=3\ncopies_max=2\n");
    BAD_INI("[reward]\ncopies_max=5\n");
#undef BAD_INI

    /* Workable variations are accepted, including a skip with otherwise odd offers. */
    CHECK(parse_ini("[draft]\ncopies_min=2\ncopies_max=2\npicks=10\n", &s, &line) == STS_SETTINGS_OK);
    CHECK(sts_settings_validate(&s) == NULL);
    CHECK(parse_ini("[draft]\ncopies_min=1\ncopies_max=4\n", &s, &line) == STS_SETTINGS_OK);
    CHECK(sts_settings_validate(&s) == NULL);
    CHECK(parse_ini("[reward]\nmode=skip\noffers=0\n", &s, &line) == STS_SETTINGS_OK);
    CHECK(sts_settings_validate(&s) == NULL);
    CHECK(parse_ini("[deck]\nsize_min=15\nsize_max=25\n[reward]\nmode=remove\n", &s, &line) == STS_SETTINGS_OK);
    CHECK(sts_settings_validate(&s) == NULL);

    /* The fingerprint notices a change to any rule. */
    {
        uint32_t base = sts_settings_hash(&d);
#define HASH_DIFFERS(field, value) do { \
            s = d; s.field = value; \
            CHECK(sts_settings_hash(&s) != base); \
        } while (0)
        HASH_DIFFERS(rules.deck_min, 19);
        HASH_DIFFERS(rules.deck_max, 21);
        HASH_DIFFERS(rules.copy_limit, 3);
        HASH_DIFFERS(draft_picks, 21);
        HASH_DIFFERS(draft.offer_count, 4);
        HASH_DIFFERS(draft.copies_max, 2);
        HASH_DIFFERS(draft.type_mask, 1u);
        HASH_DIFFERS(reward.mode, STS_REWARD_SKIP);
        HASH_DIFFERS(reward.offer_count, 4);
        HASH_DIFFERS(reward.copies_min, 2);
        HASH_DIFFERS(reward.copies_max, 2);
        HASH_DIFFERS(reward.type_mask, 1u);
        HASH_DIFFERS(reward.replace, STS_REPLACE_RANDOM);
        HASH_DIFFERS(who_picks, STS_WHO_WINNER);
        HASH_DIFFERS(pick_visibility, STS_VISIBILITY_LIVE);
        HASH_DIFFERS(opponent_deck, STS_OPPONENT_DECK_FULL);
        HASH_DIFFERS(items_enabled, 1);
#undef HASH_DIFFERS
    }

    /* Compatible mode always ends up legal, 20 cards, 4 copies, no items. */
    {
        static const char *const inis[] = {
            "",
            "[deck]\nsize_min=15\nsize_max=30\n[reward]\nmode=add\n",
            "[deck]\nsize_min=10\nsize_max=10\n[draft]\npicks=10\n",
            "[deck]\nsize_max=40\n[draft]\ncopies_min=2\ncopies_max=2\npicks=20\n",
            "[deck]\ncopy_limit=2\n[reward]\nmode=remove\ncopies_max=2\n[items]\nenabled=true\n",
            "[deck]\nsize_min=15\nsize_max=30\n[reward]\nmode=skip\n[netplay]\npick_visibility=live\n",
        };
        size_t i;
        for (i = 0; i < sizeof inis / sizeof inis[0]; i++) {
            CHECK(parse_ini(inis[i], &s, &line) == STS_SETTINGS_OK);
            sts_settings_force_compatible(&s);
            CHECK(sts_settings_validate(&s) == NULL);
            CHECK(s.rules.deck_min == 20 && s.rules.deck_max == 20 && s.rules.copy_limit == 4);
            CHECK(s.items_enabled == 0);
            CHECK(s.reward.mode == STS_REWARD_REPLACE || s.reward.mode == STS_REWARD_SKIP);
            CHECK(s.pick_visibility == STS_VISIBILITY_HIDDEN);
        }

        /* It only changes what it must: other reward and draft choices survive. */
        CHECK(parse_ini("[deck]\nsize_min=15\nsize_max=30\n[draft]\noffers=5\n"
                        "[reward]\nmode=add\noffers=4\nwho_picks=winner\n", &s, &line) == STS_SETTINGS_OK);
        sts_settings_force_compatible(&s);
        CHECK(s.reward.mode == STS_REWARD_REPLACE && s.reward.offer_count == 4);
        CHECK(s.draft.offer_count == 5 && s.who_picks == STS_WHO_WINNER);
    }
}

/* ---------- runs ---------- */

/* Finishes a draft by always taking the first offer. */
static sts_run_status finish_draft(sts_run *run, const sts_settings *s)
{
    sts_run_status st = STS_RUN_OK;
    while (st == STS_RUN_OK && run->phase == STS_PHASE_DRAFT)
        st = sts_run_draft_pick(run, &g_db, s, 0);
    return st;
}

static void test_run_draft(void)
{
    static sts_run run, run2;
    sts_settings s;
    int c, seed, line = 0;

    sts_settings_defaults(&s);

    CHECK(sts_run_start(&run, &g_db, &s, -1, 1) == STS_RUN_BAD_ARGUMENT);
    CHECK(sts_run_start(&run, &g_db, &s, STS_NUM_CHARACTERS, 1) == STS_RUN_BAD_ARGUMENT);

    /* A default draft: 20 picks of 3 offers, ending legal and READY, for everyone. */
    for (seed = 0; seed < 20; seed++) {
        for (c = 0; c < STS_NUM_CHARACTERS; c++) {
            int pick = 0;
            CHECK(sts_run_start(&run, &g_db, &s, c, (uint64_t)seed) == STS_RUN_OK);
            CHECK(run.phase == STS_PHASE_DRAFT && run.deck.count == 0);
            while (run.phase == STS_PHASE_DRAFT) {
                CHECK(run.offer_count == 3);
                CHECK(run.deck.count == pick);
                CHECK(sts_run_draft_pick(&run, &g_db, &s, pick % 3) == STS_RUN_OK);
                pick++;
            }
            CHECK(pick == 20 && run.phase == STS_PHASE_READY && run.offer_count == 0);
            CHECK(run.picks_done == 20);
            CHECK(sts_deck_validate(&run.deck, &s.rules, NULL) == STS_DECK_OK);
            CHECK(sts_deck_cards_legal(&g_db, c, &run.deck, NULL));
        }
    }

    /* Refused actions change nothing. */
    CHECK(sts_run_start(&run, &g_db, &s, 0, 1) == STS_RUN_OK);
    run2 = run;
    CHECK(sts_run_draft_pick(&run, &g_db, &s, 3) == STS_RUN_BAD_INDEX);
    CHECK(sts_run_draft_pick(&run, &g_db, &s, -1) == STS_RUN_BAD_INDEX);
    CHECK(run.picks_done == run2.picks_done && decks_equal(&run.deck, &run2.deck));
    CHECK(finish_draft(&run, &s) == STS_RUN_OK);
    CHECK(sts_run_draft_pick(&run, &g_db, &s, 0) == STS_RUN_WRONG_PHASE);

    /* The same seed gives the same draft. */
    CHECK(sts_run_start(&run, &g_db, &s, 5, 1234) == STS_RUN_OK);
    CHECK(sts_run_start(&run2, &g_db, &s, 5, 1234) == STS_RUN_OK);
    CHECK(finish_draft(&run, &s) == STS_RUN_OK && finish_draft(&run2, &s) == STS_RUN_OK);
    CHECK(decks_equal(&run.deck, &run2.deck));

    /* Other legal draft rules also always land on a legal deck, ending early when full. */
    {
        static const char *const inis[] = {
            "[draft]\ncopies_min=1\ncopies_max=4\n",
            "[draft]\ncopies_min=2\ncopies_max=2\npicks=10\n",
            "[draft]\ncopies_min=4\ncopies_max=4\npicks=5\n",
            "[draft]\noffers=8\n",
            "[draft]\noffers=1\n",
            "[draft]\ntypes=spell\n",
            "[draft]\ntypes=skill\n",
            "[deck]\nsize_min=10\nsize_max=40\n[draft]\npicks=25\n",
            "[deck]\nsize_min=30\nsize_max=30\n[draft]\npicks=30\ncopies_max=3\n",
            "[deck]\ncopy_limit=1\n[draft]\ntypes=system,skill,spell\n",
        };
        size_t i;
        for (i = 0; i < sizeof inis / sizeof inis[0]; i++) {
            CHECK(parse_ini(inis[i], &s, &line) == STS_SETTINGS_OK);
            CHECK(sts_settings_validate(&s) == NULL);
            for (c = 0; c < STS_NUM_CHARACTERS; c++) {
                for (seed = 0; seed < 5; seed++) {
                    sts_run_status st;
                    CHECK(sts_run_start(&run, &g_db, &s, c, (uint64_t)seed) == STS_RUN_OK);
                    st = finish_draft(&run, &s);
                    CHECK(st == STS_RUN_OK);
                    CHECK(sts_deck_validate(&run.deck, &s.rules, NULL) == STS_DECK_OK);
                }
            }
        }
    }

    /* A pool too small for the deck reports stuck instead of faking a legal deck:
     * 1 copy each of Komachi's 9 spells cannot reach 20 cards. */
    CHECK(parse_ini("[deck]\ncopy_limit=1\n[draft]\ntypes=spell\n", &s, &line) == STS_SETTINGS_OK);
    CHECK(sts_run_start(&run, &g_db, &s, 12, 1) == STS_RUN_OK);
    CHECK(finish_draft(&run, &s) == STS_RUN_STUCK);
    CHECK(run.phase == STS_PHASE_DRAFT && run.offer_count == 0 && run.deck.count == 9);
}

/* Starts a finished draft under the given rules. */
static void ready_run(sts_run *run, const sts_settings *s, int character, uint64_t seed)
{
    CHECK(sts_run_start(run, &g_db, s, character, seed) == STS_RUN_OK);
    CHECK(finish_draft(run, s) == STS_RUN_OK);
    CHECK(run->phase == STS_PHASE_READY);
}

static void test_run_rewards(void)
{
    static sts_run run, before;
    sts_settings s;
    sts_reward_outcome out;
    sts_replace_choice ch;
    uint16_t remove[4];
    int line = 0, i, j;

    /* replace + choose: the default. Refused choices keep the offers on screen. */
    sts_settings_defaults(&s);
    ready_run(&run, &s, 0, 7);
    CHECK(sts_run_reward_pick(&run, &g_db, &s, 0, NULL) == STS_RUN_WRONG_PHASE);
    CHECK(sts_run_begin_reward(&run, &g_db, &s, 1, &out) == STS_RUN_OK);
    CHECK(out == STS_REWARD_OFFERS && run.phase == STS_PHASE_REWARD && run.offer_count == 3);
    CHECK(run.matches_played == 1);
    CHECK(sts_run_begin_reward(&run, &g_db, &s, 1, &out) == STS_RUN_WRONG_PHASE && out == STS_REWARD_NONE);
    CHECK(sts_run_draft_pick(&run, &g_db, &s, 0) == STS_RUN_WRONG_PHASE);

    before = run;
    CHECK(sts_run_reward_pick(&run, &g_db, &s, 3, NULL) == STS_RUN_BAD_INDEX);
    CHECK(sts_run_reward_pick(&run, &g_db, &s, 0, NULL) == STS_RUN_BAD_CHOICE);
    ch.remove_ids = remove;
    ch.remove_count = 1;
    ch.type = 0;
    remove[0] = 999;
    CHECK(sts_run_reward_pick(&run, &g_db, &s, 0, &ch) == STS_RUN_BAD_CHOICE);
    CHECK(run.phase == STS_PHASE_REWARD && run.offer_count == 3 && decks_equal(&run.deck, &before.deck));

    remove[0] = run.deck.ids[0];
    {
        uint16_t taken = run.offers[1].id;
        int had = sts_deck_count_of(&run.deck, taken);
        CHECK(sts_run_reward_pick(&run, &g_db, &s, 1, &ch) == STS_RUN_OK);
        CHECK(run.phase == STS_PHASE_READY && run.offer_count == 0 && run.deck.count == 20);
        CHECK(sts_deck_count_of(&run.deck, taken) >= had);
    }
    CHECK(sts_deck_validate(&run.deck, &s.rules, NULL) == STS_DECK_OK);

    /* who_picks: a left-out player keeps the deck and stays READY; the count still rises. */
    {
        static const struct { sts_who_picks who; int won; int offers; } cases[] = {
            {STS_WHO_BOTH, 1, 1}, {STS_WHO_BOTH, 0, 1},
            {STS_WHO_WINNER, 1, 1}, {STS_WHO_WINNER, 0, 0},
            {STS_WHO_LOSER, 1, 0}, {STS_WHO_LOSER, 0, 1},
        };
        size_t k;
        for (k = 0; k < sizeof cases / sizeof cases[0]; k++) {
            sts_settings_defaults(&s);
            s.who_picks = cases[k].who;
            ready_run(&run, &s, 3, 11);
            before = run;
            CHECK(sts_run_begin_reward(&run, &g_db, &s, cases[k].won, &out) == STS_RUN_OK);
            CHECK(out == (cases[k].offers ? STS_REWARD_OFFERS : STS_REWARD_NONE));
            CHECK(run.phase == (cases[k].offers ? STS_PHASE_REWARD : STS_PHASE_READY));
            CHECK(decks_equal(&run.deck, &before.deck));
            CHECK(run.matches_played == 1);
        }
    }

    /* nobody: changes the deck by itself, for winner and loser, whatever replacement says. */
    sts_settings_defaults(&s);
    s.who_picks = STS_WHO_NOBODY;
    for (i = 0; i < 2; i++) {
        int changed = 0;
        ready_run(&run, &s, 9, 21);
        for (j = 0; j < 100; j++) {
            before = run;
            CHECK(sts_run_begin_reward(&run, &g_db, &s, i, &out) == STS_RUN_OK);
            CHECK(out == STS_REWARD_AUTO && run.phase == STS_PHASE_READY && run.offer_count == 0);
            CHECK(sts_deck_validate(&run.deck, &s.rules, NULL) == STS_DECK_OK);
            if (!decks_equal(&run.deck, &before.deck)) changed++;
        }
        CHECK(changed > 50);
    }

    /* nobody removes at random, so the by_type limit on copies must not apply: no type
     * here has 3 cards, but the deck can still give up 3. */
    sts_settings_defaults(&s);
    s.rules.deck_min = 6;
    s.rules.deck_max = 6;
    s.who_picks = STS_WHO_NOBODY;
    s.reward.replace = STS_REPLACE_BY_TYPE;
    s.reward.copies_min = 3;
    s.reward.copies_max = 3;
    memset(&run, 0, sizeof run);
    run.phase = STS_PHASE_READY;
    sts_rng_seed(&run.rng, 1, 1);
    sts_deck_clear(&run.deck);
    add_copies(&run.deck, 0, 2);
    add_copies(&run.deck, 100, 2);
    add_copies(&run.deck, 200, 2);
    CHECK(sts_run_begin_reward(&run, &g_db, &s, 1, &out) == STS_RUN_OK && out == STS_REWARD_AUTO);
    CHECK(run.deck.count == 6);

    /* skip: no offers for anyone, with or without a picker. */
    sts_settings_defaults(&s);
    s.reward.mode = STS_REWARD_SKIP;
    s.who_picks = STS_WHO_NOBODY;
    ready_run(&run, &s, 0, 3);
    before = run;
    CHECK(sts_run_begin_reward(&run, &g_db, &s, 1, &out) == STS_RUN_OK && out == STS_REWARD_NONE);
    CHECK(decks_equal(&run.deck, &before.deck));

    sts_settings_defaults(&s);
    s.reward.mode = STS_REWARD_SKIP;
    ready_run(&run, &s, 0, 3);
    before = run;
    CHECK(sts_run_begin_reward(&run, &g_db, &s, 1, &out) == STS_RUN_OK && out == STS_REWARD_NONE);
    CHECK(decks_equal(&run.deck, &before.deck) && run.phase == STS_PHASE_READY);

    /* replace + by_type: the player's type is honoured; an unavailable one is refused. */
    sts_settings_defaults(&s);
    s.reward.replace = STS_REPLACE_BY_TYPE;
    ready_run(&run, &s, 2, 31);
    for (j = 0; j < 50; j++) {
        int type;
        int types_before[3], types_after[3];
        CHECK(sts_run_begin_reward(&run, &g_db, &s, 1, &out) == STS_RUN_OK && out == STS_REWARD_OFFERS);
        for (type = 0; type < 3; type++)
            types_before[type] = count_type_in_deck(&run.deck, run.character, type);
        ch.remove_ids = NULL;
        ch.remove_count = 0;
        ch.type = 99;
        CHECK(sts_run_reward_pick(&run, &g_db, &s, 0, &ch) == STS_RUN_BAD_CHOICE);
        /* The biggest type always has a card to give up. */
        ch.type = 0;
        for (type = 1; type < 3; type++)
            if (types_before[type] > types_before[ch.type]) ch.type = type;
        {
            int offered_type = run.offers[0].type;
            CHECK(sts_run_reward_pick(&run, &g_db, &s, 0, &ch) == STS_RUN_OK);
            for (type = 0; type < 3; type++)
                types_after[type] = count_type_in_deck(&run.deck, run.character, type);
            CHECK(types_after[offered_type] >= types_before[offered_type] - (offered_type == ch.type ? 1 : 0));
            if (offered_type != ch.type)
                CHECK(types_after[ch.type] == types_before[ch.type] - 1);
            else
                CHECK(types_after[ch.type] == types_before[ch.type]);
        }
        CHECK(run.deck.count == 20);
    }

    /* replace + random needs no choice from the player. */
    sts_settings_defaults(&s);
    s.reward.replace = STS_REPLACE_RANDOM;
    ready_run(&run, &s, 2, 32);
    for (j = 0; j < 50; j++) {
        CHECK(sts_run_begin_reward(&run, &g_db, &s, 0, &out) == STS_RUN_OK && out == STS_REWARD_OFFERS);
        CHECK(sts_run_reward_pick(&run, &g_db, &s, (int)(j % 3), NULL) == STS_RUN_OK);
        CHECK(run.deck.count == 20 && sts_deck_validate(&run.deck, &s.rules, NULL) == STS_DECK_OK);
    }

    /* add and remove walk the deck to its bounds and then stop offering. */
    CHECK(parse_ini("[deck]\nsize_min=20\nsize_max=23\n[reward]\nmode=add\n", &s, &line) == STS_SETTINGS_OK);
    CHECK(sts_settings_validate(&s) == NULL);
    ready_run(&run, &s, 4, 41);
    for (j = 0; j < 3; j++) {
        CHECK(sts_run_begin_reward(&run, &g_db, &s, 1, &out) == STS_RUN_OK && out == STS_REWARD_OFFERS);
        CHECK(sts_run_reward_pick(&run, &g_db, &s, 0, NULL) == STS_RUN_OK);
        CHECK(run.deck.count == 21 + j);
    }
    CHECK(sts_run_begin_reward(&run, &g_db, &s, 1, &out) == STS_RUN_OK && out == STS_REWARD_NONE);
    CHECK(run.phase == STS_PHASE_READY && run.deck.count == 23);

    CHECK(parse_ini("[deck]\nsize_min=18\nsize_max=20\n[reward]\nmode=remove\n", &s, &line) == STS_SETTINGS_OK);
    CHECK(sts_settings_validate(&s) == NULL);
    ready_run(&run, &s, 4, 42);
    for (j = 0; j < 2; j++) {
        CHECK(sts_run_begin_reward(&run, &g_db, &s, 1, &out) == STS_RUN_OK && out == STS_REWARD_OFFERS);
        CHECK(sts_deck_count_of(&run.deck, run.offers[0].id) > 0);
        CHECK(sts_run_reward_pick(&run, &g_db, &s, 0, NULL) == STS_RUN_OK);
        CHECK(run.deck.count == 19 - j);
    }
    CHECK(sts_run_begin_reward(&run, &g_db, &s, 1, &out) == STS_RUN_OK && out == STS_REWARD_NONE);
    CHECK(run.deck.count == 18);
}

/* ---------- save format ---------- */

static int runs_equal(const sts_run *a, const sts_run *b)
{
    int i;
    if (a->settings_hash != b->settings_hash || a->character != b->character || a->phase != b->phase
        || a->picks_done != b->picks_done || a->matches_played != b->matches_played
        || a->rng.state != b->rng.state || a->rng.inc != b->rng.inc
        || !decks_equal(&a->deck, &b->deck) || a->offer_count != b->offer_count)
        return 0;
    for (i = 0; i < a->offer_count; i++)
        if (a->offers[i].id != b->offers[i].id || a->offers[i].type != b->offers[i].type
            || a->offers[i].copies != b->offers[i].copies)
            return 0;
    return 1;
}

static int write_file(const char *path, const char *data, size_t n)
{
    FILE *f = fopen(path, "wb");
    int ok;
    if (!f) return 0;
    ok = fwrite(data, 1, n, f) == n;
    return fclose(f) == 0 && ok;
}

static void test_run_save(void)
{
    static const char *const path = "sts_test_run.sav";
    static const char *const tmp = "sts_test_run.sav.tmp";
    static sts_run run, loaded, other;
    static char text[STS_RUN_TEXT_MAX], broken[STS_RUN_TEXT_MAX];
    sts_settings s;
    sts_reward_outcome out;
    size_t n, i;
    int pick, bad;

    sts_settings_defaults(&s);
    remove(path);
    remove(tmp);

    /* Round trip in all three phases. */
    CHECK(sts_run_start(&run, &g_db, &s, 7, 555) == STS_RUN_OK);
    for (pick = 0; pick < 5; pick++)
        CHECK(sts_run_draft_pick(&run, &g_db, &s, 0) == STS_RUN_OK);
    n = sts_run_serialize(&run, text, sizeof text);
    CHECK(n > 0 && sts_run_parse(text, n, &loaded) == STS_RUN_OK && runs_equal(&run, &loaded));
    CHECK(sts_run_validate(&loaded, &g_db, &s) == STS_RUN_OK);

    /* Resuming mid-draft gives exactly the run that was never interrupted: a reload
     * shows the same offers, so it cannot reroll them. */
    CHECK(finish_draft(&run, &s) == STS_RUN_OK);
    CHECK(finish_draft(&loaded, &s) == STS_RUN_OK);
    CHECK(runs_equal(&run, &loaded));

    n = sts_run_serialize(&run, text, sizeof text);
    CHECK(n > 0 && sts_run_parse(text, n, &loaded) == STS_RUN_OK && runs_equal(&run, &loaded));
    CHECK(loaded.phase == STS_PHASE_READY && sts_run_validate(&loaded, &g_db, &s) == STS_RUN_OK);

    CHECK(sts_run_begin_reward(&run, &g_db, &s, 1, &out) == STS_RUN_OK && out == STS_REWARD_OFFERS);
    n = sts_run_serialize(&run, text, sizeof text);
    CHECK(n > 0 && sts_run_parse(text, n, &loaded) == STS_RUN_OK && runs_equal(&run, &loaded));
    CHECK(loaded.phase == STS_PHASE_REWARD && sts_run_validate(&loaded, &g_db, &s) == STS_RUN_OK);

    /* The largest deck still fits; a small buffer is reported. */
    other = run;
    other.deck.count = STS_MAX_DECK;
    for (pick = 0; pick < STS_MAX_DECK; pick++)
        other.deck.ids[pick] = 0xFFFF;
    other.offer_count = STS_OFFER_MAX;
    other.matches_played = 1000000000;
    for (pick = 0; pick < STS_OFFER_MAX; pick++) {
        other.offers[pick].id = 0xFFFF;
        other.offers[pick].type = STS_CARD_SPELL;
        other.offers[pick].copies = STS_MAX_DECK;
    }
    i = sts_run_serialize(&other, text, sizeof text);
    CHECK(i > 0 && sts_run_parse(text, i, &loaded) == STS_RUN_OK && runs_equal(&other, &loaded));
    CHECK(sts_run_serialize(&run, text, 10) == 0);
    CHECK(sts_run_serialize(&run, text, 0) == 0);

    /* The output is plain ASCII. */
    n = sts_run_serialize(&run, text, sizeof text);
    bad = 0;
    for (i = 0; i < n; i++)
        if ((unsigned char)text[i] > 127) bad++;
    CHECK(bad == 0);

    /* Flipping any single byte, or cutting the file anywhere, is detected. */
    bad = 0;
    for (i = 0; i < n; i++) {
        memcpy(broken, text, n);
        broken[i] ^= 0x01;
        if (sts_run_parse(broken, n, &loaded) != STS_RUN_DAMAGED) bad++;
    }
    CHECK(bad == 0);
    bad = 0;
    for (i = 0; i < n; i++)
        if (sts_run_parse(text, i, &loaded) != STS_RUN_DAMAGED) bad++;
    CHECK(bad == 0);

    /* An intact file from another format version is identified as such. */
    {
        uint32_t check;
        size_t body = n - 15, w;
        memcpy(broken, text, body);
        broken[8] = '2';
        check = sts_fnv1a(STS_FNV_START, broken, body);
        w = body + (size_t)sprintf(broken + body, "check %08x\n", (unsigned)check);
        CHECK(sts_run_parse(broken, w, &loaded) == STS_RUN_BAD_VERSION);
    }

    /* Validation catches what a checksum cannot: changed rules and illegal contents. */
    {
        sts_settings changed = s;
        n = sts_run_serialize(&run, text, sizeof text);
        CHECK(sts_run_parse(text, n, &loaded) == STS_RUN_OK);
        changed.rules.copy_limit = 3;
        CHECK(sts_run_validate(&loaded, &g_db, &changed) == STS_RUN_SETTINGS_CHANGED);

        other = loaded;
        other.character = 20;
        CHECK(sts_run_validate(&other, &g_db, &s) == STS_RUN_ILLEGAL);
        other = loaded;
        other.deck.ids[0] = 999;      /* not a real card */
        CHECK(sts_run_validate(&other, &g_db, &s) == STS_RUN_ILLEGAL);
        other = loaded;
        other.deck.count = 19;        /* below size_min outside the draft */
        CHECK(sts_run_validate(&other, &g_db, &s) == STS_RUN_ILLEGAL);
        other = loaded;
        other.offer_count = 0;        /* REWARD with nothing to pick */
        CHECK(sts_run_validate(&other, &g_db, &s) == STS_RUN_ILLEGAL);
        other = loaded;
        other.offers[0].id = 999;
        CHECK(sts_run_validate(&other, &g_db, &s) == STS_RUN_ILLEGAL);
        other = loaded;
        other.offers[0].type = (uint8_t)(other.offers[0].type == STS_CARD_SPELL ? STS_CARD_SKILL : STS_CARD_SPELL);
        CHECK(sts_run_validate(&other, &g_db, &s) == STS_RUN_ILLEGAL);
        other = loaded;
        other.offers[0].copies = 0;
        CHECK(sts_run_validate(&other, &g_db, &s) == STS_RUN_ILLEGAL);
        other = loaded;
        other.picks_done = 21;
        CHECK(sts_run_validate(&other, &g_db, &s) == STS_RUN_ILLEGAL);
        other = loaded;
        other.phase = STS_PHASE_READY;  /* READY must not carry offers */
        CHECK(sts_run_validate(&other, &g_db, &s) == STS_RUN_ILLEGAL);
    }

    /* Files: no save yet, then a normal save and overwrite, leaving no temp file. */
    CHECK(sts_run_load(path, &loaded) == STS_RUN_IO);
    CHECK(sts_run_save(path, &run) == STS_RUN_OK);
    CHECK(sts_run_load(path, &loaded) == STS_RUN_OK && runs_equal(&run, &loaded));
    other = run;
    other.matches_played = run.matches_played + 1;
    CHECK(sts_run_save(path, &other) == STS_RUN_OK);
    CHECK(sts_run_load(path, &loaded) == STS_RUN_OK && runs_equal(&other, &loaded));
    {
        FILE *f = fopen(tmp, "rb");
        CHECK(f == NULL);
        if (f) fclose(f);
    }

    /* A crash during the temp write: the partial file is ignored, the old save loads. */
    n = sts_run_serialize(&run, text, sizeof text);
    CHECK(write_file(tmp, text, n / 2));
    CHECK(sts_run_load(path, &loaded) == STS_RUN_OK && runs_equal(&other, &loaded));

    /* A crash just before the rename: the complete temp file is the newer one. */
    CHECK(write_file(tmp, text, n));
    CHECK(sts_run_load(path, &loaded) == STS_RUN_OK && runs_equal(&run, &loaded));

    /* Even with the main file gone, as after a remove with the rename not done. */
    remove(path);
    CHECK(sts_run_load(path, &loaded) == STS_RUN_OK && runs_equal(&run, &loaded));

    /* A later save clears the leftover and wins. */
    CHECK(sts_run_save(path, &other) == STS_RUN_OK);
    CHECK(sts_run_load(path, &loaded) == STS_RUN_OK && runs_equal(&other, &loaded));

    /* A damaged main file with no temp file is reported, not silently replaced. */
    remove(tmp);
    CHECK(write_file(path, text, n / 2));
    CHECK(sts_run_load(path, &loaded) == STS_RUN_DAMAGED);

    remove(path);
    remove(tmp);
    CHECK(strcmp(sts_run_status_str(STS_RUN_DAMAGED), "save file is damaged") == 0);
}

/* A whole run, saved and reloaded after every step, stays legal in every mode. */
static void test_run_endurance(void)
{
    static const char *const inis[] = {
        "",
        "[reward]\nreplacement=by_type\n",
        "[reward]\nreplacement=random\ncopies_max=4\n",
        "[reward]\nwho_picks=nobody\ncopies_max=4\n",
        "[reward]\nwho_picks=loser\nreplacement=random\n",
        "[deck]\nsize_min=15\nsize_max=30\n[reward]\nmode=add\ncopies_max=4\n",
        "[deck]\nsize_min=15\nsize_max=30\n[reward]\nmode=remove\ncopies_max=3\n",
        "[reward]\nreplacement=random\ntypes=spell\ncopies_min=2\ncopies_max=2\n",
    };
    static sts_run run;
    static char text[STS_RUN_TEXT_MAX];
    sts_settings s;
    sts_rng dice;
    size_t i;
    int c, m, line = 0, violations = 0, stuck = 0;

    sts_rng_seed(&dice, 77, 77);
    for (i = 0; i < sizeof inis / sizeof inis[0]; i++) {
        CHECK(parse_ini(inis[i], &s, &line) == STS_SETTINGS_OK);
        CHECK(sts_settings_validate(&s) == NULL);
        for (c = 0; c < STS_NUM_CHARACTERS; c++) {
            size_t n;
            if (sts_run_start(&run, &g_db, &s, c, (uint64_t)(i * 100 + (size_t)c)) != STS_RUN_OK) stuck++;
            if (finish_draft(&run, &s) != STS_RUN_OK) stuck++;
            for (m = 0; m < 60; m++) {
                sts_reward_outcome out;
                sts_replace_choice ch;
                uint16_t remove_ids[4];
                int k;

                if (sts_run_begin_reward(&run, &g_db, &s, m & 1, &out) != STS_RUN_OK)
                    violations++;

                /* Quit and reload with offers on screen. */
                n = sts_run_serialize(&run, text, sizeof text);
                if (!n || sts_run_parse(text, n, &run) != STS_RUN_OK
                    || sts_run_validate(&run, &g_db, &s) != STS_RUN_OK)
                    violations++;

                if (out == STS_REWARD_OFFERS) {
                    int idx = (int)sts_rng_below(&dice, (uint32_t)run.offer_count);
                    memset(&ch, 0, sizeof ch);
                    if (s.reward.replace == STS_REPLACE_CHOOSE) {
                        ch.remove_ids = remove_ids;
                        ch.remove_count = run.offers[idx].copies;
                        for (k = 0; k < ch.remove_count; k++)
                            remove_ids[k] = run.deck.ids[k];
                    } else if (s.reward.replace == STS_REPLACE_BY_TYPE) {
                        unsigned mask = sts_replace_types_available(&g_db, c, &run.deck, run.offers[idx].copies);
                        ch.type = (mask & 1u) ? 0 : (mask & 2u) ? 1 : 2;
                    }
                    if (sts_run_reward_pick(&run, &g_db, &s, idx, &ch) != STS_RUN_OK)
                        violations++;
                }
                if (sts_deck_validate(&run.deck, &s.rules, NULL) != STS_DECK_OK
                    || !sts_deck_cards_legal(&g_db, c, &run.deck, NULL))
                    violations++;
            }
        }
    }
    CHECK(stuck == 0);
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
    test_offer_apply();
    test_settings();
    test_run_draft();
    test_run_rewards();
    test_run_save();
    test_run_endurance();
    test_real_profile_cards();
    printf("%d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
