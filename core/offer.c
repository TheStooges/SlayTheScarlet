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

static int min_int(int a, int b)
{
    return a < b ? a : b;
}

static int count_of_type(const sts_card_db *db, int character, const sts_deck *deck, int type)
{
    int i, n = 0;

    for (i = 0; i < deck->count; i++) {
        const sts_card *c = sts_card_db_find(db, character, deck->ids[i]);
        if (c && c->type == type)
            n++;
    }
    return n;
}

/* How many cards a REPLACE adding this card can take out of the deck. Its own copies do not
 * count: swapping a card for itself would waste the reward. */
static int replace_capacity(const sts_card_db *db, int character, const sts_deck *deck,
                            const sts_offer_settings *s, const sts_card *card)
{
    int have = sts_deck_count_of(deck, card->id);
    int type, best = 0;

    if (s->replace != STS_REPLACE_BY_TYPE)
        return deck->count - have;
    for (type = STS_CARD_SYSTEM; type <= STS_CARD_SPELL; type++) {
        int n = count_of_type(db, character, deck, type);
        if (type == card->type)
            n -= have;
        if (n > best)
            best = n;
    }
    return best;
}

/* The most copies of this card that the mode allows, ignoring copies_min. */
static int max_copies_for(const sts_card_db *db, int character, const sts_card *card, const sts_deck *deck,
                          const sts_rules *rules, const sts_offer_settings *s)
{
    int have = sts_deck_count_of(deck, card->id);
    int deck_limit = min_int(rules->deck_max, STS_MAX_DECK);
    int m = s->copies_max;

    switch (s->mode) {
    case STS_REWARD_ADD:
        m = min_int(m, rules->copy_limit - have);
        return min_int(m, deck_limit - deck->count);
    case STS_REWARD_REPLACE:
        m = min_int(m, rules->copy_limit - have);
        return min_int(m, replace_capacity(db, character, deck, s, card));
    case STS_REWARD_REMOVE:
        m = min_int(m, have);
        return min_int(m, deck->count - rules->deck_min);
    case STS_REWARD_SKIP:
        break;
    }
    return 0;
}

static void collect_candidates(const sts_card_db *db, int character, const sts_card_list *list,
                               const sts_deck *deck, const sts_rules *rules,
                               const sts_offer_settings *s, candidate_list *by_type)
{
    int i;

    for (i = 0; i < list->count; i++) {
        const sts_card *card = &list->cards[i];
        candidate_list *out = &by_type[card->type];
        int max_copies;

        if (!(s->type_mask & STS_TYPE_BIT(card->type)))
            continue;
        max_copies = max_copies_for(db, character, card, deck, rules, s);
        if (max_copies < s->copies_min)
            continue;
        out->items[out->count].card = card;
        out->items[out->count].max_copies = max_copies;
        out->count++;
    }
}

/* The type for the offer at this position, or -1 if nothing more can be offered. */
static int choose_type(const sts_offer_settings *s, const candidate_list *by_type, int position, sts_rng *rng)
{
    int total = 0, type;

    if (s->style == STS_OFFER_ONE_OF_EACH) {
        int i;

        for (i = 0; i < 3; i++) {
            type = (position + i) % 3;
            if (by_type[type].count > 0)
                return type;
        }
        return -1;
    }

    /* Only types that still have a card take part, so no roll is ever wasted. */
    for (type = 0; type < 3; type++)
        if (by_type[type].count > 0)
            total += s->weights[type];
    if (total > 0) {
        int roll = (int)sts_rng_below(rng, (uint32_t)total);

        for (type = 0; type < 3; type++) {
            if (by_type[type].count == 0)
                continue;
            if (roll < s->weights[type])
                return type;
            roll -= s->weights[type];
        }
    }
    return -1;
}

int sts_offer_generate(const sts_card_db *db, int character, const sts_deck *deck,
                       const sts_rules *rules, const sts_offer_settings *settings,
                       sts_rng *rng, sts_offer *out, int max_out)
{
    candidate_list by_type[3];
    int want, i;

    if (character < 0 || character >= STS_NUM_CHARACTERS)
        return -1;
    if (settings->offer_count < 0 || max_out < 0)
        return -1;
    if (settings->copies_min < 1 || settings->copies_max < settings->copies_min)
        return -1;

    for (i = 0; i < 3; i++)
        by_type[i].count = 0;
    collect_candidates(db, character, &db->system, deck, rules, settings, by_type);
    collect_candidates(db, character, &db->characters[character], deck, rules, settings, by_type);

    want = min_int(settings->offer_count, max_out);
    for (i = 0; i < want; i++) {
        int type = choose_type(settings, by_type, i, rng);
        candidate_list *list;
        candidate picked;
        int j, range;

        if (type < 0)
            break;
        /* Taken out of its list, so one set of offers never repeats a card. */
        list = &by_type[type];
        j = (int)sts_rng_below(rng, (uint32_t)list->count);
        picked = list->items[j];
        list->items[j] = list->items[--list->count];

        range = picked.max_copies - settings->copies_min + 1;
        out[i].id = picked.card->id;
        out[i].type = picked.card->type;
        out[i].copies = settings->copies_min + (int)sts_rng_below(rng, (uint32_t)range);
    }
    return i;
}

int sts_offer_is_valid(const sts_card_db *db, int character, const sts_deck *deck,
                       const sts_rules *rules, const sts_offer_settings *settings, const sts_offer *offer)
{
    const sts_card *card = sts_card_db_find(db, character, offer->id);

    if (!card || !(settings->type_mask & STS_TYPE_BIT(card->type)))
        return 0;
    return offer->copies >= settings->copies_min
        && offer->copies <= max_copies_for(db, character, card, deck, rules, settings);
}

/* ---------- applying a taken offer ---------- */

/* Removes one copy. Returns 0 if the deck has none. */
static int remove_one(sts_deck *d, uint16_t id)
{
    int i;

    for (i = 0; i < d->count; i++) {
        if (d->ids[i] == id) {
            d->ids[i] = d->ids[--d->count];
            return 1;
        }
    }
    return 0;
}

static int add_copies(sts_deck *d, uint16_t id, int copies)
{
    int i;

    for (i = 0; i < copies; i++)
        if (sts_deck_add(d, id) != 0)
            return 0;
    return 1;
}

/* Removes `count` cards at random from the listed deck positions. */
static void remove_random_of(sts_deck *d, int *positions, int available, int count, sts_rng *rng)
{
    uint16_t ids[STS_MAX_DECK];
    int i;

    for (i = 0; i < count; i++) {
        int j = i + (int)sts_rng_below(rng, (uint32_t)(available - i));
        ids[i] = d->ids[positions[j]];
        positions[j] = positions[i];
    }
    for (i = 0; i < count; i++)
        remove_one(d, ids[i]);
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
    int i;

    if (offer->copies < 1)
        return STS_APPLY_ILLEGAL;
    for (i = 0; i < offer->copies; i++)
        if (!remove_one(&result, offer->id))
            return STS_APPLY_BAD_REMOVAL;
    return commit_full(deck, &result, rules);
}

sts_apply_status sts_offer_replace_chosen(sts_deck *deck, const sts_offer *offer, const sts_rules *rules,
                                          const uint16_t *remove_ids, int remove_count)
{
    sts_deck result = *deck;
    int i;

    if (offer->copies < 1 || remove_count != offer->copies)
        return STS_APPLY_BAD_REMOVAL;
    for (i = 0; i < remove_count; i++)
        if (remove_ids[i] == offer->id || !remove_one(&result, remove_ids[i]))
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
    int i, n = 0;

    if (offer->copies < 1)
        return STS_APPLY_BAD_REMOVAL;
    for (i = 0; i < result.count; i++) {
        const sts_card *c = sts_card_db_find(db, character, result.ids[i]);
        if (c && c->type == type && result.ids[i] != offer->id)
            positions[n++] = i;
    }
    if (n < offer->copies)
        return STS_APPLY_BAD_REMOVAL;
    remove_random_of(&result, positions, n, offer->copies, rng);
    if (!add_copies(&result, offer->id, offer->copies))
        return STS_APPLY_ILLEGAL;
    return commit_full(deck, &result, rules);
}

sts_apply_status sts_offer_replace_random(sts_deck *deck, const sts_offer *offer, const sts_rules *rules,
                                          sts_rng *rng)
{
    sts_deck result = *deck;
    int positions[STS_MAX_DECK];
    int i, n = 0;

    if (offer->copies < 1)
        return STS_APPLY_BAD_REMOVAL;
    for (i = 0; i < result.count; i++)
        if (result.ids[i] != offer->id)
            positions[n++] = i;
    if (n < offer->copies)
        return STS_APPLY_BAD_REMOVAL;
    remove_random_of(&result, positions, n, offer->copies, rng);
    if (!add_copies(&result, offer->id, offer->copies))
        return STS_APPLY_ILLEGAL;
    return commit_full(deck, &result, rules);
}

int sts_replace_count_of_type(const sts_card_db *db, int character, const sts_deck *deck,
                              uint16_t offer_id, int type)
{
    const sts_card *card = sts_card_db_find(db, character, offer_id);
    int n = count_of_type(db, character, deck, type);

    if (card && card->type == type)
        n -= sts_deck_count_of(deck, offer_id);
    return n;
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
