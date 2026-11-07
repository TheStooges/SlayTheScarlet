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

/* How REPLACE decides which cards leave the deck. */
typedef enum {
    STS_REPLACE_CHOOSE,   /* the player names the exact cards */
    STS_REPLACE_BY_TYPE,  /* the player names a type; random cards of that type leave */
    STS_REPLACE_RANDOM    /* random cards leave */
} sts_replace_control;

/* Most offers one pick can show. */
#define STS_OFFER_MAX 8

/* Bits for sts_offer_settings.type_mask. */
#define STS_TYPE_BIT(card_type) (1u << (card_type))
#define STS_TYPE_ALL (STS_TYPE_BIT(STS_CARD_SYSTEM) | STS_TYPE_BIT(STS_CARD_SKILL) | STS_TYPE_BIT(STS_CARD_SPELL))

typedef struct {
    sts_reward_mode mode;
    int offer_count;      /* offers per pick */
    int copies_min;       /* copies per offer; equal min and max give a fixed count */
    int copies_max;
    unsigned type_mask;   /* which card types may be offered */
    sts_replace_control replace;  /* REPLACE only */
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
 * removed, so it stays legal whichever cards end up being replaced. With BY_TYPE, copies
 * are also limited to the largest type in the deck, so some type can always supply them.
 *
 * Returns the number of offers, which is less than requested when too few cards are
 * eligible and 0 when none are. Returns -1 for invalid arguments. */
int sts_offer_generate(const sts_card_db *db, int character, const sts_deck *deck,
                       const sts_rules *rules, const sts_offer_settings *settings,
                       sts_rng *rng, sts_offer *out, int max_out);

/* Returns 1 if the generator could have made this exact offer: used to check an offer made
 * earlier, such as one from a save, against the rules now. */
int sts_offer_is_valid(const sts_card_db *db, int character, const sts_deck *deck,
                       const sts_rules *rules, const sts_offer_settings *settings, const sts_offer *offer);

/* ---------- applying a taken offer ----------
 *
 * Each function works on a copy and only changes the deck if the result is legal, so a
 * refused pick leaves it untouched. On success the deck is sorted, as the game stores it.
 * ADD and the draft allow a deck still below deck_min; the others require a legal deck. */
typedef enum {
    STS_APPLY_OK = 0,
    STS_APPLY_ILLEGAL,       /* the result would break the rules */
    STS_APPLY_BAD_REMOVAL    /* the deck cannot give up the requested cards */
} sts_apply_status;

sts_apply_status sts_offer_apply_add(sts_deck *deck, const sts_offer *offer, const sts_rules *rules);

sts_apply_status sts_offer_apply_remove(sts_deck *deck, const sts_offer *offer, const sts_rules *rules);

/* CHOOSE: remove_ids lists remove_count cards, which must equal offer->copies. */
sts_apply_status sts_offer_replace_chosen(sts_deck *deck, const sts_offer *offer, const sts_rules *rules,
                                          const uint16_t *remove_ids, int remove_count);

/* BY_TYPE: removes offer->copies random cards of the given type. */
sts_apply_status sts_offer_replace_by_type(sts_deck *deck, const sts_offer *offer, const sts_rules *rules,
                                           const sts_card_db *db, int character, int type, sts_rng *rng);

/* RANDOM: removes offer->copies random cards. */
sts_apply_status sts_offer_replace_random(sts_deck *deck, const sts_offer *offer, const sts_rules *rules,
                                          sts_rng *rng);

/* Mask of the types holding at least `copies` cards in the deck: what a BY_TYPE
 * screen can let the player choose from. */
unsigned sts_replace_types_available(const sts_card_db *db, int character, const sts_deck *deck, int copies);

#endif
