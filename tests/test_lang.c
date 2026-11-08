/* Tests: Translation files, and that the shipped one covers every tr("...") in mod/. */
#include <ctype.h>
#include "test_util.h"
#include "lang.h"

#ifndef STS_LANG_JA_PATH
#define STS_LANG_JA_PATH "data/lang/ja.txt"
#endif
#ifndef STS_MOD_SOURCES
#define STS_MOD_SOURCES ""
#endif

static sts_lang_status parse_lang(const char *text, sts_lang *lang, int *line)
{
    return sts_lang_parse(text, strlen(text), "ja", lang, line);
}

static void test_lang_formats(void)
{
    CHECK(sts_lang_formats_match("plain", "other"));
    CHECK(sts_lang_formats_match("%s: pick %d of %d", "%s - %d / %d"));
    CHECK(sts_lang_formats_match("%dx %.90s", "x%d %.90s"));
    CHECK(sts_lang_formats_match("100%% %d", "%d 100%%"));
    CHECK(!sts_lang_formats_match("%s %d", "%d %s"));
    CHECK(!sts_lang_formats_match("%s %d", "%s"));
    CHECK(!sts_lang_formats_match("%s", "%s %s"));
    CHECK(!sts_lang_formats_match("%.90s", "%s"));
    CHECK(!sts_lang_formats_match("%d", "%%d"));
}

static void test_lang_parse(void)
{
    sts_lang lang;
    int line = 0;

    CHECK(parse_lang("# c\n\nen=Back\nja=BACK\r\nen=%s: pick %d\nja=%s -- %d\n", &lang, &line) == STS_LANG_OK);
    CHECK(lang.count == 2);
    CHECK(strcmp(sts_lang_find(&lang, "Back"), "BACK") == 0);
    CHECK(strcmp(sts_lang_find(&lang, "%s: pick %d"), "%s -- %d") == 0);
    CHECK(sts_lang_find(&lang, "Missing") == NULL);
    sts_lang_free(&lang);

    /* Spaces count, and a byte order mark and a missing last newline are fine. */
    CHECK(parse_lang("\xEF\xBB\xBF" "en=a  b \nja= c", &lang, &line) == STS_LANG_OK);
    CHECK(strcmp(sts_lang_find(&lang, "a  b "), " c") == 0);
    sts_lang_free(&lang);

    /* Only this language's lines count as translations. */
    CHECK(parse_lang("en=a\nfr=b\n", &lang, &line) == STS_LANG_BAD_LINE && line == 2);
    CHECK(parse_lang("junk\n", &lang, &line) == STS_LANG_BAD_LINE && line == 1);
    CHECK(parse_lang("en=a\nen=b\nja=c\n", &lang, &line) == STS_LANG_UNPAIRED && line == 2);
    CHECK(parse_lang("ja=c\n", &lang, &line) == STS_LANG_UNPAIRED && line == 1);
    CHECK(parse_lang("en=a\nja=b\nen=c\n", &lang, &line) == STS_LANG_UNPAIRED);
    CHECK(parse_lang("en=a\nja=b\nen=a\nja=c\n", &lang, &line) == STS_LANG_DUPLICATE && line == 4);
    CHECK(parse_lang("en=%s %d\nja=%d %s\n", &lang, &line) == STS_LANG_BAD_FORMAT && line == 2);
    CHECK(lang.count == 0 && lang.text == NULL);
}

/* The next tr("...") literal in the text after *cursor, copied into out. 0 when there are no more. */
static int next_tr_literal(const char **cursor, char *out, size_t size)
{
    const char *start = strstr(*cursor, "tr(\"");
    const char *end;

    /* tr( preceded by a letter or '_' is a longer name, such as sts_lang_status_str( */
    while (start && start > *cursor && (isalnum((unsigned char)start[-1]) || start[-1] == '_'))
        start = strstr(start + 1, "tr(\"");
    if (!start)
        return 0;
    start += 4;
    end = strstr(start, "\")");
    if (!end || (size_t)(end - start) >= size)
        return 0;
    memcpy(out, start, (size_t)(end - start));
    out[end - start] = 0;
    /* The translation file holds the text itself, where the source writes a quote as \". */
    {
        char *from = out, *to = out;

        for (; *from; from++) {
            if (from[0] == '\\' && from[1] == '"')
                from++;
            *to++ = *from;
        }
        *to = 0;
    }
    *cursor = end + 2;
    return 1;
}

static void test_lang_shipped(void)
{
    sts_lang lang;
    size_t length = 0;
    char *text = sts_read_file(STS_LANG_JA_PATH, &length);
    char sources[2048];
    char *path;
    int line = 0, character, found = 0, wau;
    int *used;

    CHECK(text != NULL);
    if (!text)
        return;
    CHECK(sts_lang_parse(text, length, "ja", &lang, &line) == STS_LANG_OK);
    free(text);
    used = (int *)calloc((size_t)lang.count + 1, sizeof *used);

    /* Every character has a name. */
    for (character = 0; character < STS_NUM_CHARACTERS; character++) {
        const char *name = sts_character_name(character);
        CHECK(sts_lang_find(&lang, name) != NULL);
        for (wau = 0; wau < lang.count; wau++)
            if (strcmp(lang.from[wau], name) == 0)
                used[wau] = 1;
    }

    /* Every tr("...") in the mod has a translation, and every translation is still used. */
    CHECK(strlen(STS_MOD_SOURCES) > 0 && strlen(STS_MOD_SOURCES) < sizeof sources);
    strncpy(sources, STS_MOD_SOURCES, sizeof sources - 1);
    sources[sizeof sources - 1] = 0;
    for (path = strtok(sources, "|"); path; path = strtok(NULL, "|")) {
        char *source = sts_read_file(path, &length);
        const char *cursor = source;
        char literal[256];

        CHECK(source != NULL);
        if (!source)
            continue;
        while (next_tr_literal(&cursor, literal, sizeof literal)) {
            int known = 0;
            found++;
            for (wau = 0; wau < lang.count; wau++) {
                if (strcmp(lang.from[wau], literal) == 0) {
                    used[wau] = 1;
                    known = 1;
                }
            }
            if (!known)
                printf("ja.txt has no translation for \"%s\" (%s)\n", literal, path);
            CHECK(known);
        }
        free(source);
    }
    CHECK(found > 50);
    for (wau = 0; wau < lang.count; wau++) {
        if (!used[wau])
            printf("ja.txt translates \"%s\", which the mod no longer uses\n", lang.from[wau]);
        CHECK(used[wau]);
    }
    free(used);
    sts_lang_free(&lang);
}

void test_lang(void)
{
    test_lang_formats();
    test_lang_parse();
    test_lang_shipped();
}
