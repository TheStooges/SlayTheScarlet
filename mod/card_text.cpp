/* Card names and descriptions, read from the game's own files, so they are in
 * whatever language the player launched with. */
#include <stdio.h>
#include <string.h>
#include "internal.h"

static card_text g_texts[MAX_CARD_TEXT];
static int g_text_count = 0;

/* Removes the game's <a> marker and anything else after a '<' in a card name. */
static void clean_name(char *name)
{
    char *tag = strchr(name, '<');
    size_t length;

    if (tag)
        *tag = 0;
    length = strlen(name);
    while (length > 0 && name[length - 1] == ' ')
        name[--length] = 0;
}

static void load_text_file(const char *path)
{
    SokuLib::CSVParser parser(path);

    for (auto &line : parser.data) {
        card_text *entry;
        int i = 0;

        if (g_text_count >= MAX_CARD_TEXT)
            return;
        entry = &g_texts[g_text_count];
        memset(entry, 0, sizeof *entry);
        for (auto &cell : line) {
            const char *cell_text = (const char *)cell;
            if (i == 0)
                entry->id = (uint16_t)atoi(cell_text);
            else if (i == 1)
                strncpy(entry->name, cell_text, NAME_SIZE - 1);
            else if (i == 4)
                strncpy(entry->desc, cell_text, DESC_SIZE - 1);
            i++;
        }
        clean_name(entry->name);
        g_text_count++;
    }
}

void load_card_text(int character)
{
    char path[128];

    g_text_count = 0;
    load_text_file("data/csv/common/spellcard.csv");
    sprintf(path, "data/csv/%.64s/spellcard.csv", SokuLib::getCharName((unsigned)character));
    load_text_file(path);
}

const card_text *find_text(uint16_t id)
{
    int i;

    for (i = 0; i < g_text_count; i++)
        if (g_texts[i].id == id)
            return &g_texts[i];
    return NULL;
}
