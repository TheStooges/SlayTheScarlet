#include "deck.h"

void sts_deck_clear(sts_deck *d)
{
    d->count = 0;
}

int sts_deck_add(sts_deck *d, uint16_t id)
{
    if (d->count >= STS_MAX_DECK)
        return -1;
    d->ids[d->count++] = id;
    return 0;
}

int sts_deck_count_of(const sts_deck *d, uint16_t id)
{
    int i, n = 0;
    for (i = 0; i < d->count; i++)
        if (d->ids[i] == id)
            n++;
    return n;
}

void sts_deck_sort(sts_deck *d)
{
    int i, j;
    for (i = 1; i < d->count; i++) {
        uint16_t key = d->ids[i];
        for (j = i - 1; j >= 0 && d->ids[j] > key; j--)
            d->ids[j + 1] = d->ids[j];
        d->ids[j + 1] = key;
    }
}

sts_deck_status sts_deck_validate(const sts_deck *d, const sts_rules *r, uint16_t *bad_id)
{
    int i;
    if (d->count < r->deck_min)
        return STS_DECK_TOO_FEW;
    if (d->count > r->deck_max)
        return STS_DECK_TOO_MANY;
    for (i = 0; i < d->count; i++) {
        if (sts_deck_count_of(d, d->ids[i]) > r->copy_limit) {
            if (bad_id)
                *bad_id = d->ids[i];
            return STS_DECK_COPY_LIMIT;
        }
    }
    return STS_DECK_OK;
}

const char *sts_deck_status_str(sts_deck_status s)
{
    switch (s) {
    case STS_DECK_OK:         return "ok";
    case STS_DECK_TOO_FEW:    return "too few cards";
    case STS_DECK_TOO_MANY:   return "too many cards";
    case STS_DECK_COPY_LIMIT: return "too many copies of one card";
    }
    return "unknown";
}

sts_card_type sts_card_type_of(uint16_t id)
{
    if (id <= 20)
        return STS_CARD_SYSTEM;
    if (id >= 100 && id < 200)
        return STS_CARD_SKILL;
    if (id >= 200)
        return STS_CARD_SPELL;
    return STS_CARD_UNKNOWN;
}
