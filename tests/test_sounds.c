/* Tests: the card sound table. */
#include "test_util.h"
#include "sounds.h"

#ifndef STS_CARD_SOUNDS_PATH
#define STS_CARD_SOUNDS_PATH "data/card_sounds.csv"
#endif

static sts_sound_table g_sounds;

static sts_sounds_status parse_text(const char *text, int *err_line)
{
    return sts_sounds_parse(text, strlen(text), &g_sounds, err_line);
}

void test_sounds_parse(void)
{
    const sts_sound_row *row;
    int err = 0;

    /* Comments, blank lines, CRLF endings, extra spaces and a missing final newline are all fine. */
    CHECK(parse_text("# comment\n\r\nsystem,0,common/54@0\r\nReimu,200,reimu/6@13  common/23@0 reimu/6@13\n"
                     "Reisen,110,udonge/18@13", &err) == STS_SOUNDS_OK);
    CHECK(g_sounds.row_count == 3 && g_sounds.play_count == 5);
    row = sts_sounds_find(&g_sounds, STS_SOUND_SYSTEM, 0);
    CHECK(row && row->outcomes == 1 && row->count[0] == 1 && g_sounds.plays[row->first[0]].folder == STS_SOUND_COMMON
          && g_sounds.plays[row->first[0]].number == 54 && g_sounds.plays[row->first[0]].frame == 0);
    /* A sound played twice is kept twice, in the file's order. */
    row = sts_sounds_find(&g_sounds, 0, 200);
    CHECK(row && row->count[0] == 3 && g_sounds.plays[row->first[0]].folder == 0 && g_sounds.plays[row->first[0]].frame == 13
          && g_sounds.plays[row->first[0] + 1].folder == STS_SOUND_COMMON && g_sounds.plays[row->first[0] + 2].number == 6);
    /* Reisen's folder is the game's, udonge. */
    row = sts_sounds_find(&g_sounds, 10, 110);
    CHECK(row && g_sounds.plays[row->first[0]].folder == 10 && g_sounds.plays[row->first[0]].number == 18);
    CHECK(sts_sounds_find(&g_sounds, 1, 200) == NULL && sts_sounds_find(&g_sounds, 0, 0) == NULL);

    /* Each kind of damage is reported, on the right line. */
    CHECK(parse_text("system,0,common/54@0\nReimu,200\n", &err) == STS_SOUNDS_BAD_LINE && err == 2);
    CHECK(parse_text("Reimu,200,reimu/6@13,x\n", &err) == STS_SOUNDS_BAD_LINE);
    CHECK(parse_text("Nobody,200,reimu/6@13\n", &err) == STS_SOUNDS_BAD_LINE);
    CHECK(parse_text("Reimu,2x0,reimu/6@13\n", &err) == STS_SOUNDS_BAD_LINE);
    CHECK(parse_text("Reimu,200,reisen/6@13\n", &err) == STS_SOUNDS_BAD_LINE);
    CHECK(parse_text("Reimu,200,reimu/6\n", &err) == STS_SOUNDS_BAD_LINE);
    CHECK(parse_text("Reimu,200,reimu6@13\n", &err) == STS_SOUNDS_BAD_LINE);
    CHECK(parse_text("Reimu,200,reimu/@13\n", &err) == STS_SOUNDS_BAD_LINE);
    CHECK(parse_text("Reimu,200,reimu/6@-1\n", &err) == STS_SOUNDS_BAD_LINE);
    CHECK(parse_text("Reimu,200,reimu/1000@0\n", &err) == STS_SOUNDS_BAD_LINE);
    CHECK(parse_text("Reimu,200, \n", &err) == STS_SOUNDS_BAD_LINE);
    CHECK(parse_text("Reimu,200,reimu/6@13\n\nReimu,200,reimu/7@1\n", &err) == STS_SOUNDS_DUPLICATE && err == 3);

    /* Outcomes: each a list of its own, with or without spaces around the '>'. */
    CHECK(parse_text("Sanae,101,sanae/30@12 sanae/31@90 > sanae/30@12 sanae/32@90>sanae/33@90\n", &err)
          == STS_SOUNDS_OK);
    row = sts_sounds_find(&g_sounds, 15, 101);
    CHECK(row && row->outcomes == 3 && row->count[0] == 2 && row->count[1] == 2 && row->count[2] == 1);
    CHECK(row && g_sounds.plays[row->first[1] + 1].number == 32 && g_sounds.plays[row->first[2]].number == 33);
    /* Every outcome needs a sound, there is a limit to how many there are, and '|' is not a separator. */
    CHECK(parse_text("Reisen,209,udonge/54@86 >\n", &err) == STS_SOUNDS_BAD_LINE);
    CHECK(parse_text("Reisen,209,> udonge/54@86\n", &err) == STS_SOUNDS_BAD_LINE);
    CHECK(parse_text("Reisen,209,udonge/54@86 >> udonge/56@132\n", &err) == STS_SOUNDS_BAD_LINE);
    CHECK(parse_text("Reisen,209,udonge/54@86 | udonge/56@132\n", &err) == STS_SOUNDS_BAD_LINE);
    CHECK(parse_text("Sanae,101,sanae/1@1>sanae/2@1>sanae/3@1>sanae/4@1>sanae/5@1>sanae/6@1>sanae/7@1>sanae/8@1>sanae/9@1\n",
                     &err) == STS_SOUNDS_OVERFLOW);

    CHECK(strcmp(sts_sound_folder_name(STS_SOUND_COMMON), "common") == 0);
    CHECK(strcmp(sts_sound_folder_name(16), "chirno") == 0 && sts_sound_folder_name(20) == NULL);
}

/* The real table: every row is a card in the card table, and every card has a row. */
void test_sounds_file(void)
{
    size_t size = 0;
    char *text = sts_read_file(STS_CARD_SOUNDS_PATH, &size);
    int err = 0, wau, character, unknown = 0, missing = 0;

    CHECK(text != NULL);
    if (!text)
        return;
    CHECK(sts_sounds_parse(text, size, &g_sounds, &err) == STS_SOUNDS_OK);
    free(text);
    for (wau = 0; wau < g_sounds.row_count; wau++) {
        const sts_sound_row *row = &g_sounds.rows[wau];
        const sts_card *card = sts_card_db_find(&g_db, row->owner == STS_SOUND_SYSTEM ? 0 : row->owner, row->id);

        if (!card || (row->owner == STS_SOUND_SYSTEM) != (card->type == STS_CARD_SYSTEM)) {
            printf("card_sounds.csv: %d,%d is not a card\n", row->owner, row->id);
            unknown++;
        }
    }
    CHECK(unknown == 0);
    for (wau = 0; wau < g_db.system.count; wau++)
        missing += !sts_sounds_find(&g_sounds, STS_SOUND_SYSTEM, g_db.system.cards[wau].id);
    for (character = 0; character < STS_NUM_CHARACTERS; character++)
        for (wau = 0; wau < g_db.characters[character].count; wau++)
            missing += !sts_sounds_find(&g_sounds, character, g_db.characters[character].cards[wau].id);
    CHECK(missing == 0);
}
