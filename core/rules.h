/* Deck rules. Every field is a setting; nothing in core/ hardcodes 20 or 4. */
#ifndef STS_RULES_H
#define STS_RULES_H

typedef struct {
    int deck_min;    /* smallest legal deck size */
    int deck_max;    /* largest legal deck size */
    int copy_limit;  /* max copies of any single card ID */
} sts_rules;

/* Vanilla rules: exactly 20 cards, at most 4 copies. */
static inline sts_rules sts_rules_vanilla(void)
{
    sts_rules r;
    r.deck_min = 20;
    r.deck_max = 20;
    r.copy_limit = 4;
    return r;
}

#endif
