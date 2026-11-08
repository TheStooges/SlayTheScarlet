/* Translations of the mod's own text. The game's text (card names and descriptions) needs none:
 * it is read from the game in whatever language it runs in.
 *
 * A translation file pairs each English line, as written in the source, with its translation:
 *
 *     # comment
 *     en=%s: pick %d of %d
 *     ja=(the translation)
 *
 * Everything after the '=' counts, spaces included. A line with %d, %s and so on is a format,
 * filled in by sprintf, so its translation must have exactly the same ones in the same order,
 * or the game could crash. Such a file is refused. Translations can move words around the
 * blanks but not reorder the blanks themselves. Lines are matched as bytes, so the file must be
 * in the game's own encoding by the time it is parsed (the mod converts it from UTF-8).
 */
#ifndef STS_LANG_H
#define STS_LANG_H

#include <stddef.h>

typedef struct {
    char *text;            /* owns the strings below */
    const char **from;     /* English */
    const char **to;       /* the translation */
    int count;
} sts_lang;

typedef enum {
    STS_LANG_OK = 0,
    STS_LANG_BAD_LINE,     /* not a comment, blank, en= or the language's own line */
    STS_LANG_UNPAIRED,     /* an en= line without its translation next, or the reverse */
    STS_LANG_BAD_FORMAT,   /* the translation's %d/%s blanks differ from the English */
    STS_LANG_DUPLICATE,    /* the same English line twice */
    STS_LANG_NO_MEMORY
} sts_lang_status;

/* Parses a file for language `code` (such as "ja"). On error nothing is kept and *err_line (if
 * non-NULL) receives the 1-based line number. Free a parsed table with sts_lang_free. */
sts_lang_status sts_lang_parse(const char *text, size_t len, const char *code, sts_lang *out, int *err_line);

void sts_lang_free(sts_lang *lang);

/* The translation of an English line, or NULL if the file has none. */
const char *sts_lang_find(const sts_lang *lang, const char *english);

const char *sts_lang_status_str(sts_lang_status status);

/* 1 if both formats have the same % conversions in the same order ("%%" is not one). */
int sts_lang_formats_match(const char *first, const char *second);

#endif
