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

/* A card's rarity, from data/rarity.csv. Starter marks the cards a starting deck is made of; offers
 * treat it as another tier (see sts_card_tier). */
typedef enum {
    STS_RARITY_COMMON = 0,
    STS_RARITY_UNCOMMON = 1,
    STS_RARITY_RARE = 2,
    STS_RARITY_STARTER = 3
} sts_rarity;

/* The tiers an offer can roll: common, uncommon and rare. */
#define STS_TIER_COUNT 3

typedef struct {
    uint16_t id;
    uint8_t type;           /* sts_card_type */
    uint8_t cost_or_slot;   /* card cost; for skills, the skill family */
    uint8_t rarity;         /* sts_rarity; common until sts_rarity_parse gives one */
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
    STS_DB_INCOMPLETE,  /* the system list or some character has no cards; or a card has no rarity */
    STS_DB_UNKNOWN_CARD /* a rarity for a card the table does not have */
} sts_db_status;

/* Parses CSV text. Blank lines and lines starting with '#' are ignored. On a line
 * error, *err_line (if non-NULL) receives the 1-based line number. */
sts_db_status sts_card_db_parse(const char *text, size_t len, sts_card_db *out, int *err_line);

sts_db_status sts_card_db_load(const char *path, sts_card_db *out, int *err_line);

const char *sts_db_status_str(sts_db_status status);

/* Reads data/rarity.csv into a parsed table: lines of owner,id,rarity,name, where rarity is common,
 * uncommon, rare or starter and the name (the rest of the line) is ignored. Every card must have
 * exactly one line. On an error the table may hold some of the rarities, and *err_line (if
 * non-NULL) receives the 1-based line number, or 0 for a card with no line. */
sts_db_status sts_rarity_parse(const char *text, size_t len, sts_card_db *db, int *err_line);

sts_db_status sts_rarity_load(const char *path, sts_card_db *db, int *err_line);

/* The tier an offer counts this card in, an sts_rarity below STS_TIER_COUNT. Starter cards
 * count by type, as decided: the system ones (Spiritual Strike Talisman and Divine Raiment of the
 * Dragon Fish) are strong and count as rare, and the starter skills and spells as common. */
int sts_card_tier(const sts_card *card);

/* Looks a card up for a character (system cards included), or NULL if that
 * character cannot use it. */
const sts_card *sts_card_db_find(const sts_card_db *db, int character, uint16_t id);

/* Community notation for a skill card: "d" for the default skill on an input or "a" for
 * an alternate, then the input in numpad notation, such as "d236" or "a421". Writes to
 * out (at least STS_SKILL_LABEL_SIZE bytes) and returns 1, or returns 0 and writes an
 * empty string if the card is not a skill this character has. */
#define STS_SKILL_LABEL_SIZE 8
int sts_skill_label(const sts_card_db *db, int character, uint16_t id, char *out);

/* Returns 1 if the character can use every card in the deck. Otherwise returns 0
 * and sets *bad_id (if non-NULL) to the first card it cannot use. */
int sts_deck_cards_legal(const sts_card_db *db, int character, const sts_deck *deck, uint16_t *bad_id);

#endif
