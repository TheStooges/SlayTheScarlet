/* The data a run is built on: the card table, the settings and the run saves. */
#include <stdio.h>
#include <string.h>
#include "internal.h"

char g_dir[MAX_PATH];
int g_data_ok = 0;
sts_card_db g_db;
sts_settings g_settings;
sts_run g_run;

void screen_set_dir(const char *dir)
{
    strncpy(g_dir, dir, sizeof g_dir - 1);
}

/* A save of the selected player's, in their profile's folder. */
void run_path(char *out, size_t size, int character)
{
    char folder[MAX_PATH + 80];

    player_runs_folder(folder, sizeof folder);
    _snprintf(out, size - 1, "%srun_%02d.sav", folder, character);
    out[size - 1] = 0;
}

/* Whether a save file is there at all, even a damaged one that will not load. */
int run_file_exists(int character)
{
    char path[MAX_PATH + 96];

    run_path(path, sizeof path, character);
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

int load_data(void)
{
    char path[MAX_PATH + 96];
    int line = 0;
    sts_db_status db = STS_DB_IO;
    sts_settings_status status;
    const char *bad;
    const char *cards;
    size_t cards_length;

    if (g_data_ok)
        return 1;
    cards = embedded_data("CARDS_CSV", &cards_length);
    if (cards)
        db = sts_card_db_parse(cards, cards_length, &g_db, &line);
    if (db != STS_DB_OK) {
        set_error("cards.csv", sts_db_status_str(db));
        return 0;
    }
    sprintf(path, "%ssettings.ini", g_dir);
    status = sts_settings_load(path, &g_settings, &line);
    if (status != STS_SETTINGS_OK && status != STS_SETTINGS_IO) {   /* no file just means defaults */
        char where[64];
        sprintf(where, tr("settings.ini line %d"), line);
        set_error(where, sts_settings_status_str(status));
        return 0;
    }
    bad = sts_settings_validate(&g_settings);
    if (!bad)
        bad = offers_off_screen(&g_settings);
    if (bad) {
        set_error("settings.ini", bad);
        return 0;
    }
    /* Every run plays against opponents without the mod until full mode exists. */
    {
        uint32_t before = sts_settings_hash(&g_settings);

        sts_settings_force_compatible(&g_settings);
        if (sts_settings_hash(&g_settings) != before)
            slog("settings.ini asks for rules that need full mode, using compatible rules instead");
    }
    bad = sts_run_check_starters(&g_db, &g_settings);
    if (bad) {
        set_error("settings.ini", bad);
        return 0;
    }
    g_data_ok = 1;
    return 1;
}

void save_run(void)
{
    char path[MAX_PATH + 96];
    sts_run_status status;

    /* Nothing is in use until a run has been saved, so this is the first write to the folders. If
     * they already exist this fails harmlessly, and a real problem shows up in the save. */
    sprintf(path, "%s" RUNS_FOLDER, g_dir);
    CreateDirectoryA(path, NULL);
    player_runs_folder(path, sizeof path);
    CreateDirectoryA(path, NULL);

    run_path(path, sizeof path, g_run.character);
    status = sts_run_save(path, &g_run);
    if (status != STS_RUN_OK)
        set_error(tr("Could not save the run"), sts_run_status_str(status));
    /* The offers on screen have been found, for the gallery. Every new set of offers is saved before it
     * shows, so none is missed. */
    note_found_offers(&g_run);
}
