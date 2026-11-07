/* Card offers: what a player may pick during the draft or after a match.
 *
 * The pool is filtered BEFORE rolling, so an offer can never break the copy limit or
 * the deck size bounds and there is never a reroll. Offers may use a local RNG because
 * only the local player sees them; the chosen deck is what reaches the match.
 */
#ifndef STS_OFFER_H
#define STS_OFFER_H

#include <stdint.h>
#include "deck.h"
#include "pool.h"
#include "rng.h"
#include "rules.h"

/* What taking an offer does to the deck. */
typedef enum {
    STS_REWARD_ADD,      /* adds the copies; the deck grows */
    STS_REWARD_REPLACE,  /* adds the copies and removes as many others; the size is unchanged */
    STS_REWARD_REMOVE,   /* removes the copies; offers come from the deck, not the pool */
    STS_REWARD_SKIP      /* no offers */
} sts_reward_mode;

/* Bits for sts_offer_settings.type_mask. */
#define STS_TYPE_BIT(card_type) (1u << (card_type))
#define STS_TYPE_ALL (STS_TYPE_BIT(STS_CARD_SYSTEM) | STS_TYPE_BIT(STS_CARD_SKILL) | STS_TYPE_BIT(STS_CARD_SPELL))

typedef struct {
    sts_reward_mode mode;
    int offer_count;      /* offers per pick */
    int copies_min;       /* copies per offer; equal min and max give a fixed count */
    int copies_max;
    unsigned type_mask;   /* which card types may be offered */
} sts_offer_settings;

typedef struct {
    uint16_t id;
    uint8_t type;         /* sts_card_type */
    int copies;
} sts_offer;

/* Fills out with up to min(offer_count, max_out) offers for distinct cards. A card is
 * only eligible if at least copies_min of it fit; copies are then rolled between
 * copies_min and the most that fit, so a card close to a limit gets fewer copies.
 *
 * REPLACE counts the copy limit against the deck as it stands, before anything is
 * removed, so it stays legal whichever cards end up being replaced.
 *
 * Returns the number of offers, which is less than requested when too few cards are
 * eligible and 0 when none are. Returns -1 for invalid arguments. */
int sts_offer_generate(const sts_card_db *db, int character, const sts_deck *deck,
                       const sts_rules *rules, const sts_offer_settings *settings,
                       sts_rng *rng, sts_offer *out, int max_out);

#endif
