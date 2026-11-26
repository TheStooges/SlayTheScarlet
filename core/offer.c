#include "offer.h"

typedef struct {
    const sts_card *card;
    int max_copies;
} candidate;

/* The eligible cards of one type. */
typedef struct {
    candidate items[STS_POOL_MAX_CARDS * 2];
    int count;
} candidate_list;

/* The eligible cards of one tier, by type. With rarity off, every card is in the first tier. */
typedef struct {
    candidate_list by_type[3];
} tier_candidates;

static int min_int(int first, int second)
{
    return first < second ? first : second;
}

static int count_of_type(const sts_card_db *db, int character, const sts_deck *deck, int type)
{
    int wau, count = 0;

    for (wau = 0; wau < deck->count; wau++) {
        const sts_card *card = sts_card_db_find(db, character, deck->ids[wau]);
        if (card && card->type == type)
            count++;
    }
    return count;
}

/* How many cards a REPLACE adding this card can take out of the deck. Its own copies do not
 * count: swapping a card for itself would waste the reward. */
static int replace_capacity(const sts_card_db *db, int character, const sts_deck *deck,
                            const sts_offer_settings *offer_settings, const sts_card *card)
{
    int have = sts_deck_count_of(deck, card->id);
    int type, best = 0;

    if (offer_settings->replace != STS_REPLACE_BY_TYPE)
        return deck->count - have;
    for (type = STS_CARD_SYSTEM; type <= STS_CARD_SPELL; type++) {
        int count = count_of_type(db, character, deck, type);
        if (type == card->type)
            count -= have;
        if (count > best)
            best = count;
    }
    return best;
}

/* The most copies of this card that the mode allows, ignoring copies_min. */
static int max_copies_for(const sts_card_db *db, int character, const sts_card *card, const sts_deck *deck,
                          const sts_rules *rules, const sts_offer_settings *offer_settings)
{
    int have = sts_deck_count_of(deck, card->id);
    int deck_limit = min_int(rules->deck_max, STS_MAX_DECK);
    int limit = offer_settings->copies_max;

    switch (offer_settings->mode) {
    case STS_REWARD_ADD:
        limit = min_int(limit, rules->copy_limit - have);
        return min_int(limit, deck_limit - deck->count);
    case STS_REWARD_REPLACE:
        limit = min_int(limit, rules->copy_limit - have);
        return min_int(limit, replace_capacity(db, character, deck, offer_settings, card));
    case STS_REWARD_REMOVE:
        limit = min_int(limit, have);
        return min_int(limit, deck->count - rules->deck_min);
    case STS_REWARD_SKIP:
        break;
    }
    return 0;
}

static void collect_candidates(const sts_card_db *db, int character, const sts_card_list *list,
                               const sts_deck *deck, const sts_rules *rules,
                               const sts_offer_settings *offer_settings, tier_candidates *tiers)
{
    int wau;

    for (wau = 0; wau < list->count; wau++) {
        const sts_card *card = &list->cards[wau];
        int tier = offer_settings->rarity ? sts_card_tier(card) : 0;
        candidate_list *out = &tiers[tier].by_type[card->type];
        int max_copies;

        if (!(offer_settings->type_mask & STS_TYPE_BIT(card->type)))
            continue;
        if (offer_settings->rarity && offer_settings->rarity_weights[tier] <= 0)
            continue;
        max_copies = max_copies_for(db, character, card, deck, rules, offer_settings);
        if (max_copies < offer_settings->copies_min)
            continue;
        out->items[out->count].card = card;
        out->items[out->count].max_copies = max_copies;
        out->count++;
    }
}

static int type_weight(const sts_offer_settings *offer_settings, const candidate_list *by_type, int type)
{
    return offer_settings->style == STS_OFFER_BY_CARD ? by_type[type].count : offer_settings->weights[type];
}

/* The type for the offer at this position, or -1 if nothing more can be offered. */
static int choose_type(const sts_offer_settings *offer_settings, const candidate_list *by_type, int position,
                       sts_rng *rng)
{
    int total = 0, type;

    if (offer_settings->style == STS_OFFER_ONE_OF_EACH) {
        int wau;

        for (wau = 0; wau < 3; wau++) {
            type = (position + wau) % 3;
            if (by_type[type].count > 0)
                return type;
        }
        return -1;
    }

    /* Only types that still have a card take part, so no roll is ever wasted. By card, a type weighs
     * as many as the cards it has left. */
    for (type = 0; type < 3; type++)
        if (by_type[type].count > 0)
            total += type_weight(offer_settings, by_type, type);
    if (total > 0) {
        int roll = (int)sts_rng_below(rng, (uint32_t)total);

        for (type = 0; type < 3; type++) {
            if (by_type[type].count == 0)
                continue;
            if (roll < type_weight(offer_settings, by_type, type))
                return type;
            roll -= type_weight(offer_settings, by_type, type);
        }
    }
    return -1;
}

static int tier_card_count(const tier_candidates *tier)
{
    return tier->by_type[0].count + tier->by_type[1].count + tier->by_type[2].count;
}

/* The tier for a pick's offers, from the weights alone, so two players with the same roll and weights
 * get the same tier whatever cards each has left; a tier with none falls back (fallback_tier). The
 * roll is tier_roll if given, else from rng. Returns -1 if no tier has a weight. */
static int roll_tier(const sts_offer_settings *offer_settings, const uint32_t *tier_roll, sts_rng *rng)
{
    int total = 0, tier, roll;

    if (!offer_settings->rarity)
        return 0;
    for (tier = 0; tier < STS_TIER_COUNT; tier++)
        if (offer_settings->rarity_weights[tier] > 0)
            total += offer_settings->rarity_weights[tier];
    if (total <= 0)
        return -1;
    roll = tier_roll ? (int)(*tier_roll % (uint32_t)total) : (int)sts_rng_below(rng, (uint32_t)total);
    for (tier = 0; tier < STS_TIER_COUNT; tier++) {
        if (offer_settings->rarity_weights[tier] <= 0)
            continue;
        if (roll < offer_settings->rarity_weights[tier])
            return tier;
        roll -= offer_settings->rarity_weights[tier];
    }
    return -1;
}

/* The tier to carry on from once this one has no cards left: the nearest with any, the lower one
 * first. Tiers with weight 0 were never filled, so they are never used. Returns -1 if none has. */
static int fallback_tier(const tier_candidates *tiers, int tier)
{
    static const int steps[] = {-1, 1, -2, 2};
    int wau;

    for (wau = 0; wau < 4; wau++) {
        int other = tier + steps[wau];

        if (other >= 0 && other < STS_TIER_COUNT && tier_card_count(&tiers[other]) > 0)
            return other;
    }
    return -1;
}

/* The weight of copies_min + extra copies. */
static int copies_weight(const sts_offer_settings *offer_settings, int available, int extra)
{
    if (offer_settings->copy_odds == STS_COPY_ODDS_FALLING)
        return offer_settings->copies_max - offer_settings->copies_min + 1 - extra;
    if (offer_settings->copy_odds == STS_COPY_ODDS_HALVING)
        return 1 << (available - 1 - extra);
    return 1;
}

/* How many copies an offer gives, from copies_min up to max_copies. Each number weighs what it
 * would over the whole range, so the numbers that fit keep their proportions. */
static int roll_copies(const sts_offer_settings *offer_settings, int max_copies, sts_rng *rng)
{
    int available = max_copies - offer_settings->copies_min + 1;
    int total = 0, extra, roll;

    if (offer_settings->copy_odds == STS_COPY_ODDS_HALVING && available > 16)
        available = 16;   /* past this, a number's chance is below 1 in 65536 */
    for (extra = 0; extra < available; extra++)
        total += copies_weight(offer_settings, available, extra);
    roll = (int)sts_rng_below(rng, (uint32_t)total);
    for (extra = 0; extra < available - 1; extra++) {
        int weight = copies_weight(offer_settings, available, extra);

        if (roll < weight)
            break;
        roll -= weight;
    }
    return offer_settings->copies_min + extra;
}

static int generate(const sts_card_db *db, int character, const sts_deck *deck, const sts_rules *rules,
                    const sts_offer_settings *settings, const uint32_t *tier_roll, sts_rng *rng, sts_offer *out,
                    int max_out)
{
    tier_candidates tiers[STS_TIER_COUNT];
    int want, wau, tier;

    if (character < 0 || character >= STS_NUM_CHARACTERS)
        return -1;
    if (settings->offer_count < 0 || max_out < 0)
        return -1;
    if (settings->copies_min < 1 || settings->copies_max < settings->copies_min)
        return -1;

    for (tier = 0; tier < STS_TIER_COUNT; tier++)
        for (wau = 0; wau < 3; wau++)
            tiers[tier].by_type[wau].count = 0;
    collect_candidates(db, character, &db->system, deck, rules, settings, tiers);
    collect_candidates(db, character, &db->characters[character], deck, rules, settings, tiers);

    want = min_int(settings->offer_count, max_out);
    tier = want > 0 ? roll_tier(settings, tier_roll, rng) : -1;
    for (wau = 0; wau < want && tier >= 0; wau++) {
        candidate_list *list;
        candidate picked;
        int type, j;

        if (tier_card_count(&tiers[tier]) == 0)
            tier = fallback_tier(tiers, tier);
        if (tier < 0)
            break;
        type = choose_type(settings, tiers[tier].by_type, wau, rng);
        if (type < 0 || tiers[tier].by_type[type].count <= 0)   /* a chosen type always has a card */
            break;
        /* Taken out of its list, so one set of offers never repeats a card. */
        list = &tiers[tier].by_type[type];
        j = (int)sts_rng_below(rng, (uint32_t)list->count);
        picked = list->items[j];
        list->items[j] = list->items[--list->count];

        out[wau].id = picked.card->id;
        out[wau].type = picked.card->type;
        out[wau].copies = roll_copies(settings, picked.max_copies, rng);
    }
    return wau;
}

int sts_offer_generate(const sts_card_db *db, int character, const sts_deck *deck,
                       const sts_rules *rules, const sts_offer_settings *settings,
                       sts_rng *rng, sts_offer *out, int max_out)
{
    return generate(db, character, deck, rules, settings, NULL, rng, out, max_out);
}

int sts_offer_generate_rolled(const sts_card_db *db, int character, const sts_deck *deck,
                              const sts_rules *rules, const sts_offer_settings *settings, uint32_t tier_roll,
                              sts_rng *rng, sts_offer *out, int max_out)
{
    return generate(db, character, deck, rules, settings, &tier_roll, rng, out, max_out);
}

int sts_offer_is_valid(const sts_card_db *db, int character, const sts_deck *deck,
                       const sts_rules *rules, const sts_offer_settings *settings, const sts_offer *offer)
{
    const sts_card *card = sts_card_db_find(db, character, offer->id);

    if (!card || !(settings->type_mask & STS_TYPE_BIT(card->type)))
        return 0;
    if (settings->rarity && settings->rarity_weights[sts_card_tier(card)] <= 0)
        return 0;
    return offer->copies >= settings->copies_min
        && offer->copies <= max_copies_for(db, character, card, deck, rules, settings);
}

/* ---------- applying a taken offer ---------- */

/* Removes one copy. Returns 0 if the deck has none. */
static int remove_one(sts_deck *deck, uint16_t id)
{
    int wau;

    for (wau = 0; wau < deck->count; wau++) {
        if (deck->ids[wau] == id) {
            deck->ids[wau] = deck->ids[--deck->count];
            return 1;
        }
    }
    return 0;
}

static int add_copies(sts_deck *deck, uint16_t id, int copies)
{
    int wau;

    for (wau = 0; wau < copies; wau++)
        if (sts_deck_add(deck, id) != 0)
            return 0;
    return 1;
}

/* Removes `count` cards at random from the listed deck positions. */
static void remove_random_of(sts_deck *deck, int *positions, int available, int count, sts_rng *rng)
{
    uint16_t ids[STS_MAX_DECK];
    int wau;

    for (wau = 0; wau < count; wau++) {
        int j = wau + (int)sts_rng_below(rng, (uint32_t)(available - wau));
        ids[wau] = deck->ids[positions[j]];
        positions[j] = positions[wau];
    }
    for (wau = 0; wau < count; wau++)
        remove_one(deck, ids[wau]);
}

static sts_apply_status commit_full(sts_deck *deck, sts_deck *result, const sts_rules *rules)
{
    if (sts_deck_validate(result, rules, NULL) != STS_DECK_OK)
        return STS_APPLY_ILLEGAL;
    sts_deck_sort(result);
    *deck = *result;
    return STS_APPLY_OK;
}

sts_apply_status sts_offer_apply_add(sts_deck *deck, const sts_offer *offer, const sts_rules *rules)
{
    sts_deck result = *deck;
    sts_rules partial = *rules;

    partial.deck_min = 0;       /* a draft is below the minimum until it finishes */
    if (offer->copies < 1 || !add_copies(&result, offer->id, offer->copies))
        return STS_APPLY_ILLEGAL;
    return commit_full(deck, &result, &partial);
}

sts_apply_status sts_offer_apply_remove(sts_deck *deck, const sts_offer *offer, const sts_rules *rules)
{
    sts_deck result = *deck;
    int wau;

    if (offer->copies < 1)
        return STS_APPLY_ILLEGAL;
    for (wau = 0; wau < offer->copies; wau++)
        if (!remove_one(&result, offer->id))
            return STS_APPLY_BAD_REMOVAL;
    return commit_full(deck, &result, rules);
}

sts_apply_status sts_offer_replace_chosen(sts_deck *deck, const sts_offer *offer, const sts_rules *rules,
                                          const uint16_t *remove_ids, int remove_count)
{
    sts_deck result = *deck;
    int wau;

    if (offer->copies < 1 || remove_count != offer->copies)
        return STS_APPLY_BAD_REMOVAL;
    for (wau = 0; wau < remove_count; wau++)
        if (remove_ids[wau] == offer->id || !remove_one(&result, remove_ids[wau]))
            return STS_APPLY_BAD_REMOVAL;
    if (!add_copies(&result, offer->id, offer->copies))
        return STS_APPLY_ILLEGAL;
    return commit_full(deck, &result, rules);
}

sts_apply_status sts_offer_replace_by_type(sts_deck *deck, const sts_offer *offer, const sts_rules *rules,
                                           const sts_card_db *db, int character, int type, sts_rng *rng)
{
    sts_deck result = *deck;
    int positions[STS_MAX_DECK];
    int wau, count = 0;

    if (offer->copies < 1)
        return STS_APPLY_BAD_REMOVAL;
    for (wau = 0; wau < result.count; wau++) {
        const sts_card *card = sts_card_db_find(db, character, result.ids[wau]);
        if (card && card->type == type && result.ids[wau] != offer->id)
            positions[count++] = wau;
    }
    if (count < offer->copies)
        return STS_APPLY_BAD_REMOVAL;
    remove_random_of(&result, positions, count, offer->copies, rng);
    if (!add_copies(&result, offer->id, offer->copies))
        return STS_APPLY_ILLEGAL;
    return commit_full(deck, &result, rules);
}

sts_apply_status sts_offer_replace_random(sts_deck *deck, const sts_offer *offer, const sts_rules *rules,
                                          sts_rng *rng)
{
    sts_deck result = *deck;
    int positions[STS_MAX_DECK];
    int wau, count = 0;

    if (offer->copies < 1)
        return STS_APPLY_BAD_REMOVAL;
    for (wau = 0; wau < result.count; wau++)
        if (result.ids[wau] != offer->id)
            positions[count++] = wau;
    if (count < offer->copies)
        return STS_APPLY_BAD_REMOVAL;
    remove_random_of(&result, positions, count, offer->copies, rng);
    if (!add_copies(&result, offer->id, offer->copies))
        return STS_APPLY_ILLEGAL;
    return commit_full(deck, &result, rules);
}

int sts_replace_count_of_type(const sts_card_db *db, int character, const sts_deck *deck,
                              uint16_t offer_id, int type)
{
    const sts_card *card = sts_card_db_find(db, character, offer_id);
    int count = count_of_type(db, character, deck, type);

    if (card && card->type == type)
        count -= sts_deck_count_of(deck, offer_id);
    return count;
}

unsigned sts_replace_types_available(const sts_card_db *db, int character, const sts_deck *deck,
                                     uint16_t offer_id, int copies)
{
    unsigned mask = 0;
    int type;

    for (type = STS_CARD_SYSTEM; type <= STS_CARD_SPELL; type++)
        if (sts_replace_count_of_type(db, character, deck, offer_id, type) >= copies)
            mask |= STS_TYPE_BIT(type);
    return mask;
}
