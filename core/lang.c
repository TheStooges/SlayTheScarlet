#include <stdlib.h>
#include <string.h>
#include "lang.h"

/* Moves *cursor to the next % conversion and returns its length, such as 4 for "%.9s", or 0 at
 * the end of the text. "%%" is skipped, as it prints a percent sign rather than taking a value. */
static size_t next_conversion(const char **cursor)
{
    const char *text = *cursor;

    for (;;) {
        size_t length;

        text = strchr(text, '%');
        if (!text) {
            *cursor = NULL;
            return 0;
        }
        if (text[1] == '%') {
            text += 2;
            continue;
        }
        length = 1;
        while (text[length] && strchr("-+ #0123456789.*hlLzjt", text[length]))
            length++;
        if (text[length])
            length++;   /* the conversion letter */
        *cursor = text;
        return length;
    }
}

int sts_lang_formats_match(const char *first, const char *second)
{
    for (;;) {
        size_t first_length = next_conversion(&first);
        size_t second_length = next_conversion(&second);

        if (first_length != second_length)
            return 0;
        if (first_length == 0)
            return 1;
        if (memcmp(first, second, first_length) != 0)
            return 0;
        first += first_length;
        second += second_length;
    }
}

void sts_lang_free(sts_lang *lang)
{
    free(lang->text);
    free((void *)lang->from);
    free((void *)lang->to);
    memset(lang, 0, sizeof *lang);
}

/* Returns the value if line is "<key>=value", else NULL. */
static const char *value_of(const char *line, const char *key)
{
    size_t length = strlen(key);

    if (strncmp(line, key, length) == 0 && line[length] == '=')
        return line + length + 1;
    return NULL;
}

sts_lang_status sts_lang_parse(const char *text, size_t len, const char *code, sts_lang *out, int *err_line)
{
    sts_lang lang;
    size_t pos = 0, lines = 1, wau;
    const char *pending = NULL;
    int line_no = 0;
    sts_lang_status status = STS_LANG_OK;

    memset(out, 0, sizeof *out);
    memset(&lang, 0, sizeof lang);
    if (err_line)
        *err_line = 0;
    for (wau = 0; wau < len; wau++)
        lines += text[wau] == '\n';

    lang.text = (char *)malloc(len + 1);
    lang.from = (const char **)malloc(lines * sizeof *lang.from);
    lang.to = (const char **)malloc(lines * sizeof *lang.to);
    if (!lang.text || !lang.from || !lang.to) {
        sts_lang_free(&lang);
        return STS_LANG_NO_MEMORY;
    }
    memcpy(lang.text, text, len);
    lang.text[len] = 0;

    /* A UTF-8 byte order mark, as some editors write, is not part of the first line. */
    if (len >= 3 && memcmp(lang.text, "\xEF\xBB\xBF", 3) == 0)
        pos = 3;

    while (pos <= len && status == STS_LANG_OK) {
        char *line = lang.text + pos;
        char *end = strchr(line, '\n');
        const char *value;

        if (end)
            *end = 0;
        pos = end ? (size_t)(end - lang.text) + 1 : len + 1;
        line_no++;
        if (*line && line[strlen(line) - 1] == '\r')
            line[strlen(line) - 1] = 0;

        if (*line == 0 || *line == '#')
            continue;
        if ((value = value_of(line, "en")) != NULL) {
            if (pending)
                status = STS_LANG_UNPAIRED;
            pending = value;
        } else if ((value = value_of(line, code)) != NULL) {
            if (!pending) {
                status = STS_LANG_UNPAIRED;
            } else if (!sts_lang_formats_match(pending, value)) {
                status = STS_LANG_BAD_FORMAT;
            } else if (sts_lang_find(&lang, pending)) {
                status = STS_LANG_DUPLICATE;
            } else {
                lang.from[lang.count] = pending;
                lang.to[lang.count] = value;
                lang.count++;
                pending = NULL;
            }
        } else {
            status = STS_LANG_BAD_LINE;
        }
    }
    if (status == STS_LANG_OK && pending) {
        status = STS_LANG_UNPAIRED;
        line_no++;
    }
    if (status != STS_LANG_OK) {
        if (err_line)
            *err_line = line_no;
        sts_lang_free(&lang);
        return status;
    }
    *out = lang;
    return STS_LANG_OK;
}

const char *sts_lang_find(const sts_lang *lang, const char *english)
{
    int wau;

    for (wau = 0; wau < lang->count; wau++)
        if (strcmp(lang->from[wau], english) == 0)
            return lang->to[wau];
    return NULL;
}

const char *sts_lang_status_str(sts_lang_status status)
{
    switch (status) {
    case STS_LANG_OK:         return "ok";
    case STS_LANG_BAD_LINE:   return "bad line";
    case STS_LANG_UNPAIRED:   return "a line without its pair";
    case STS_LANG_BAD_FORMAT: return "the translation's blanks (%d, %s) differ from the English";
    case STS_LANG_DUPLICATE:  return "the same English line twice";
    case STS_LANG_NO_MEMORY:  return "out of memory";
    }
    return "unknown";
}
