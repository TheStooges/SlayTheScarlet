/* Tests: The card table. */
#include "test_util.h"

static sts_card_db g_db2;
static char g_text[8 * 1024];

/* Writes the smallest complete table: one system card and one spell per character. */
static size_t minimal_db_text(char *out)
{
    size_t count = 0;
    int character;
    count += (size_t)sprintf(out + count, "system,0,system,1\n");
    for (character = 0; character < STS_NUM_CHARACTERS; character++)
        count += (size_t)sprintf(out + count, "%s,200,spell,3\n", sts_character_name(character));
    return count;
}

/* Parses the minimal table plus one extra line. Returns the status. */
static sts_db_status parse_with_extra(const char *extra, int *err_line)
{
    size_t count = minimal_db_text(g_text);
    count += (size_t)sprintf(g_text + count, "%s", extra);
    return sts_card_db_parse(g_text, count, &g_db2, err_line);
}

void test_card_db_parse(void)
{
    int err = 0, i;
    size_t count;

    /* Comments, blank lines, CRLF endings and a missing final newline are all fine. */
    count = minimal_db_text(g_text);
    count += (size_t)sprintf(g_text + count, "# comment\n\r\n\nReimu,100,skill,2\r\nCirno,213,spell,5");
    CHECK(sts_card_db_parse(g_text, count, &g_db2, &err) == STS_DB_OK);
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
    count = minimal_db_text(g_text);
    for (i = 0; i < STS_POOL_MAX_CARDS; i++)
        count += (size_t)sprintf(g_text + count, "Reimu,%d,spell,1\n", 300 + i);
    CHECK(sts_card_db_parse(g_text, count, &g_db2, &err) == STS_DB_OVERFLOW);

    /* A table missing a character or the system list is rejected. */
    count = (size_t)sprintf(g_text, "system,0,system,1\nReimu,100,skill,0\n");
    CHECK(sts_card_db_parse(g_text, count, &g_db2, &err) == STS_DB_INCOMPLETE);
    CHECK(sts_card_db_parse("", 0, &g_db2, &err) == STS_DB_INCOMPLETE);

    CHECK(sts_card_db_load("no/such/file.csv", &g_db2, &err) == STS_DB_IO);
    CHECK(strcmp(sts_db_status_str(STS_DB_DUPLICATE), "duplicate card") == 0);
}

static int count_type(const sts_card_list *list, int type)
{
    int i, count = 0;
    for (i = 0; i < list->count; i++)
        if (list->cards[i].type == type) count++;
    return count;
}

/* Checks data/cards.csv against what was read from the game. */
void test_card_db_file(void)
{
    int err = 0, character, total = 0;
    sts_db_status status = sts_card_db_load(STS_CARDS_PATH, &g_db, &err);
    sts_deck deck;
    uint16_t bad = 0;

    if (status != STS_DB_OK)
        printf("cards file: %s (line %d)\n", sts_db_status_str(status), err);
    CHECK(status == STS_DB_OK);
    if (status != STS_DB_OK) return;

    CHECK(g_db.system.count == 21);
    CHECK(count_type(&g_db.system, STS_CARD_SYSTEM) == 21);
    total = g_db.system.count;
    for (character = 0; character < STS_NUM_CHARACTERS; character++) {
        const sts_card_list *list = &g_db.characters[character];
        total += list->count;
        CHECK(count_type(list, STS_CARD_SYSTEM) == 0);
        CHECK(count_type(list, STS_CARD_SKILL) == (character == 4 ? 15 : 12));  /* Patchouli has 15 */
        CHECK(count_type(list, STS_CARD_SPELL) >= 9 && count_type(list, STS_CARD_SPELL) <= 14);
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

    /* Skill notation, from the community's names for Patchouli's skills. */
    {
        static const struct { int character; uint16_t id; const char *label; } known[] = {
            {4, 100, "d236"},   /* Summer Red */
            {4, 102, "d623"},   /* Spring Wind */
            {4, 103, "d214"},   /* Autumn Edge */
            {4, 104, "d421"},   /* Dew Spear */
            {4, 101, "d22"},    /* Winter Element */
            {4, 114, "a421"},   /* Diamond Hardness */
            {5, 101, "d623"},   /* Youmu: Crescent Moon Slash */
            {12, 100, "d236"},  /* Komachi: Spirits of the Firm */
        };
        char label[STS_SKILL_LABEL_SIZE];
        size_t k;
        int i;

        for (k = 0; k < sizeof known / sizeof known[0]; k++) {
            CHECK(sts_skill_label(&g_db, known[k].character, known[k].id, label) == 1);
            CHECK(strcmp(label, known[k].label) == 0);
        }

        /* Every skill has a label. Each character has one default per input, and as many
         * alternates per default as the game gives (two). */
        for (character = 0; character < STS_NUM_CHARACTERS; character++) {
            const sts_card_list *list = &g_db.characters[character];
            int defaults = 0, alternates = 0;
            for (i = 0; i < list->count; i++) {
                if (list->cards[i].type != STS_CARD_SKILL) continue;
                CHECK(sts_skill_label(&g_db, character, list->cards[i].id, label) == 1);
                if (label[0] == 'd') defaults++;
                if (label[0] == 'a') alternates++;
            }
            CHECK(defaults == (character == 4 ? 5 : 4));
            CHECK(alternates == defaults * 2);
        }

        /* Anything that is not one of this character's skills has no label. */
        CHECK(sts_skill_label(&g_db, 0, 200, label) == 0 && label[0] == 0);   /* a spell */
        CHECK(sts_skill_label(&g_db, 0, 0, label) == 0 && label[0] == 0);     /* a system card */
        CHECK(sts_skill_label(&g_db, 0, 112, label) == 0 && label[0] == 0);   /* only Patchouli's */
        CHECK(sts_skill_label(&g_db, -1, 100, label) == 0 && label[0] == 0);
    }

    /* A skill family shares one slot value across its upgrades. */
    CHECK(sts_card_db_find(&g_db, 1, 100)->cost_or_slot == sts_card_db_find(&g_db, 1, 104)->cost_or_slot);
    CHECK(sts_card_db_find(&g_db, 1, 100)->cost_or_slot == sts_card_db_find(&g_db, 1, 108)->cost_or_slot);

    sts_deck_clear(&deck);
    sts_deck_add(&deck, 0);
    sts_deck_add(&deck, 100);
    sts_deck_add(&deck, 213);
    CHECK(sts_deck_cards_legal(&g_db, 4, &deck, &bad));
    CHECK(!sts_deck_cards_legal(&g_db, 0, &deck, &bad) && bad == 213);
}
