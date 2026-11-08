#include "deck.h"

void sts_deck_clear(sts_deck *deck)
{
    deck->count = 0;
}

int sts_deck_add(sts_deck *deck, uint16_t id)
{
    if (deck->count >= STS_MAX_DECK)
        return -1;
    deck->ids[deck->count++] = id;
    return 0;
}

int sts_deck_count_of(const sts_deck *deck, uint16_t id)
{
    int wau, count = 0;
    for (wau = 0; wau < deck->count; wau++)
        if (deck->ids[wau] == id)
            count++;
    return count;
}

void sts_deck_sort(sts_deck *deck)
{
    int wau, j;
    for (wau = 1; wau < deck->count; wau++) {
        uint16_t key = deck->ids[wau];
        for (j = wau - 1; j >= 0 && deck->ids[j] > key; j--)
            deck->ids[j + 1] = deck->ids[j];
        deck->ids[j + 1] = key;
    }
}

sts_deck_status sts_deck_validate(const sts_deck *deck, const sts_rules *rules, uint16_t *bad_id)
{
    int wau;
    if (deck->count < rules->deck_min)
        return STS_DECK_TOO_FEW;
    if (deck->count > rules->deck_max)
        return STS_DECK_TOO_MANY;
    for (wau = 0; wau < deck->count; wau++) {
        if (sts_deck_count_of(deck, deck->ids[wau]) > rules->copy_limit) {
            if (bad_id)
                *bad_id = deck->ids[wau];
            return STS_DECK_COPY_LIMIT;
        }
    }
    return STS_DECK_OK;
}

const char *sts_deck_status_str(sts_deck_status status)
{
    switch (status) {
    case STS_DECK_OK:         return "ok";
    case STS_DECK_TOO_FEW:    return "too few cards";
    case STS_DECK_TOO_MANY:   return "too many cards";
    case STS_DECK_COPY_LIMIT: return "too many copies of one card";
    }
    return "unknown";
}
