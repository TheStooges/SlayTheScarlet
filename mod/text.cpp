/* The mod's own text in the game's language. The game's text is Japanese unless the English
 * translation (th123e) replaced its data files, so the card names tell which one is running.
 * Translations are written in data/lang/<code>.txt, in UTF-8, built into the DLL (embedded.cpp),
 * and converted to the game's Shift-JIS when loaded. Without a translation, text stays in English. */
#include <stdio.h>
#include <stdlib.h>
#include "internal.h"

extern "C" {
#include "lang.h"
#include "util.h"
}

#define SHIFT_JIS_CODE_PAGE 932

static sts_lang g_lang;
static int g_lang_loaded = 0;
static int g_japanese = 0;

/* 1 if any card name in the game's shared card table is not plain ASCII. */
static int game_text_is_japanese(void)
{
    SokuLib::CSVParser parser("data/csv/common/spellcard.csv");

    for (auto &line : parser.data) {
        int column = 0;

        for (auto &cell : line) {
            if (column++ == 1) {
                const unsigned char *name = (const unsigned char *)(const char *)cell;
                for (; *name; name++)
                    if (*name >= 0x80)
                        return 1;
            }
        }
    }
    return 0;
}

/* UTF-8 to the game's code page. Returns a malloc'd, NUL-terminated buffer, or NULL. */
static char *to_game_encoding(const char *text, size_t length, size_t *out_length)
{
    int wide_length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, (int)length, NULL, 0);
    wchar_t *wide;
    char *out;
    int out_size;

    if (wide_length <= 0)
        return NULL;
    wide = (wchar_t *)malloc(wide_length * sizeof *wide);
    if (!wide)
        return NULL;
    MultiByteToWideChar(CP_UTF8, 0, text, (int)length, wide, wide_length);
    out_size = WideCharToMultiByte(SHIFT_JIS_CODE_PAGE, 0, wide, wide_length, NULL, 0, NULL, NULL);
    out = out_size > 0 ? (char *)malloc(out_size + 1) : NULL;
    if (out) {
        WideCharToMultiByte(SHIFT_JIS_CODE_PAGE, 0, wide, wide_length, out, out_size, NULL, NULL);
        out[out_size] = 0;
        *out_length = (size_t)out_size;
    }
    free(wide);
    return out;
}

static void load_language(void)
{
    const char *code = "ja";
    size_t length = 0, converted_length = 0;
    const char *text;
    char *converted;
    sts_lang_status status;
    int line = 0;

    g_lang_loaded = 1;
    g_japanese = game_text_is_japanese();
    if (!g_japanese) {
        slog("game text is English, so the mod's text is too");
        return;
    }
    text = embedded_data("LANG_JA", &length);
    if (!text) {
        slog("game text is Japanese, but the DLL has no lang\\%s.txt: using English", code);
        return;
    }
    converted = to_game_encoding(text, length, &converted_length);
    if (!converted) {
        slog("lang\\%s.txt is not valid UTF-8: using English", code);
        return;
    }
    status = sts_lang_parse(converted, converted_length, code, &g_lang, &line);
    free(converted);
    if (status != STS_LANG_OK)
        slog("lang\\%s.txt line %d: %s. Using English", code, line, sts_lang_status_str(status));
    else
        slog("using lang\\%s.txt, %d lines", code, g_lang.count);
}

const char *tr(const char *english)
{
    const char *found;

    if (!g_lang_loaded)
        load_language();
    found = sts_lang_find(&g_lang, english);
    return found ? found : english;
}

int game_is_japanese(void)
{
    if (!g_lang_loaded)
        load_language();
    return g_japanese;
}

const char *character_label(int character)
{
    return tr(sts_character_name(character));
}
