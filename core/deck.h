/* Deck model and validation. Portable C: no Windows or SokuLib headers. */
#ifndef STS_DECK_H
#define STS_DECK_H

#include <stdint.h>
#include "rules.h"

/* The .pf format stores a deck's card count in one byte. */
#define STS_MAX_DECK 255

typedef struct {
    uint16_t ids[STS_MAX_DECK];
    int count;
} sts_deck;

typedef enum {
    STS_DECK_OK = 0,
    STS_DECK_TOO_FEW,     /* fewer than rules.deck_min cards */
    STS_DECK_TOO_MANY,    /* more than rules.deck_max cards */
    STS_DECK_COPY_LIMIT   /* some card appears more than rules.copy_limit times */
} sts_deck_status;

void sts_deck_clear(sts_deck *d);

/* Appends a card. Returns 0, or -1 if the deck is already at STS_MAX_DECK. */
int sts_deck_add(sts_deck *d, uint16_t id);

int sts_deck_count_of(const sts_deck *d, uint16_t id);

/* Ascending by ID, as the game itself stores decks. */
void sts_deck_sort(sts_deck *d);

/* Checks size first, then copies. On STS_DECK_COPY_LIMIT, *bad_id (if non-NULL)
 * receives the offending card ID. */
sts_deck_status sts_deck_validate(const sts_deck *d, const sts_rules *r, uint16_t *bad_id);

const char *sts_deck_status_str(sts_deck_status s);

#endif
