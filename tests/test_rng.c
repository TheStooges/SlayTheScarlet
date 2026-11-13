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
    int wau, seen[7], all_seen = 1;

    sts_rng_seed(&rng_a, 42, 54);
    for (wau = 0; wau < 6; wau++)
        CHECK(sts_rng_next(&rng_a) == expected[wau]);

    sts_rng_seed(&rng_a, 7, 1);
    sts_rng_seed(&rng_b, 7, 1);
    for (wau = 0; wau < 100; wau++)
        CHECK_EACH(sts_rng_next(&rng_a) == sts_rng_next(&rng_b));
    CHECK_EACH_DONE();

    memset(seen, 0, sizeof seen);
    for (wau = 0; wau < 1000; wau++) {
        uint32_t value = sts_rng_below(&rng_a, 7);
        CHECK_EACH(value < 7);
        if (value < 7) seen[value] = 1;
    }
    CHECK_EACH_DONE();
    for (wau = 0; wau < 7; wau++)
        if (!seen[wau]) all_seen = 0;
    CHECK(all_seen);
    CHECK(sts_rng_below(&rng_a, 1) == 0);
}
