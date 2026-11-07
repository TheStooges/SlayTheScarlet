#include "offer.h"

typedef struct {
    const sts_card *card;
    int max_copies;
} candidate;

static int min_int(int a, int b)
{
    return a < b ? a : b;
}

/* The most copies of this card that the mode allows, ignoring copies_min. */
static int max_copies_for(const sts_card *card, const sts_deck *deck, const sts_rules *rules,
                          const sts_offer_settings *s)
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
        return min_int(m, deck->count);
    case STS_REWARD_REMOVE:
        m = min_int(m, have);
        return min_int(m, deck->count - rules->deck_min);
    case STS_REWARD_SKIP:
        break;
    }
    return 0;
}

static int collect_candidates(const sts_card_list *list, const sts_deck *deck, const sts_rules *rules,
                              const sts_offer_settings *s, candidate *out, int n)
{
    int i;

    for (i = 0; i < list->count; i++) {
        const sts_card *card = &list->cards[i];
        int max_copies;

        if (!(s->type_mask & STS_TYPE_BIT(card->type)))
            continue;
        max_copies = max_copies_for(card, deck, rules, s);
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
    int n, want, i;

    if (character < 0 || character >= STS_NUM_CHARACTERS)
        return -1;
    if (settings->offer_count < 0 || max_out < 0)
        return -1;
    if (settings->copies_min < 1 || settings->copies_max < settings->copies_min)
        return -1;

    n = collect_candidates(&db->system, deck, rules, settings, candidates, 0);
    n = collect_candidates(&db->characters[character], deck, rules, settings, candidates, n);

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
