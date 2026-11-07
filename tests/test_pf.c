/* Tests: Profile (.pf) files, including optional real files; see test_main.c. */
#include "test_util.h"

static sts_profile g_a, g_b;          /* static: a profile is ~41 KB */
static uint8_t g_buf[64 * 1024];

void test_pf_synthetic_roundtrip(void)
{
    size_t consumed = 0, size, written;
    int character, slot, i;

    memset(&g_a, 0, sizeof g_a);
    for (i = 0; i < STS_PF_HEADER_SIZE; i++) g_a.header[i] = (uint8_t)(i * 3);
    for (character = 0; character < STS_NUM_CHARACTERS; character++)
        for (slot = 0; slot < STS_NUM_SLOTS; slot++)
            fill_legal_deck(&g_a.decks[character][slot], 100 + character);
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
    FILE *file = fopen(path, "rb");
    uint8_t *buffer;
    long count;
    if (!file) return NULL;
    fseek(file, 0, SEEK_END); count = ftell(file); fseek(file, 0, SEEK_SET);
    buffer = (uint8_t *)malloc((size_t)count);
    if (buffer && fread(buffer, 1, (size_t)count, file) != (size_t)count) { free(buffer); buffer = NULL; }
    fclose(file);
    *len = buffer ? (size_t)count : 0;
    return buffer;
}

void test_real_profile(void)
{
    const char *path = getenv("STS_TEST_PF");
    sts_rules rules = sts_rules_vanilla();
    size_t len = 0, consumed = 0, written;
    uint8_t *orig;
    int character, slot, bad = 0;

    if (!path) { printf("SKIP real profile (set STS_TEST_PF)\n"); return; }
    orig = read_all(path, &len);
    CHECK(orig != NULL);
    if (!orig) return;

    CHECK(sts_pf_parse(orig, len, &g_a, &consumed) == STS_PF_OK);
    CHECK(consumed == len);
    for (character = 0; character < STS_NUM_CHARACTERS; character++)
        for (slot = 0; slot < STS_NUM_SLOTS; slot++)
            if (sts_deck_validate(&g_a.decks[character][slot], &rules, NULL) != STS_DECK_OK) bad++;
    CHECK(bad == 0);
    written = sts_pf_serialize(&g_a, g_buf, sizeof g_buf);
    CHECK(written == len && memcmp(g_buf, orig, len) == 0);   /* byte-identical round trip */
    printf("real profile: %zu bytes, %d illegal decks\n", len, bad);
    free(orig);
}

/* Every deck in a real profile must use only cards its character owns. */
void test_real_profile_cards(void)
{
    const char *path = getenv("STS_TEST_PF");
    int character, slot, bad = 0;

    if (!path) { printf("SKIP real profile cards (set STS_TEST_PF)\n"); return; }
    CHECK(sts_pf_load(path, &g_a, NULL) == STS_PF_OK);
    for (character = 0; character < STS_NUM_CHARACTERS; character++)
        for (slot = 0; slot < STS_NUM_SLOTS; slot++)
            if (!sts_deck_cards_legal(&g_db, character, &g_a.decks[character][slot], NULL)) bad++;
    CHECK(bad == 0);
}

void test_randomizer_output(void)
{
    const char *path = getenv("STS_TEST_RANDOM_PF");
    sts_rules rules = sts_rules_vanilla();
    size_t consumed = 0;
    int character, slot, violations = 0;

    if (!path) { printf("SKIP randomizer output (set STS_TEST_RANDOM_PF)\n"); return; }
    CHECK(sts_pf_load(path, &g_a, &consumed) == STS_PF_OK);
    for (character = 0; character < STS_NUM_CHARACTERS; character++) {
        for (slot = 0; slot < STS_NUM_SLOTS; slot++) {
            uint16_t bad = 0;
            sts_deck_status status = sts_deck_validate(&g_a.decks[character][slot], &rules, &bad);
            CHECK(g_a.decks[character][slot].count == 20);
            CHECK(status == STS_DECK_OK || status == STS_DECK_COPY_LIMIT);
            if (status != STS_DECK_OK) {
                violations++;
                printf("  illegal: %s slot %d (%s, card %u x%d)\n", sts_character_name(character), slot + 1,
                       sts_deck_status_str(status), bad, sts_deck_count_of(&g_a.decks[character][slot], bad));
            }
        }
    }
    printf("randomizer output: %d illegal deck(s)\n", violations);
}
