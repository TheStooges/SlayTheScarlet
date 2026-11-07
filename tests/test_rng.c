/* Tests: Random numbers. */
#include "test_util.h"

void test_rng(void)
{
    /* Reference output of the PCG32 demo program for seed 42, sequence 54. If this
     * fails, the generator is not portable. */
    static const uint32_t expected[6] = {
        0xa15c02b7u, 0x7b47f409u, 0xba1d3330u, 0x83d2f293u, 0xbfa4784bu, 0xcbed606eu
    };
    sts_rng rng_a, rng_b;
    int i, seen[7], all_seen = 1;

    sts_rng_seed(&rng_a, 42, 54);
    for (i = 0; i < 6; i++)
        CHECK(sts_rng_next(&rng_a) == expected[i]);

    sts_rng_seed(&rng_a, 7, 1);
    sts_rng_seed(&rng_b, 7, 1);
    for (i = 0; i < 100; i++)
        CHECK(sts_rng_next(&rng_a) == sts_rng_next(&rng_b));

    memset(seen, 0, sizeof seen);
    for (i = 0; i < 1000; i++) {
        uint32_t value = sts_rng_below(&rng_a, 7);
        CHECK(value < 7);
        if (value < 7) seen[value] = 1;
    }
    for (i = 0; i < 7; i++)
        if (!seen[i]) all_seen = 0;
    CHECK(all_seen);
    CHECK(sts_rng_below(&rng_a, 1) == 0);
}
