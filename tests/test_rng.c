/* Tests: Random numbers. */
#include "test_util.h"

void test_rng(void)
{
    /* Reference output of the PCG32 demo program for seed 42, sequence 54. If this
     * fails, the generator is not portable. */
    static const uint32_t expected[6] = {
        0xa15c02b7u, 0x7b47f409u, 0xba1d3330u, 0x83d2f293u, 0xbfa4784bu, 0xcbed606eu
    };
    sts_rng a, b;
    int i, seen[7], all_seen = 1;

    sts_rng_seed(&a, 42, 54);
    for (i = 0; i < 6; i++)
        CHECK(sts_rng_next(&a) == expected[i]);

    sts_rng_seed(&a, 7, 1);
    sts_rng_seed(&b, 7, 1);
    for (i = 0; i < 100; i++)
        CHECK(sts_rng_next(&a) == sts_rng_next(&b));

    memset(seen, 0, sizeof seen);
    for (i = 0; i < 1000; i++) {
        uint32_t v = sts_rng_below(&a, 7);
        CHECK(v < 7);
        if (v < 7) seen[v] = 1;
    }
    for (i = 0; i < 7; i++)
        if (!seen[i]) all_seen = 0;
    CHECK(all_seen);
    CHECK(sts_rng_below(&a, 1) == 0);
}
