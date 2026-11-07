/* Card table: which cards exist for each character, loaded from data/cards.csv.
 * Holds no text; names and descriptions come from the game at runtime. */
#ifndef STS_POOL_H
#define STS_POOL_H

#include <stddef.h>
#include <stdint.h>
#include "deck.h"
#include "pf.h"

/* Largest list seen is 27 cards; this leaves room without needing allocation. */
#define STS_POOL_MAX_CARDS 64

/* Values match the game's card type field. */
typedef enum {
    STS_CARD_SYSTEM = 0,
    STS_CARD_SKILL = 1,
    STS_CARD_SPELL = 2
} sts_card_type;

typedef struct {
    uint16_t id;
    uint8_t type;           /* sts_card_type */
    uint8_t cost_or_slot;   /* card cost; for skills, the skill family */
} sts_card;

typedef struct {
    sts_card cards[STS_POOL_MAX_CARDS];
    int count;
} sts_card_list;

/* System cards are shared. A character's card IDs mean nothing on their own: spell
 * 213 is a different card for each character that has one. */
typedef struct {
    sts_card_list system;
    sts_card_list characters[STS_NUM_CHARACTERS];
} sts_card_db;

typedef enum {
    STS_DB_OK = 0,
    STS_DB_IO,          /* file could not be opened or read */
    STS_DB_BAD_LINE,    /* wrong field count, unknown owner or type, bad number */
    STS_DB_DUPLICATE,   /* same ID twice for one owner */
    STS_DB_OVERFLOW,    /* more than STS_POOL_MAX_CARDS for one owner */
    STS_DB_INCOMPLETE   /* the system list or some character has no cards */
} sts_db_status;

/* Parses CSV text. Blank lines and lines starting with '#' are ignored. On a line
 * error, *err_line (if non-NULL) receives the 1-based line number. */
sts_db_status sts_card_db_parse(const char *text, size_t len, sts_card_db *out, int *err_line);

sts_db_status sts_card_db_load(const char *path, sts_card_db *out, int *err_line);

const char *sts_db_status_str(sts_db_status s);

/* Looks a card up for a character (system cards included), or NULL if that
 * character cannot use it. */
const sts_card *sts_card_db_find(const sts_card_db *db, int character, uint16_t id);

/* Returns 1 if the character can use every card in the deck. Otherwise returns 0
 * and sets *bad_id (if non-NULL) to the first card it cannot use. */
int sts_deck_cards_legal(const sts_card_db *db, int character, const sts_deck *d, uint16_t *bad_id);

#endif
