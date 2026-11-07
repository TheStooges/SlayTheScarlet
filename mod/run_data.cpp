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
    InitializeCriticalSection(&g_log_lock);
    strncpy(g_dir, dir, sizeof g_dir - 1);
}

void run_path(char *out, size_t size, int character)
{
    _snprintf(out, size - 1, "%srun_%02d.sav", g_dir, character);
    out[size - 1] = 0;
}

int load_data(void)
{
    char path[MAX_PATH + 32];
    int line = 0;
    sts_db_status db;
    sts_settings_status status;
    const char *bad;

    if (g_data_ok)
        return 1;
    sprintf(path, "%scards.csv", g_dir);
    db = sts_card_db_load(path, &g_db, &line);
    if (db != STS_DB_OK) {
        set_error("cards.csv", sts_db_status_str(db));
        return 0;
    }
    sprintf(path, "%ssettings.ini", g_dir);
    status = sts_settings_load(path, &g_settings, &line);
    if (status != STS_SETTINGS_OK && status != STS_SETTINGS_IO) {   /* no file just means defaults */
        char where[64];
        sprintf(where, "settings.ini line %d", line);
        set_error(where, sts_settings_status_str(status));
        return 0;
    }
    bad = sts_settings_validate(&g_settings);
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
    g_data_ok = 1;
    return 1;
}

void save_run(void)
{
    char path[MAX_PATH + 32];
    sts_run_status status;

    run_path(path, sizeof path, g_run.character);
    status = sts_run_save(path, &g_run);
    if (status != STS_RUN_OK)
        set_error("Could not save the run", sts_run_status_str(status));
}
