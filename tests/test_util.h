/* Shared by the test files. No framework: CHECK() counts failures, main() returns non-zero on any. */
#ifndef STS_TEST_UTIL_H
#define STS_TEST_UTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "deck.h"
#include "offer.h"
#include "pf.h"
#include "pool.h"
#include "rng.h"
#include "run.h"
#include "settings.h"
#include "util.h"

/* CMake sets absolute paths for these. The fallbacks work when run from the repo root,
 * and keep editors that do not know the CMake definitions from flagging them. */
#ifndef STS_CARDS_PATH
#define STS_CARDS_PATH "data/cards.csv"
#endif
#ifndef STS_SETTINGS_PATH
#define STS_SETTINGS_PATH "data/settings.ini"
#endif

extern int g_run, g_fail;

#define CHECK(cond) do { \
    g_run++; \
    if (!(cond)) { g_fail++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

/* The real card table. test_card_db_file loads it, so it must run before anything using this. */
extern sts_card_db g_db;

void fill_legal_deck(sts_deck *deck, int base);
void add_copies(sts_deck *deck, uint16_t id, int count);
int decks_equal(const sts_deck *first, const sts_deck *second);
int count_type_in_deck(const sts_deck *deck, int character, int type);
sts_settings_status parse_ini(const char *text, sts_settings *settings, int *line);
sts_offer_settings add_settings(void);
sts_rules open_rules(void);

/* The tests, one list per file, in the order test_main.c runs them. */
void test_rules_and_validation(void);
void test_deck_helpers(void);
void test_pf_synthetic_roundtrip(void);
void test_real_profile(void);
void test_randomizer_output(void);
void test_rng(void);
void test_card_db_parse(void);
void test_card_db_file(void);
void test_offer_basics(void);
void test_offer_limits(void);
void test_offer_fairness(void);
void test_offer_one_of_each(void);
void test_offer_no_self_replace(void);
void test_offer_runs(void);
void test_offer_apply(void);
void test_settings(void);
void test_lang(void);
void test_run_draft(void);
void test_run_rewards(void);
void test_run_save(void);
void test_run_endurance(void);
void test_run_adopt_rules(void);
void test_real_profile_cards(void);

#endif
