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
    int i, count = 0;
    for (i = 0; i < deck->count; i++)
        if (deck->ids[i] == id)
            count++;
    return count;
}

void sts_deck_sort(sts_deck *deck)
{
    int i, j;
    for (i = 1; i < deck->count; i++) {
        uint16_t key = deck->ids[i];
        for (j = i - 1; j >= 0 && deck->ids[j] > key; j--)
            deck->ids[j + 1] = deck->ids[j];
        deck->ids[j + 1] = key;
    }
}

sts_deck_status sts_deck_validate(const sts_deck *deck, const sts_rules *rules, uint16_t *bad_id)
{
    int i;
    if (deck->count < rules->deck_min)
        return STS_DECK_TOO_FEW;
    if (deck->count > rules->deck_max)
        return STS_DECK_TOO_MANY;
    for (i = 0; i < deck->count; i++) {
        if (sts_deck_count_of(deck, deck->ids[i]) > rules->copy_limit) {
            if (bad_id)
                *bad_id = deck->ids[i];
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
