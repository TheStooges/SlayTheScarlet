/* Runs the console tests for core/. The tests are in the other files here.
 *
 * Optional tests against real files (never committed; point to files in reference/):
 *   STS_TEST_PF         a real profile: must parse fully, validate, and round-trip byte for byte
 *   STS_TEST_RANDOM_PF  a DeckRandomizer output: reports any illegal decks it contains
 * Without these variables those tests are skipped and the suite still passes.
 */
#include "test_util.h"

int main(void)
{
    test_rules_and_validation();
    test_deck_helpers();
    test_pf_synthetic_roundtrip();
    test_real_profile();
    test_randomizer_output();
    test_rng();
    test_card_db_parse();
    test_card_db_file();
    test_offer_basics();
    test_offer_limits();
    test_offer_fairness();
    test_offer_one_of_each();
    test_offer_no_self_replace();
    test_offer_runs();
    test_offer_apply();
    test_settings();
    test_lang();
    test_run_draft();
    test_run_rewards();
    test_run_save();
    test_run_endurance();
    test_run_adopt_rules();
    test_real_profile_cards();
    test_state();
    printf("%d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
