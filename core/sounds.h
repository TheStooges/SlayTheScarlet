/* The sounds each card makes in a match, loaded from data/card_sounds.csv, for the offer screens.
 * Only numbers: the sounds themselves are the game's files, data/se/<folder>/NNN.wav for a
 * character's and data/se/NNN.wav for the shared ones. */
#ifndef STS_SOUNDS_H
#define STS_SOUNDS_H

#include <stddef.h>
#include <stdint.h>

/* The folder of a shared sound, and the owner of a system card. Characters are 0 to 19. */
#define STS_SOUND_COMMON (-1)
#define STS_SOUND_SYSTEM (-1)

/* The table has a row for each of the game's 482 cards, with 1,917 plays in all, at most 105 in one
 * row. These leave room. */
#define STS_SOUNDS_MAX_ROWS 600
#define STS_SOUNDS_MAX_PLAYS 6000
/* A row's line can be long (a card that plays one sound many times). */
#define STS_SOUNDS_MAX_LINE 4096

/* A card whose sounds vary lists each outcome in full, separated by '>'. They come in turn, one each
 * time the card is used (Reisen's Elixir explodes on every fourth use; Sanae's Omikuji Bomb goes
 * through its four fortunes). */
#define STS_SOUNDS_MAX_OUTCOMES 8

/* The card-use sound of skills and system cards, and the spell card declaration: what a card with no
 * row plays. */
#define STS_SOUND_CARD_USED 54
#define STS_SOUND_SPELL_DECLARED 23

typedef struct {
    int8_t folder;      /* STS_SOUND_COMMON or a character */
    uint16_t number;    /* the file's number */
    uint16_t frame;     /* frames after the card's move starts */
} sts_sound_play;

typedef struct {
    int8_t owner;       /* STS_SOUND_SYSTEM or a character */
    uint16_t id;
    uint8_t outcomes;   /* 1, or how many ways the card can sound, in turn */
    uint16_t first[STS_SOUNDS_MAX_OUTCOMES];   /* each outcome's plays in the table's list, in order */
    uint16_t count[STS_SOUNDS_MAX_OUTCOMES];
} sts_sound_row;

typedef struct {
    sts_sound_row rows[STS_SOUNDS_MAX_ROWS];
    int row_count;
    sts_sound_play plays[STS_SOUNDS_MAX_PLAYS];
    int play_count;
} sts_sound_table;

typedef enum {
    STS_SOUNDS_OK = 0,
    STS_SOUNDS_BAD_LINE,    /* wrong field count, unknown owner or folder, bad number, no plays */
    STS_SOUNDS_DUPLICATE,   /* the same card twice */
    STS_SOUNDS_OVERFLOW     /* more rows, plays, or outcomes than the table holds, or a line too long */
} sts_sounds_status;

/* Parses the file's text: owner,id,sounds, the sounds being folder/number@frame separated by
 * spaces, and outcomes separated by '>'. Every outcome needs at least one sound. Blank lines and
 * lines starting with '#' are ignored. On an error, *err_line (if non-NULL) receives the 1-based line
 * number. */
sts_sounds_status sts_sounds_parse(const char *text, size_t len, sts_sound_table *out, int *err_line);

const char *sts_sounds_status_str(sts_sounds_status status);

/* A card's row, or NULL if it has none. A system card's owner is STS_SOUND_SYSTEM. */
const sts_sound_row *sts_sounds_find(const sts_sound_table *table, int owner, uint16_t id);

/* The game's folder name for a character's sounds ("reimu", "udonge", ...), or "common". NULL for
 * anything else. */
const char *sts_sound_folder_name(int folder);

#endif
