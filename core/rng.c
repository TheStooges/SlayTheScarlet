#include "rng.h"

uint32_t sts_rng_next(sts_rng *rng)
{
    uint64_t old = rng->state;
    uint32_t xorshifted = (uint32_t)(((old >> 18) ^ old) >> 27);
    uint32_t rot = (uint32_t)(old >> 59);

    rng->state = old * 6364136223846793005ULL + rng->inc;
    return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
}

void sts_rng_seed(sts_rng *rng, uint64_t seed, uint64_t seq)
{
    rng->state = 0;
    rng->inc = (seq << 1) | 1u;
    sts_rng_next(rng);
    rng->state += seed;
    sts_rng_next(rng);
}

uint32_t sts_rng_below(sts_rng *rng, uint32_t bound)
{
    /* Values below threshold would make low results slightly more likely. */
    uint32_t threshold = (0u - bound) % bound;

    for (;;) {
        uint32_t value = sts_rng_next(rng);
        if (value >= threshold)
            return value % bound;
    }
}
