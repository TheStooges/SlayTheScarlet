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
#include "pf.h"

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

    CHECK(sts_card_type_of(0) == STS_CARD_SYSTEM && sts_card_type_of(20) == STS_CARD_SYSTEM);
    CHECK(sts_card_type_of(100) == STS_CARD_SKILL && sts_card_type_of(114) == STS_CARD_SKILL);
    CHECK(sts_card_type_of(200) == STS_CARD_SPELL && sts_card_type_of(219) == STS_CARD_SPELL);
    CHECK(sts_card_type_of(50) == STS_CARD_UNKNOWN);
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
    printf("%d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
