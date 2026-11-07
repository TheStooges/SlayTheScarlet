/* Portable random numbers (PCG32). Integer only, so the same seed gives the same
 * sequence on every compiler and machine. Never uses rand() or the clock: callers
 * choose the seed, and anything affecting a match must seed from shared data. */
#ifndef STS_RNG_H
#define STS_RNG_H

#include <stdint.h>

typedef struct {
    uint64_t state;
    uint64_t inc;
} sts_rng;

/* seq selects an independent stream; streams with different seq do not overlap. */
void sts_rng_seed(sts_rng *r, uint64_t seed, uint64_t seq);

uint32_t sts_rng_next(sts_rng *r);

/* Uniform value in [0, bound), without modulo bias. bound must be at least 1. */
uint32_t sts_rng_below(sts_rng *r, uint32_t bound);

#endif
