#include "rng.h"

uint32_t sts_rng_next(sts_rng *r)
{
    uint64_t old = r->state;
    uint32_t xorshifted = (uint32_t)(((old >> 18) ^ old) >> 27);
    uint32_t rot = (uint32_t)(old >> 59);

    r->state = old * 6364136223846793005ULL + r->inc;
    return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
}

void sts_rng_seed(sts_rng *r, uint64_t seed, uint64_t seq)
{
    r->state = 0;
    r->inc = (seq << 1) | 1u;
    sts_rng_next(r);
    r->state += seed;
    sts_rng_next(r);
}

uint32_t sts_rng_below(sts_rng *r, uint32_t bound)
{
    /* Values below threshold would make low results slightly more likely. */
    uint32_t threshold = (0u - bound) % bound;

    for (;;) {
        uint32_t v = sts_rng_next(r);
        if (v >= threshold)
            return v % bound;
    }
}
