#include "offer.h"

typedef struct {
    const sts_card *card;
    int max_copies;
} candidate;

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

/* How many cards a REPLACE can take out of the deck. */
static int replace_capacity(const sts_card_db *db, int character, const sts_deck *deck,
                            const sts_offer_settings *s)
{
    int type, best = 0;

    if (s->replace != STS_REPLACE_BY_TYPE)
        return deck->count;
    for (type = STS_CARD_SYSTEM; type <= STS_CARD_SPELL; type++) {
        int n = count_of_type(db, character, deck, type);
        if (n > best)
            best = n;
    }
    return best;
}

/* The most copies of this card that the mode allows, ignoring copies_min. */
static int max_copies_for(const sts_card *card, const sts_deck *deck, const sts_rules *rules,
                          const sts_offer_settings *s, int capacity)
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
        return min_int(m, capacity);
    case STS_REWARD_REMOVE:
        m = min_int(m, have);
        return min_int(m, deck->count - rules->deck_min);
    case STS_REWARD_SKIP:
        break;
    }
    return 0;
}

static int collect_candidates(const sts_card_list *list, const sts_deck *deck, const sts_rules *rules,
                              const sts_offer_settings *s, int capacity, candidate *out, int n)
{
    int i;

    for (i = 0; i < list->count; i++) {
        const sts_card *card = &list->cards[i];
        int max_copies;

        if (!(s->type_mask & STS_TYPE_BIT(card->type)))
            continue;
        max_copies = max_copies_for(card, deck, rules, s, capacity);
        if (max_copies < s->copies_min)
            continue;
        out[n].card = card;
        out[n].max_copies = max_copies;
        n++;
    }
    return n;
}

int sts_offer_generate(const sts_card_db *db, int character, const sts_deck *deck,
                       const sts_rules *rules, const sts_offer_settings *settings,
                       sts_rng *rng, sts_offer *out, int max_out)
{
    candidate candidates[STS_POOL_MAX_CARDS * 2];
    int n, want, i, capacity;

    if (character < 0 || character >= STS_NUM_CHARACTERS)
        return -1;
    if (settings->offer_count < 0 || max_out < 0)
        return -1;
    if (settings->copies_min < 1 || settings->copies_max < settings->copies_min)
        return -1;

    capacity = replace_capacity(db, character, deck, settings);
    n = collect_candidates(&db->system, deck, rules, settings, capacity, candidates, 0);
    n = collect_candidates(&db->characters[character], deck, rules, settings, capacity, candidates, n);

    /* Partial Fisher-Yates: the first want entries become a uniform distinct sample. */
    want = min_int(min_int(settings->offer_count, max_out), n);
    for (i = 0; i < want; i++) {
        int j = i + (int)sts_rng_below(rng, (uint32_t)(n - i));
        candidate picked = candidates[j];
        int range = picked.max_copies - settings->copies_min + 1;

        candidates[j] = candidates[i];   /* slot i is not read again, so no full swap */

        out[i].id = picked.card->id;
        out[i].type = picked.card->type;
        out[i].copies = settings->copies_min + (int)sts_rng_below(rng, (uint32_t)range);
    }
    return want;
}

int sts_offer_is_valid(const sts_card_db *db, int character, const sts_deck *deck,
                       const sts_rules *rules, const sts_offer_settings *settings, const sts_offer *offer)
{
    const sts_card *card = sts_card_db_find(db, character, offer->id);

    if (!card || !(settings->type_mask & STS_TYPE_BIT(card->type)))
        return 0;
    return offer->copies >= settings->copies_min
        && offer->copies <= max_copies_for(card, deck, rules, settings, replace_capacity(db, character, deck, settings));
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
        if (!remove_one(&result, remove_ids[i]))
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
        if (c && c->type == type)
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
    int i;

    if (offer->copies < 1 || offer->copies > result.count)
        return STS_APPLY_BAD_REMOVAL;
    for (i = 0; i < result.count; i++)
        positions[i] = i;
    remove_random_of(&result, positions, result.count, offer->copies, rng);
    if (!add_copies(&result, offer->id, offer->copies))
        return STS_APPLY_ILLEGAL;
    return commit_full(deck, &result, rules);
}

unsigned sts_replace_types_available(const sts_card_db *db, int character, const sts_deck *deck, int copies)
{
    unsigned mask = 0;
    int type;

    for (type = STS_CARD_SYSTEM; type <= STS_CARD_SPELL; type++)
        if (count_of_type(db, character, deck, type) >= copies)
            mask |= STS_TYPE_BIT(type);
    return mask;
}
