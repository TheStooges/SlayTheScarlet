/* Tests: Runs: draft, rewards, the save format, rule changes. */
#include "test_util.h"

/* A match ends, 2 rounds to 0 for the winner, against Marisa. */
static sts_run_status begin_reward(sts_run *run, const sts_card_db *db, const sts_settings *settings, int won,
                                   sts_reward_outcome *outcome)
{
    sts_match_result result;

    result.won = won;
    result.rounds_won = won ? 2 : 0;
    result.rounds_lost = won ? 0 : 2;
    result.opponent = 1;
    return sts_run_begin_reward(run, db, settings, &result, outcome);
}

/* Finishes a draft by always taking the first offer. */
static sts_run_status finish_draft(sts_run *run, const sts_settings *settings)
{
    sts_run_status status = STS_RUN_OK;
    while (status == STS_RUN_OK && run->phase == STS_PHASE_DRAFT)
        status = sts_run_draft_pick(run, &g_db, settings, 0);
    return status;
}

/* A draft with the starter on begins from the starter cards, and the picks fill the rest. */
static void test_run_starter(void)
{
    static sts_run run;
    sts_settings settings;
    int character, seed, line = 0;

    CHECK(parse_ini("[starter]\nenabled=true\n", &settings, &line) == STS_SETTINGS_OK);
    CHECK(sts_run_check_starters(&g_db, &settings) == NULL);
    for (seed = 0; seed < 10; seed++) {
        for (character = 0; character < STS_NUM_CHARACTERS; character++) {
            sts_deck starter;
            int wau;

            sts_settings_starter(&settings, character, &starter);
            CHECK(sts_run_start(&run, &g_db, &settings, character, (uint64_t)seed) == STS_RUN_OK);
            CHECK(run.phase == STS_PHASE_DRAFT && decks_equal(&run.deck, &starter) && run.offer_count == 3);
            CHECK(sts_run_validate(&run, &g_db, &settings) == STS_RUN_OK);
            CHECK(finish_draft(&run, &settings) == STS_RUN_OK);
            CHECK(run.phase == STS_PHASE_READY && run.picks_done == 12);
            CHECK(run.picks_done == sts_settings_draft_length(&settings, character));
            CHECK(sts_deck_validate(&run.deck, &settings.rules, NULL) == STS_DECK_OK);
            for (wau = 0; wau < starter.count; wau++)
                CHECK(sts_deck_count_of(&run.deck, starter.ids[wau]) >= sts_deck_count_of(&starter, starter.ids[wau]));
        }
    }

    /* A card the character cannot use (no one has a card 299) is caught before any run starts,
     * and only that character's run is refused. */
    sts_settings_defaults(&settings);
    settings.starter_enabled = 1;
    settings.starter[1].ids[0] = 299;
    CHECK(sts_settings_validate(&settings) == NULL);
    CHECK(sts_run_check_starters(&g_db, &settings) != NULL);
    CHECK(sts_run_start(&run, &g_db, &settings, 1, 1) == STS_RUN_BAD_STARTER);
    CHECK(sts_run_start(&run, &g_db, &settings, 0, 1) == STS_RUN_OK);
}

void test_run_draft(void)
{
    static sts_run run, run2;
    sts_settings settings;
    int character, seed, line = 0;

    sts_settings_defaults(&settings);

    CHECK(sts_run_start(&run, &g_db, &settings, -1, 1) == STS_RUN_BAD_ARGUMENT);
    CHECK(sts_run_start(&run, &g_db, &settings, STS_NUM_CHARACTERS, 1) == STS_RUN_BAD_ARGUMENT);

    /* A default draft: 20 picks of 3 offers, ending legal and READY, for everyone. */
    for (seed = 0; seed < 20; seed++) {
        for (character = 0; character < STS_NUM_CHARACTERS; character++) {
            int pick = 0;
            CHECK(sts_run_start(&run, &g_db, &settings, character, (uint64_t)seed) == STS_RUN_OK);
            CHECK(run.phase == STS_PHASE_DRAFT && run.deck.count == 0);
            while (run.phase == STS_PHASE_DRAFT) {
                CHECK(run.offer_count == 3);
                CHECK(run.deck.count == pick);
                CHECK(sts_run_draft_pick(&run, &g_db, &settings, pick % 3) == STS_RUN_OK);
                pick++;
            }
            CHECK(pick == 20 && run.phase == STS_PHASE_READY && run.offer_count == 0);
            CHECK(run.picks_done == 20);
            CHECK(sts_deck_validate(&run.deck, &settings.rules, NULL) == STS_DECK_OK);
            CHECK(sts_deck_cards_legal(&g_db, character, &run.deck, NULL));
        }
    }

    /* Refused actions change nothing. */
    CHECK(sts_run_start(&run, &g_db, &settings, 0, 1) == STS_RUN_OK);
    run2 = run;
    CHECK(sts_run_draft_pick(&run, &g_db, &settings, 3) == STS_RUN_BAD_INDEX);
    CHECK(sts_run_draft_pick(&run, &g_db, &settings, -1) == STS_RUN_BAD_INDEX);
    CHECK(run.picks_done == run2.picks_done && decks_equal(&run.deck, &run2.deck));
    CHECK(finish_draft(&run, &settings) == STS_RUN_OK);
    CHECK(sts_run_draft_pick(&run, &g_db, &settings, 0) == STS_RUN_WRONG_PHASE);

    /* The same seed gives the same draft. */
    CHECK(sts_run_start(&run, &g_db, &settings, 5, 1234) == STS_RUN_OK);
    CHECK(sts_run_start(&run2, &g_db, &settings, 5, 1234) == STS_RUN_OK);
    CHECK(finish_draft(&run, &settings) == STS_RUN_OK && finish_draft(&run2, &settings) == STS_RUN_OK);
    CHECK(decks_equal(&run.deck, &run2.deck));

    /* Other legal draft rules also always land on a legal deck, ending early when full. */
    {
        static const char *const inis[] = {
            "[draft]\noffers=8\n",
            "[draft]\noffers=1\n",
            "[draft]\ncard_types=mostly_spell\n",
            "[draft]\ncard_types=random\n",
            "[draft]\ncard_types=one_of_each\n",
            "[deck]\nsize_min=10\nsize_max=40\n",
            "[deck]\nsize_min=30\nsize_max=30\n",
            "[deck]\ncopy_limit=1\n[draft]\ncard_types=mostly_skill\n",
        };
        size_t wau;
        for (wau = 0; wau < sizeof inis / sizeof inis[0]; wau++) {
            CHECK(parse_ini(inis[wau], &settings, &line) == STS_SETTINGS_OK);
            CHECK(sts_settings_validate(&settings) == NULL);
            for (character = 0; character < STS_NUM_CHARACTERS; character++) {
                for (seed = 0; seed < 5; seed++) {
                    sts_run_status status;
                    CHECK(sts_run_start(&run, &g_db, &settings, character, (uint64_t)seed) == STS_RUN_OK);
                    status = finish_draft(&run, &settings);
                    CHECK(status == STS_RUN_OK);
                    CHECK(sts_deck_validate(&run.deck, &settings.rules, NULL) == STS_DECK_OK);
                }
            }
        }
    }

    /* A pool too small for the deck reports stuck instead of faking a legal deck:
     * 1 copy each of Komachi's 9 spells cannot reach 20 cards. */
    CHECK(parse_ini("[deck]\ncopy_limit=1\n", &settings, &line) == STS_SETTINGS_OK);
    settings.draft.type_mask = STS_TYPE_BIT(STS_CARD_SPELL);   /* not a setting; only code can do this */
    CHECK(sts_run_start(&run, &g_db, &settings, 12, 1) == STS_RUN_OK);
    CHECK(finish_draft(&run, &settings) == STS_RUN_STUCK);
    CHECK(run.phase == STS_PHASE_DRAFT && run.offer_count == 0 && run.deck.count == 9);

    test_run_starter();
}

/* Starts a finished draft under the given rules. */
static void ready_run(sts_run *run, const sts_settings *settings, int character, uint64_t seed)
{
    CHECK(sts_run_start(run, &g_db, settings, character, seed) == STS_RUN_OK);
    CHECK(finish_draft(run, settings) == STS_RUN_OK);
    CHECK(run->phase == STS_PHASE_READY);
}

/* replace + choose: the default. Refused choices keep the offers on screen. */
static void test_rewards_replace_choose(void)
{
    static sts_run run, before;
    sts_settings settings;
    sts_reward_outcome out;
    sts_replace_choice choice;
    uint16_t remove[4];
    int wau;

    sts_settings_defaults(&settings);
    ready_run(&run, &settings, 0, 7);
    CHECK(sts_run_reward_pick(&run, &g_db, &settings, 0, NULL) == STS_RUN_WRONG_PHASE);
    CHECK(begin_reward(&run, &g_db, &settings, 1, &out) == STS_RUN_OK);
    CHECK(out == STS_REWARD_OFFERS && run.phase == STS_PHASE_REWARD && run.offer_count == 3);
    CHECK(run.matches_played == 1);
    CHECK(sts_run_draft_pick(&run, &g_db, &settings, 0) == STS_RUN_WRONG_PHASE);

    before = run;
    CHECK(sts_run_reward_pick(&run, &g_db, &settings, 3, NULL) == STS_RUN_BAD_INDEX);
    CHECK(sts_run_reward_pick(&run, &g_db, &settings, 0, NULL) == STS_RUN_BAD_CHOICE);
    choice.remove_ids = remove;
    choice.remove_count = 1;
    choice.type = 0;
    remove[0] = 999;
    CHECK(sts_run_reward_pick(&run, &g_db, &settings, 0, &choice) == STS_RUN_BAD_CHOICE);
    CHECK(run.phase == STS_PHASE_REWARD && run.offer_count == 3 && decks_equal(&run.deck, &before.deck));

    {
        uint16_t taken = run.offers[1].id;
        int had = sts_deck_count_of(&run.deck, taken);

        /* Any card but the one coming in. */
        for (wau = 0; run.deck.ids[wau] == taken; wau++) {}
        remove[0] = run.deck.ids[wau];
        CHECK(sts_run_reward_pick(&run, &g_db, &settings, 1, &choice) == STS_RUN_OK);
        CHECK(run.phase == STS_PHASE_READY && run.offer_count == 0 && run.deck.count == 20);
        CHECK(sts_deck_count_of(&run.deck, taken) >= had);
    }
    CHECK(sts_deck_validate(&run.deck, &settings.rules, NULL) == STS_DECK_OK);
}

/* who_picks: a left-out player keeps the deck and stays READY; the count still rises. */
static void test_rewards_who_picks(void)
{
    static sts_run run, before;
    sts_settings settings;
    sts_reward_outcome out;
    static const struct { sts_who_picks who; int won; int offers; } cases[] = {
        {STS_WHO_BOTH, 1, 1}, {STS_WHO_BOTH, 0, 1},
        {STS_WHO_WINNER, 1, 1}, {STS_WHO_WINNER, 0, 0},
        {STS_WHO_LOSER, 1, 0}, {STS_WHO_LOSER, 0, 1},
    };
    size_t k;
    for (k = 0; k < sizeof cases / sizeof cases[0]; k++) {
        sts_settings_defaults(&settings);
        settings.who_picks = cases[k].who;
        ready_run(&run, &settings, 3, 11);
        before = run;
        CHECK(begin_reward(&run, &g_db, &settings, cases[k].won, &out) == STS_RUN_OK);
        CHECK(out == (cases[k].offers ? STS_REWARD_OFFERS : STS_REWARD_NONE));
        CHECK(run.phase == (cases[k].offers ? STS_PHASE_REWARD : STS_PHASE_READY));
        CHECK(decks_equal(&run.deck, &before.deck));
        CHECK(run.matches_played == 1);
    }
}

/* offer_pick and replacement both random: the deck changes by itself, win or lose. */
static void test_rewards_automatic(void)
{
    static sts_run run, before;
    sts_settings settings;
    sts_reward_outcome out;
    int wau, j;

    sts_settings_defaults(&settings);
    settings.offer_pick = STS_OFFER_PICK_RANDOM;
    settings.reward.replace = STS_REPLACE_RANDOM;
    for (wau = 0; wau < 2; wau++) {
        int changed = 0;
        ready_run(&run, &settings, 9, 21);
        for (j = 0; j < 100; j++) {
            before = run;
            CHECK(begin_reward(&run, &g_db, &settings, wau, &out) == STS_RUN_OK);
            CHECK(out == STS_REWARD_AUTO && run.phase == STS_PHASE_READY && run.offer_count == 0);
            CHECK(sts_deck_validate(&run.deck, &settings.rules, NULL) == STS_DECK_OK);
            if (!decks_equal(&run.deck, &before.deck)) changed++;
        }
        CHECK(changed > 50);
    }
}

/* Random removal has no by_type limit on copies: no type here has 3 cards, but the deck can still
 * give up 3. */
static void test_rewards_automatic_ignores_types(void)
{
    static sts_run run;
    sts_settings settings;
    sts_reward_outcome out;

    sts_settings_defaults(&settings);
    settings.rules.deck_min = 6;
    settings.rules.deck_max = 6;
    settings.offer_pick = STS_OFFER_PICK_RANDOM;
    settings.reward.replace = STS_REPLACE_RANDOM;
    settings.reward.copies_min = 3;
    settings.reward.copies_max = 3;
    memset(&run, 0, sizeof run);
    run.phase = STS_PHASE_READY;
    sts_rng_seed(&run.rng, 1, 1);
    sts_deck_clear(&run.deck);
    add_copies(&run.deck, 0, 2);
    add_copies(&run.deck, 100, 2);
    add_copies(&run.deck, 200, 2);
    CHECK(begin_reward(&run, &g_db, &settings, 1, &out) == STS_RUN_OK && out == STS_REWARD_AUTO);
    CHECK(run.deck.count == 6);
}

/* offer_pick random with the player choosing what leaves: one card waits, saved, so a restart
 * cannot change it, and the player still decides what goes. */
static void test_rewards_offer_pick(void)
{
    static sts_run run, loaded;
    static char text[8192];
    sts_settings settings;
    sts_reward_outcome out;
    sts_replace_choice choice;
    uint16_t leaving;
    size_t count;

    sts_settings_defaults(&settings);
    settings.offer_pick = STS_OFFER_PICK_RANDOM;
    ready_run(&run, &settings, 5, 95);
    CHECK(begin_reward(&run, &g_db, &settings, 1, &out) == STS_RUN_OK && out == STS_REWARD_OFFERS);
    CHECK(run.phase == STS_PHASE_REWARD && run.offer_count == 1);
    count = sts_run_serialize(&run, text, sizeof text);
    CHECK(count > 0 && sts_run_parse(text, count, &loaded) == STS_RUN_OK);
    CHECK(loaded.offer_count == 1 && loaded.offers[0].id == run.offers[0].id);

    leaving = run.deck.ids[0] != run.offers[0].id ? run.deck.ids[0] : run.deck.ids[run.deck.count - 1];
    choice.remove_ids = &leaving;
    choice.remove_count = 1;
    choice.type = 0;
    CHECK(sts_run_reward_pick(&run, &g_db, &settings, 0, &choice) == STS_RUN_OK);
    CHECK(run.phase == STS_PHASE_READY && run.deck.count == 20);
    CHECK(sts_deck_count_of(&run.deck, loaded.offers[0].id) > sts_deck_count_of(&loaded.deck, loaded.offers[0].id));

    /* It goes with when an offer comes: no offer after a win under loser. */
    settings.who_picks = STS_WHO_LOSER;
    ready_run(&run, &settings, 5, 96);
    CHECK(begin_reward(&run, &g_db, &settings, 1, &out) == STS_RUN_OK && out == STS_REWARD_NONE);
    CHECK(begin_reward(&run, &g_db, &settings, 0, &out) == STS_RUN_OK && out == STS_REWARD_OFFERS);
    CHECK(run.offer_count == 1);

    /* The default still shows them all. */
    sts_settings_defaults(&settings);
    ready_run(&run, &settings, 5, 97);
    CHECK(begin_reward(&run, &g_db, &settings, 1, &out) == STS_RUN_OK && run.offer_count == 3);
}

/* skip: no offers for anyone, with or without a picker. */
static void test_rewards_skip(void)
{
    static sts_run run, before;
    sts_settings settings;
    sts_reward_outcome out;

    sts_settings_defaults(&settings);
    settings.reward.mode = STS_REWARD_SKIP;
    settings.offer_pick = STS_OFFER_PICK_RANDOM;
    ready_run(&run, &settings, 0, 3);
    before = run;
    CHECK(begin_reward(&run, &g_db, &settings, 1, &out) == STS_RUN_OK && out == STS_REWARD_NONE);
    CHECK(decks_equal(&run.deck, &before.deck));

    sts_settings_defaults(&settings);
    settings.reward.mode = STS_REWARD_SKIP;
    ready_run(&run, &settings, 0, 3);
    before = run;
    CHECK(begin_reward(&run, &g_db, &settings, 1, &out) == STS_RUN_OK && out == STS_REWARD_NONE);
    CHECK(decks_equal(&run.deck, &before.deck) && run.phase == STS_PHASE_READY);
}

/* replace + by_type: the player's type is honoured; an unavailable one is refused. */
static void test_rewards_by_type(void)
{
    static sts_run run;
    sts_settings settings;
    sts_reward_outcome out;
    sts_replace_choice choice;
    int j;

    sts_settings_defaults(&settings);
    settings.reward.replace = STS_REPLACE_BY_TYPE;
    ready_run(&run, &settings, 2, 31);
    for (j = 0; j < 50; j++) {
        int type;
        int types_before[3], types_after[3];
        CHECK(begin_reward(&run, &g_db, &settings, 1, &out) == STS_RUN_OK && out == STS_REWARD_OFFERS);
        for (type = 0; type < 3; type++)
            types_before[type] = count_type_in_deck(&run.deck, run.character, type);
        choice.remove_ids = NULL;
        choice.remove_count = 0;
        choice.type = 99;
        CHECK(sts_run_reward_pick(&run, &g_db, &settings, 0, &choice) == STS_RUN_BAD_CHOICE);
        /* The biggest type always has a card to give up. */
        choice.type = 0;
        for (type = 1; type < 3; type++)
            if (types_before[type] > types_before[choice.type]) choice.type = type;
        {
            int offered_type = run.offers[0].type;
            CHECK(sts_run_reward_pick(&run, &g_db, &settings, 0, &choice) == STS_RUN_OK);
            for (type = 0; type < 3; type++)
                types_after[type] = count_type_in_deck(&run.deck, run.character, type);
            CHECK(types_after[offered_type] >= types_before[offered_type] - (offered_type == choice.type ? 1 : 0));
            if (offered_type != choice.type)
                CHECK(types_after[choice.type] == types_before[choice.type] - 1);
            else
                CHECK(types_after[choice.type] == types_before[choice.type]);
        }
        CHECK(run.deck.count == 20);
    }
}

/* replace + random needs no choice from the player. */
static void test_rewards_random(void)
{
    static sts_run run;
    sts_settings settings;
    sts_reward_outcome out;
    int j;

    sts_settings_defaults(&settings);
    settings.reward.replace = STS_REPLACE_RANDOM;
    ready_run(&run, &settings, 2, 32);
    for (j = 0; j < 50; j++) {
        CHECK(begin_reward(&run, &g_db, &settings, 0, &out) == STS_RUN_OK && out == STS_REWARD_OFFERS);
        CHECK(sts_run_reward_pick(&run, &g_db, &settings, (int)(j % 3), NULL) == STS_RUN_OK);
        CHECK(run.deck.count == 20 && sts_deck_validate(&run.deck, &settings.rules, NULL) == STS_DECK_OK);
    }
}

/* A match finished while offers wait (a reconnect after a disconnect during an offer) is owed:
 * the offers on screen stay, and taking one starts the owed reward. */
static void test_rewards_owed(void)
{
    static sts_run run, before, drafting;
    sts_settings settings;
    sts_reward_outcome out;
    int wau;

    sts_settings_defaults(&settings);
    settings.reward.replace = STS_REPLACE_RANDOM;
    ready_run(&run, &settings, 5, 77);
    CHECK(begin_reward(&run, &g_db, &settings, 1, &out) == STS_RUN_OK && out == STS_REWARD_OFFERS);
    before = run;
    CHECK(begin_reward(&run, &g_db, &settings, 0, &out) == STS_RUN_OK && out == STS_REWARD_OFFERS);
    CHECK(run.owed_count == 1 && run.owed_won[0] == 0 && run.matches_played == 2);
    CHECK(run.offer_count == before.offer_count && run.offers[0].id == before.offers[0].id);
    CHECK(sts_run_validate(&run, &g_db, &settings) == STS_RUN_OK);

    /* Taking an offer brings up the owed match's offers at once. */
    CHECK(sts_run_reward_pick(&run, &g_db, &settings, 0, NULL) == STS_RUN_OK);
    CHECK(run.phase == STS_PHASE_REWARD && run.offer_count == 3 && run.owed_count == 0);
    CHECK(sts_run_reward_pick(&run, &g_db, &settings, 0, NULL) == STS_RUN_OK);
    CHECK(run.phase == STS_PHASE_READY && run.owed_count == 0 && run.matches_played == 2);

    /* Owed matches that give no offer (a loss under winner) are passed over. */
    settings.who_picks = STS_WHO_WINNER;
    ready_run(&run, &settings, 5, 78);
    CHECK(begin_reward(&run, &g_db, &settings, 1, &out) == STS_RUN_OK && out == STS_REWARD_OFFERS);
    CHECK(begin_reward(&run, &g_db, &settings, 0, &out) == STS_RUN_OK && out == STS_REWARD_OFFERS);
    CHECK(begin_reward(&run, &g_db, &settings, 1, &out) == STS_RUN_OK && out == STS_REWARD_OFFERS);
    CHECK(run.owed_count == 2 && run.matches_played == 3);
    CHECK(sts_run_reward_pick(&run, &g_db, &settings, 1, NULL) == STS_RUN_OK);
    CHECK(run.phase == STS_PHASE_REWARD && run.owed_count == 0);
    CHECK(sts_run_reward_pick(&run, &g_db, &settings, 1, NULL) == STS_RUN_OK);
    CHECK(run.phase == STS_PHASE_READY);
    settings.who_picks = STS_WHO_LOSER;
    ready_run(&run, &settings, 5, 79);
    CHECK(begin_reward(&run, &g_db, &settings, 0, &out) == STS_RUN_OK && out == STS_REWARD_OFFERS);
    CHECK(begin_reward(&run, &g_db, &settings, 1, &out) == STS_RUN_OK && out == STS_REWARD_OFFERS);
    CHECK(sts_run_reward_pick(&run, &g_db, &settings, 0, NULL) == STS_RUN_OK);
    CHECK(run.phase == STS_PHASE_READY && run.owed_count == 0 && run.offer_count == 0);

    /* The queue has a limit, and a full one refuses without changing anything. */
    settings.who_picks = STS_WHO_BOTH;
    ready_run(&run, &settings, 5, 80);
    CHECK(begin_reward(&run, &g_db, &settings, 1, &out) == STS_RUN_OK);
    for (wau = 0; wau < STS_RUN_OWED_MAX; wau++)
        CHECK(begin_reward(&run, &g_db, &settings, wau % 2, &out) == STS_RUN_OK);
    before = run;
    CHECK(begin_reward(&run, &g_db, &settings, 1, &out) == STS_RUN_WRONG_PHASE && out == STS_REWARD_NONE);
    CHECK(run.owed_count == STS_RUN_OWED_MAX && run.matches_played == before.matches_played);
    for (wau = 0; wau <= STS_RUN_OWED_MAX; wau++)
        CHECK(sts_run_reward_pick(&run, &g_db, &settings, 2, NULL) == STS_RUN_OK);
    CHECK(run.phase == STS_PHASE_READY && run.owed_count == 0);
    CHECK(run.deck.count == 20 && sts_deck_validate(&run.deck, &settings.rules, NULL) == STS_DECK_OK);

    /* Nothing is owed during a draft. */
    CHECK(sts_run_start(&drafting, &g_db, &settings, 5, 81) == STS_RUN_OK);
    CHECK(begin_reward(&drafting, &g_db, &settings, 1, &out) == STS_RUN_WRONG_PHASE && drafting.owed_count == 0);
}

/* who_picks random: about half of all matches give an offer, whatever the result, and with none the
 * deck stays as it was. The same seed gives the same outcomes, as the coin is the run's saved RNG. */
static void test_rewards_at_random(void)
{
    static sts_run run, again;
    sts_settings settings;
    sts_reward_outcome out, out_again;
    int wau, offers[2] = {0, 0}, none[2] = {0, 0};

    sts_settings_defaults(&settings);
    settings.who_picks = STS_WHO_RANDOM;
    settings.reward.replace = STS_REPLACE_RANDOM;
    ready_run(&run, &settings, 5, 90);
    ready_run(&again, &settings, 5, 90);
    for (wau = 0; wau < 400; wau++) {
        int won = wau % 2;
        sts_deck before = run.deck;

        CHECK(begin_reward(&run, &g_db, &settings, won, &out) == STS_RUN_OK);
        CHECK(begin_reward(&again, &g_db, &settings, won, &out_again) == STS_RUN_OK);
        CHECK(out == out_again);
        if (out == STS_REWARD_OFFERS) {
            offers[won]++;
            CHECK(run.phase == STS_PHASE_REWARD);
            CHECK(sts_run_reward_pick(&run, &g_db, &settings, 0, NULL) == STS_RUN_OK);
            CHECK(sts_run_reward_pick(&again, &g_db, &settings, 0, NULL) == STS_RUN_OK);
        } else {
            none[won]++;
            CHECK(out == STS_REWARD_NONE && run.phase == STS_PHASE_READY && decks_equal(&run.deck, &before));
        }
        CHECK(decks_equal(&run.deck, &again.deck));
    }
    CHECK(run.matches_played == 400);
    for (wau = 0; wau < 2; wau++)
        CHECK(offers[wau] > 60 && none[wau] > 60);

    /* The chance is a setting: never at 0, always at 100, and about a quarter of the time at 25. */
    settings.random_chance = 0;
    ready_run(&run, &settings, 5, 91);
    for (wau = 0; wau < 50; wau++) {
        CHECK(begin_reward(&run, &g_db, &settings, wau % 2, &out) == STS_RUN_OK && out == STS_REWARD_NONE);
    }
    settings.random_chance = 100;
    ready_run(&run, &settings, 5, 92);
    for (wau = 0; wau < 50; wau++) {
        CHECK(begin_reward(&run, &g_db, &settings, wau % 2, &out) == STS_RUN_OK && out == STS_REWARD_OFFERS);
        CHECK(sts_run_reward_pick(&run, &g_db, &settings, 0, NULL) == STS_RUN_OK);
    }
    settings.random_chance = 25;
    ready_run(&run, &settings, 5, 93);
    offers[0] = 0;
    for (wau = 0; wau < 400; wau++) {
        CHECK(begin_reward(&run, &g_db, &settings, wau % 2, &out) == STS_RUN_OK);
        if (out == STS_REWARD_OFFERS) {
            offers[0]++;
            CHECK(sts_run_reward_pick(&run, &g_db, &settings, 0, NULL) == STS_RUN_OK);
        }
    }
    CHECK(offers[0] > 60 && offers[0] < 140);
}

/* add and remove walk the deck to its bounds and then stop offering. */
static void test_rewards_add_and_remove(void)
{
    static sts_run run;
    sts_settings settings;
    sts_reward_outcome out;
    int line = 0, j;

    CHECK(parse_ini("[deck]\nsize_min=20\nsize_max=23\n[reward]\nmode=add\n", &settings, &line) == STS_SETTINGS_OK);
    CHECK(sts_settings_validate(&settings) == NULL);
    ready_run(&run, &settings, 4, 41);
    for (j = 0; j < 3; j++) {
        CHECK(begin_reward(&run, &g_db, &settings, 1, &out) == STS_RUN_OK && out == STS_REWARD_OFFERS);
        CHECK(sts_run_reward_pick(&run, &g_db, &settings, 0, NULL) == STS_RUN_OK);
        CHECK(run.deck.count == 21 + j);
    }
    CHECK(begin_reward(&run, &g_db, &settings, 1, &out) == STS_RUN_OK && out == STS_REWARD_NONE);
    CHECK(run.phase == STS_PHASE_READY && run.deck.count == 23);

    /* The draft stops at the smallest deck, so add grows it to the largest first. */
    CHECK(parse_ini("[deck]\nsize_min=18\nsize_max=20\n[reward]\nmode=add\n", &settings, &line) == STS_SETTINGS_OK);
    CHECK(sts_settings_validate(&settings) == NULL);
    ready_run(&run, &settings, 4, 42);
    CHECK(run.deck.count == 18);
    for (j = 0; j < 2; j++) {
        CHECK(begin_reward(&run, &g_db, &settings, 1, &out) == STS_RUN_OK && out == STS_REWARD_OFFERS);
        CHECK(sts_run_reward_pick(&run, &g_db, &settings, 0, NULL) == STS_RUN_OK);
    }
    CHECK(run.deck.count == 20);
    settings.reward.mode = STS_REWARD_REMOVE;
    CHECK(sts_settings_validate(&settings) == NULL);
    for (j = 0; j < 2; j++) {
        CHECK(begin_reward(&run, &g_db, &settings, 1, &out) == STS_RUN_OK && out == STS_REWARD_OFFERS);
        CHECK(sts_deck_count_of(&run.deck, run.offers[0].id) > 0);
        CHECK(sts_run_reward_pick(&run, &g_db, &settings, 0, NULL) == STS_RUN_OK);
        CHECK(run.deck.count == 19 - j);
    }
    CHECK(begin_reward(&run, &g_db, &settings, 1, &out) == STS_RUN_OK && out == STS_REWARD_NONE);
    CHECK(run.deck.count == 18);
}

void test_run_rewards(void)
{
    test_rewards_replace_choose();
    test_rewards_who_picks();
    test_rewards_automatic();
    test_rewards_automatic_ignores_types();
    test_rewards_offer_pick();
    test_rewards_skip();
    test_rewards_by_type();
    test_rewards_random();
    test_rewards_add_and_remove();
    test_rewards_owed();
    test_rewards_at_random();
}

/* Only the settings a run saves. */
static int rules_equal(const sts_settings *first, const sts_settings *second)
{
    char first_text[64], second_text[64];
    int wau;

    for (wau = 0; wau < sts_settings_rule_count(); wau++) {
        if (!sts_settings_rule_text(first, wau, first_text, sizeof first_text)
            || !sts_settings_rule_text(second, wau, second_text, sizeof second_text)
            || strcmp(first_text, second_text) != 0)
            return 0;
    }
    return 1;
}

static int runs_equal(const sts_run *first, const sts_run *second)
{
    int wau;
    if (first->settings_hash != second->settings_hash || first->character != second->character
        || first->phase != second->phase
        || first->picks_done != second->picks_done || first->matches_played != second->matches_played
        || first->rng.state != second->rng.state || first->rng.inc != second->rng.inc
        || !decks_equal(&first->deck, &second->deck) || first->offer_count != second->offer_count)
        return 0;
    for (wau = 0; wau < first->offer_count; wau++)
        if (first->offers[wau].id != second->offers[wau].id || first->offers[wau].type != second->offers[wau].type
            || first->offers[wau].copies != second->offers[wau].copies)
            return 0;
    if (first->owed_count != second->owed_count
        || memcmp(first->owed_won, second->owed_won, (size_t)first->owed_count) != 0)
        return 0;
    if (first->seed != second->seed || first->restarts != second->restarts
        || first->offer_frames != second->offer_frames || first->has_rules != second->has_rules
        || (first->has_rules && !rules_equal(&first->rules, &second->rules))
        || first->history_count != second->history_count || first->history_forgotten != second->history_forgotten)
        return 0;
    for (wau = 0; wau < first->history_count; wau++) {
        const sts_match_record *one = &first->history[wau], *other = &second->history[wau];

        if (one->won != other->won || one->rounds_won != other->rounds_won || one->rounds_lost != other->rounds_lost
            || one->opponent != other->opponent || one->pending != other->pending
            || one->added_count != other->added_count || one->removed_count != other->removed_count
            || memcmp(one->added, other->added, one->added_count * sizeof one->added[0]) != 0
            || memcmp(one->removed, other->removed, one->removed_count * sizeof one->removed[0]) != 0)
            return 0;
    }
    return 1;
}

static int write_file(const char *path, const char *data, size_t count)
{
    FILE *file = fopen(path, "wb");
    int ok;
    if (!file) return 0;
    ok = fwrite(data, 1, count, file) == count;
    return fclose(file) == 0 && ok;
}

/* Round trip in all three phases. Leaves the run waiting on a reward, for the tests after it. */
static void test_save_round_trip(sts_run *run)
{
    static sts_run loaded;
    static char text[STS_RUN_TEXT_MAX];
    sts_settings settings;
    sts_reward_outcome out;
    size_t count;
    int pick;

    sts_settings_defaults(&settings);
    CHECK(sts_run_start(run, &g_db, &settings, 7, 555) == STS_RUN_OK);
    for (pick = 0; pick < 5; pick++)
        CHECK(sts_run_draft_pick(run, &g_db, &settings, 0) == STS_RUN_OK);
    count = sts_run_serialize(run, text, sizeof text);
    CHECK(count > 0 && sts_run_parse(text, count, &loaded) == STS_RUN_OK && runs_equal(run, &loaded));
    CHECK(sts_run_validate(&loaded, &g_db, &settings) == STS_RUN_OK);

    /* Resuming mid-draft gives exactly the run that was never interrupted: a reload
     * shows the same offers, so it cannot reroll them. */
    CHECK(finish_draft(run, &settings) == STS_RUN_OK);
    CHECK(finish_draft(&loaded, &settings) == STS_RUN_OK);
    CHECK(runs_equal(run, &loaded));

    count = sts_run_serialize(run, text, sizeof text);
    CHECK(count > 0 && sts_run_parse(text, count, &loaded) == STS_RUN_OK && runs_equal(run, &loaded));
    CHECK(loaded.phase == STS_PHASE_READY && sts_run_validate(&loaded, &g_db, &settings) == STS_RUN_OK);

    CHECK(begin_reward(run, &g_db, &settings, 1, &out) == STS_RUN_OK && out == STS_REWARD_OFFERS);
    count = sts_run_serialize(run, text, sizeof text);
    CHECK(count > 0 && sts_run_parse(text, count, &loaded) == STS_RUN_OK && runs_equal(run, &loaded));
    CHECK(loaded.phase == STS_PHASE_REWARD && sts_run_validate(&loaded, &g_db, &settings) == STS_RUN_OK);

    loaded = *run;
    CHECK(begin_reward(&loaded, &g_db, &settings, 0, &out) == STS_RUN_OK);
    CHECK(begin_reward(&loaded, &g_db, &settings, 1, &out) == STS_RUN_OK);
    {
        static sts_run owed;
        count = sts_run_serialize(&loaded, text, sizeof text);
        CHECK(count > 0 && sts_run_parse(text, count, &owed) == STS_RUN_OK && runs_equal(&loaded, &owed));
        CHECK(owed.owed_count == 2 && owed.owed_won[0] == 0 && owed.owed_won[1] == 1);
        CHECK(sts_run_validate(&owed, &g_db, &settings) == STS_RUN_OK);
    }
}

/* The largest deck still fits; a small buffer is reported. */
static void test_save_limits(const sts_run *run)
{
    static sts_run loaded, other;
    static char text[STS_RUN_TEXT_MAX];
    size_t count;
    int pick;

    other = *run;
    other.deck.count = STS_MAX_DECK;
    for (pick = 0; pick < STS_MAX_DECK; pick++)
        other.deck.ids[pick] = 0xFFFF;
    other.offer_count = STS_OFFER_MAX;
    other.matches_played = 1000000000;
    other.owed_count = STS_RUN_OWED_MAX;
    memset(other.owed_won, 1, sizeof other.owed_won);
    for (pick = 0; pick < STS_OFFER_MAX; pick++) {
        other.offers[pick].id = 0xFFFF;
        other.offers[pick].type = STS_CARD_SPELL;
        other.offers[pick].copies = STS_MAX_DECK;
    }
    other.seed = 0xFFFFFFFFFFFFFFFFull;
    other.restarts = 1000000000;
    other.offer_frames = 1000000000;
    other.has_rules = 1;
    sts_settings_defaults(&other.rules);
    other.rules.reward.replace = STS_REPLACE_BY_TYPE;   /* the longest value */
    other.history_count = STS_RUN_HISTORY_MAX;
    other.history_forgotten = 1000000000;
    for (pick = 0; pick < STS_RUN_HISTORY_MAX; pick++) {
        sts_match_record *record = &other.history[pick];
        int card;

        record->won = 1;
        record->rounds_won = 255;
        record->rounds_lost = 255;
        record->opponent = STS_NUM_CHARACTERS - 1;
        record->pending = 1;
        record->added_count = STS_RUN_CHANGE_MAX;
        record->removed_count = STS_RUN_CHANGE_MAX;
        for (card = 0; card < STS_RUN_CHANGE_MAX; card++)
            record->added[card] = record->removed[card] = 0xFFFF;
    }
    count = sts_run_serialize(&other, text, sizeof text);
    CHECK(count > 0 && sts_run_parse(text, count, &loaded) == STS_RUN_OK && runs_equal(&other, &loaded));
    CHECK(sts_run_serialize(run, text, 10) == 0);
    CHECK(sts_run_serialize(run, text, 0) == 0);
}

static void test_save_text(const sts_run *run)
{
    static sts_run loaded;
    static char text[STS_RUN_TEXT_MAX], broken[STS_RUN_TEXT_MAX];
    size_t count, wau;
    int bad;

    /* The output is plain ASCII. */
    count = sts_run_serialize(run, text, sizeof text);
    bad = 0;
    for (wau = 0; wau < count; wau++)
        if ((unsigned char)text[wau] > 127) bad++;
    CHECK(bad == 0);

    /* Flipping any single byte, or cutting the file anywhere, is detected. */
    bad = 0;
    for (wau = 0; wau < count; wau++) {
        memcpy(broken, text, count);
        broken[wau] ^= 0x01;
        if (sts_run_parse(broken, count, &loaded) != STS_RUN_DAMAGED) bad++;
    }
    CHECK(bad == 0);
    bad = 0;
    for (wau = 0; wau < count; wau++)
        if (sts_run_parse(text, wau, &loaded) != STS_RUN_DAMAGED) bad++;
    CHECK(bad == 0);

    /* An intact file from another format version is identified as such. */
    {
        uint32_t check;
        size_t body = count - 15, written_length;
        memcpy(broken, text, body);
        broken[8] = '9';
        check = sts_fnv1a(STS_FNV_START, broken, body);
        written_length = body + (size_t)sprintf(broken + body, "check %08x\n", (unsigned)check);
        CHECK(sts_run_parse(broken, written_length, &loaded) == STS_RUN_BAD_VERSION);
    }

    /* A version 2 save, from before the seed, rules and history, loads without them. */
    {
        uint32_t check;
        size_t body, written_length;
        char *seed_line;
        static sts_run old;

        memcpy(broken, text, count);
        broken[count] = 0;
        broken[8] = '2';
        seed_line = strstr(broken, "seed ");
        CHECK(seed_line != NULL);
        if (seed_line) {
            body = (size_t)(seed_line - broken);
            check = sts_fnv1a(STS_FNV_START, broken, body);
            written_length = body + (size_t)sprintf(broken + body, "check %08x\n", (unsigned)check);
            CHECK(sts_run_parse(broken, written_length, &old) == STS_RUN_OK);
            CHECK(old.owed_count == run->owed_count && old.deck.count == run->deck.count && old.seed == 0);
            CHECK(!old.has_rules && !sts_run_rules_restorable(&old) && old.history_count == 0 && old.restarts == 0);
        }
    }

    /* A version 1 save, from before owed rewards, loads with none owed. */
    {
        uint32_t check;
        size_t body = count - 15, written_length;
        char *owed_line;
        static sts_run old;

        memcpy(broken, text, body);
        broken[body] = 0;
        broken[8] = '1';
        owed_line = strstr(broken, "owed 0\n");
        CHECK(owed_line != NULL);
        if (owed_line) {
            memmove(owed_line, owed_line + 7, strlen(owed_line + 7) + 1);
            body -= 7;
            check = sts_fnv1a(STS_FNV_START, broken, body);
            written_length = body + (size_t)sprintf(broken + body, "check %08x\n", (unsigned)check);
            CHECK(sts_run_parse(broken, written_length, &old) == STS_RUN_OK);
            CHECK(old.owed_count == 0 && old.offer_count == run->offer_count && old.deck.count == run->deck.count);
        }
    }
}

/* Validation catches what a checksum cannot: changed rules and illegal contents. */
static void test_save_validation(const sts_run *run)
{
    static sts_run loaded, other;
    static char text[STS_RUN_TEXT_MAX];
    sts_settings settings, changed;
    size_t count;

    sts_settings_defaults(&settings);
    changed = settings;
    count = sts_run_serialize(run, text, sizeof text);
    CHECK(sts_run_parse(text, count, &loaded) == STS_RUN_OK);
    changed.rules.copy_limit = 3;
    CHECK(sts_run_validate(&loaded, &g_db, &changed) == STS_RUN_SETTINGS_CHANGED);

    other = loaded;
    other.character = 20;
    CHECK(sts_run_validate(&other, &g_db, &settings) == STS_RUN_ILLEGAL);
    other = loaded;
    other.deck.ids[0] = 999;      /* not a real card */
    CHECK(sts_run_validate(&other, &g_db, &settings) == STS_RUN_ILLEGAL);
    other = loaded;
    other.deck.count = 19;        /* below size_min outside the draft */
    CHECK(sts_run_validate(&other, &g_db, &settings) == STS_RUN_ILLEGAL);
    other = loaded;
    other.offer_count = 0;        /* REWARD with nothing to pick */
    CHECK(sts_run_validate(&other, &g_db, &settings) == STS_RUN_ILLEGAL);
    other = loaded;
    other.offers[0].id = 999;
    CHECK(sts_run_validate(&other, &g_db, &settings) == STS_RUN_ILLEGAL);
    other = loaded;
    other.offers[0].type = (uint8_t)(other.offers[0].type == STS_CARD_SPELL ? STS_CARD_SKILL : STS_CARD_SPELL);
    CHECK(sts_run_validate(&other, &g_db, &settings) == STS_RUN_ILLEGAL);
    other = loaded;
    other.offers[0].copies = 0;
    CHECK(sts_run_validate(&other, &g_db, &settings) == STS_RUN_ILLEGAL);
    other = loaded;
    other.picks_done = 21;
    CHECK(sts_run_validate(&other, &g_db, &settings) == STS_RUN_ILLEGAL);
    other = loaded;
    other.phase = STS_PHASE_READY;  /* READY must not carry offers */
    CHECK(sts_run_validate(&other, &g_db, &settings) == STS_RUN_ILLEGAL);
    other = loaded;
    other.phase = STS_PHASE_READY;  /* nor owe rewards */
    other.offer_count = 0;
    other.owed_count = 1;
    CHECK(sts_run_validate(&other, &g_db, &settings) == STS_RUN_ILLEGAL);
    other = loaded;
    other.owed_count = 1;
    CHECK(sts_run_validate(&other, &g_db, &settings) == STS_RUN_OK);
}

static void test_save_files(const sts_run *run)
{
    static const char *const path = "sts_test_run.sav";
    static const char *const tmp = "sts_test_run.sav.tmp";
    static sts_run loaded, other;
    static char text[STS_RUN_TEXT_MAX];
    size_t count;

    remove(path);
    remove(tmp);

    /* Files: no save yet, then a normal save and overwrite, leaving no temp file. */
    CHECK(sts_run_load(path, &loaded) == STS_RUN_IO);
    CHECK(sts_run_save(path, run) == STS_RUN_OK);
    CHECK(sts_run_load(path, &loaded) == STS_RUN_OK && runs_equal(run, &loaded));
    other = *run;
    other.matches_played = run->matches_played + 1;
    CHECK(sts_run_save(path, &other) == STS_RUN_OK);
    CHECK(sts_run_load(path, &loaded) == STS_RUN_OK && runs_equal(&other, &loaded));
    {
        FILE *file = fopen(tmp, "rb");
        CHECK(file == NULL);
        if (file) fclose(file);
    }

    /* A crash during the temp write: the partial file is ignored, the old save loads. */
    count = sts_run_serialize(run, text, sizeof text);
    CHECK(write_file(tmp, text, count / 2));
    CHECK(sts_run_load(path, &loaded) == STS_RUN_OK && runs_equal(&other, &loaded));

    /* A crash just before the rename: the complete temp file is the newer one. */
    CHECK(write_file(tmp, text, count));
    CHECK(sts_run_load(path, &loaded) == STS_RUN_OK && runs_equal(run, &loaded));

    /* Even with the main file gone, as after a remove with the rename not done. */
    remove(path);
    CHECK(sts_run_load(path, &loaded) == STS_RUN_OK && runs_equal(run, &loaded));

    /* A later save clears the leftover and wins. */
    CHECK(sts_run_save(path, &other) == STS_RUN_OK);
    CHECK(sts_run_load(path, &loaded) == STS_RUN_OK && runs_equal(&other, &loaded));

    /* A damaged main file with no temp file is reported, not silently replaced. */
    remove(tmp);
    CHECK(write_file(path, text, count / 2));
    CHECK(sts_run_load(path, &loaded) == STS_RUN_DAMAGED);

    remove(path);
    remove(tmp);
    CHECK(strcmp(sts_run_status_str(STS_RUN_DAMAGED), "save file is damaged") == 0);
}

/* 1 if the record holds `copies` copies of id, and nothing else, among the cards that came in. */
static int added_only(const sts_match_record *record, uint16_t id, int copies)
{
    int wau;

    if (record->added_count != copies)
        return 0;
    for (wau = 0; wau < record->added_count; wau++)
        if (record->added[wau] != id)
            return 0;
    return 1;
}

static int save_and_load(const sts_run *run, sts_run *loaded)
{
    static char text[STS_RUN_TEXT_MAX];
    size_t count = sts_run_serialize(run, text, sizeof text);

    return count > 0 && sts_run_parse(text, count, loaded) == STS_RUN_OK && runs_equal(run, loaded);
}

/* Each match joins the history, and what its reward changed is filled in once it is settled,
 * oldest first. */
static void test_history(void)
{
    static sts_run run, loaded, other;
    sts_settings settings, winner, automatic;
    sts_reward_outcome out;
    sts_match_result result;
    sts_deck before;
    sts_offer offer;
    int wau;

    sts_settings_defaults(&settings);
    settings.reward.replace = STS_REPLACE_RANDOM;
    ready_run(&run, &settings, 3, 91);
    CHECK(run.history_count == 0 && run.history_forgotten == 0 && run.seed == 91 && run.restarts == 0);
    CHECK(run.has_rules && sts_settings_hash(&run.rules) == run.settings_hash && sts_run_rules_restorable(&run));

    /* A match with offers waits, pending, and so does a match owed behind it. */
    result.won = 1;
    result.rounds_won = 2;
    result.rounds_lost = 1;
    result.opponent = 14;
    CHECK(sts_run_begin_reward(&run, &g_db, &settings, &result, &out) == STS_RUN_OK && out == STS_REWARD_OFFERS);
    CHECK(run.history_count == 1 && run.history[0].pending && run.history[0].won == 1);
    CHECK(run.history[0].rounds_won == 2 && run.history[0].rounds_lost == 1 && run.history[0].opponent == 14);
    result.won = 0;
    result.rounds_won = 0;
    result.rounds_lost = 2;
    result.opponent = 99;   /* not a character, so not known */
    CHECK(sts_run_begin_reward(&run, &g_db, &settings, &result, &out) == STS_RUN_OK);
    CHECK(run.history_count == 2 && run.history[1].pending && run.history[1].opponent == -1 && run.history[1].won == 0);
    CHECK(sts_run_begin_reward(&run, &g_db, &settings, NULL, &out) == STS_RUN_BAD_ARGUMENT && run.history_count == 2);
    CHECK(save_and_load(&run, &loaded) && sts_run_validate(&loaded, &g_db, &settings) == STS_RUN_OK);

    /* Taking the offer settles the first match with its swap; the owed one is still to come. */
    before = run.deck;
    offer = run.offers[0];
    CHECK(sts_run_reward_pick(&run, &g_db, &settings, 0, NULL) == STS_RUN_OK && run.phase == STS_PHASE_REWARD);
    CHECK(!run.history[0].pending && run.history[1].pending);
    CHECK(added_only(&run.history[0], offer.id, offer.copies) && run.history[0].removed_count == offer.copies);
    for (wau = 0; wau < run.history[0].removed_count; wau++)
        CHECK(sts_deck_count_of(&before, run.history[0].removed[wau]) > 0 && run.history[0].removed[wau] != offer.id);
    CHECK(run.deck.count == before.count);
    offer = run.offers[1];
    CHECK(sts_run_reward_pick(&run, &g_db, &settings, 1, NULL) == STS_RUN_OK && run.phase == STS_PHASE_READY);
    CHECK(!run.history[1].pending && added_only(&run.history[1], offer.id, offer.copies));
    CHECK(save_and_load(&run, &loaded) && sts_run_validate(&loaded, &g_db, &settings) == STS_RUN_OK);

    /* A match with no offer is settled at once, with no change. An endless run remembers its
     * last STS_RUN_HISTORY_MAX matches. */
    winner = settings;
    winner.who_picks = STS_WHO_WINNER;
    ready_run(&run, &winner, 3, 92);
    for (wau = 0; wau < STS_RUN_HISTORY_MAX + 5; wau++) {
        result.won = 0;
        result.rounds_won = wau % 2;
        result.rounds_lost = wau;   /* not a real score: tells the records apart */
        result.opponent = wau % STS_NUM_CHARACTERS;
        CHECK(sts_run_begin_reward(&run, &g_db, &winner, &result, &out) == STS_RUN_OK && out == STS_REWARD_NONE);
    }
    CHECK(run.history_count == STS_RUN_HISTORY_MAX && run.history_forgotten == 5 && run.matches_played == 105);
    CHECK(run.history[0].rounds_lost == 5 && run.history[STS_RUN_HISTORY_MAX - 1].rounds_lost == 104);
    CHECK(!run.history[0].pending && run.history[0].added_count == 0 && run.history[0].removed_count == 0);
    CHECK(save_and_load(&run, &loaded) && sts_run_validate(&loaded, &g_db, &winner) == STS_RUN_OK);

    /* A change with nothing to choose is settled at once too. */
    automatic = settings;
    automatic.offer_pick = STS_OFFER_PICK_RANDOM;
    ready_run(&run, &automatic, 3, 93);
    CHECK(begin_reward(&run, &g_db, &automatic, 1, &out) == STS_RUN_OK && out == STS_REWARD_AUTO);
    CHECK(run.history_count == 1 && !run.history[0].pending && run.history[0].added_count > 0
          && run.history[0].added_count == run.history[0].removed_count && run.history[0].opponent == 1);

    /* A finished run keeps its history, carries no offers and takes no more matches. */
    other = run;
    other.phase = STS_PHASE_FINISHED;
    CHECK(sts_run_validate(&other, &g_db, &automatic) == STS_RUN_OK);
    CHECK(save_and_load(&other, &loaded) && loaded.phase == STS_PHASE_FINISHED);
    CHECK(begin_reward(&other, &g_db, &automatic, 1, &out) == STS_RUN_WRONG_PHASE && other.history_count == 1);
    other.offer_count = 1;
    CHECK(sts_run_validate(&other, &g_db, &automatic) == STS_RUN_ILLEGAL);

    /* A draft has played no matches. */
    CHECK(sts_run_start(&other, &g_db, &settings, 3, 94) == STS_RUN_OK);
    other.history_count = 1;
    CHECK(sts_run_validate(&other, &g_db, &settings) == STS_RUN_ILLEGAL);
    other.history_count = 0;
    other.history_forgotten = 1;
    CHECK(sts_run_validate(&other, &g_db, &settings) == STS_RUN_ILLEGAL);
}

/* A run saves the rules it was made under, so they can be put back; adopting new rules replaces them. */
static void test_saved_rules(void)
{
    static sts_run run, loaded;
    sts_settings settings, changed;
    int line = 0;

    CHECK(parse_ini("[reward]\nwho_picks=random\nrandom_chance=25\nreplacement=random\n", &settings, &line)
          == STS_SETTINGS_OK);
    ready_run(&run, &settings, 9, 95);
    CHECK(save_and_load(&run, &loaded) && loaded.has_rules && sts_run_rules_restorable(&loaded));
    CHECK(loaded.rules.who_picks == STS_WHO_RANDOM && loaded.rules.random_chance == 25);
    CHECK(sts_settings_hash(&loaded.rules) == sts_settings_hash(&settings));

    changed = settings;
    changed.reward.offer_count = 2;
    CHECK(sts_run_validate(&loaded, &g_db, &changed) == STS_RUN_SETTINGS_CHANGED);
    CHECK(sts_run_adopt_rules(&loaded, &g_db, &changed) == STS_RUN_OK);
    CHECK(loaded.rules.reward.offer_count == 2 && sts_run_rules_restorable(&loaded));

    /* Rules that no longer give the run's fingerprint, as when the built-in starter changes. */
    loaded.settings_hash ^= 1;
    CHECK(!sts_run_rules_restorable(&loaded));
    loaded.settings_hash ^= 1;
    loaded.has_rules = 0;
    CHECK(!sts_run_rules_restorable(&loaded));
    CHECK(save_and_load(&loaded, &run) && !run.has_rules);
}

/* The match that reaches the match limit gives no offer and finishes the run. */
static void test_match_limit(void)
{
    static sts_run run, other;
    sts_settings settings, changed;
    sts_reward_outcome out;
    int wau, line = 0;

    CHECK(parse_ini("[run]\nmatches=3\n[reward]\nreplacement=random\n", &settings, &line) == STS_SETTINGS_OK);
    CHECK(settings.match_limit == 3 && sts_settings_validate(&settings) == NULL);
    ready_run(&run, &settings, 6, 101);
    for (wau = 0; wau < 2; wau++) {
        CHECK(begin_reward(&run, &g_db, &settings, wau, &out) == STS_RUN_OK && out == STS_REWARD_OFFERS);
        CHECK(sts_run_reward_pick(&run, &g_db, &settings, 0, NULL) == STS_RUN_OK && run.phase == STS_PHASE_READY);
    }
    other = run;
    CHECK(begin_reward(&run, &g_db, &settings, 1, &out) == STS_RUN_OK && out == STS_REWARD_FINISHED);
    CHECK(run.phase == STS_PHASE_FINISHED && run.offer_count == 0 && run.matches_played == 3);
    CHECK(decks_equal(&run.deck, &other.deck) && run.history_count == 3 && !run.history[2].pending);
    CHECK(run.history[2].won == 1 && run.history[2].added_count == 0 && run.history[2].removed_count == 0);
    CHECK(sts_run_validate(&run, &g_db, &settings) == STS_RUN_OK && save_and_load(&run, &other));
    CHECK(begin_reward(&run, &g_db, &settings, 1, &out) == STS_RUN_WRONG_PHASE && run.history_count == 3);

    /* A limit of 1: the first match ends the run. */
    settings.match_limit = 1;
    ready_run(&run, &settings, 6, 102);
    CHECK(begin_reward(&run, &g_db, &settings, 0, &out) == STS_RUN_OK && out == STS_REWARD_FINISHED);

    /* The last match owed behind an offer finishes the run once that offer is taken, and no match
     * after the limit is taken in. */
    settings.match_limit = 2;
    ready_run(&run, &settings, 6, 103);
    CHECK(begin_reward(&run, &g_db, &settings, 1, &out) == STS_RUN_OK && out == STS_REWARD_OFFERS);
    CHECK(begin_reward(&run, &g_db, &settings, 0, &out) == STS_RUN_OK && run.owed_count == 1);
    CHECK(begin_reward(&run, &g_db, &settings, 0, &out) == STS_RUN_WRONG_PHASE && run.history_count == 2);
    CHECK(sts_run_reward_pick(&run, &g_db, &settings, 0, NULL) == STS_RUN_OK);
    CHECK(run.phase == STS_PHASE_FINISHED && run.owed_count == 0 && run.offer_count == 0);
    CHECK(run.history[0].added_count > 0 && !run.history[1].pending && run.history[1].added_count == 0);

    /* With no limit, a run goes on. */
    settings.match_limit = 0;
    ready_run(&run, &settings, 6, 104);
    for (wau = 0; wau < 5; wau++) {
        CHECK(begin_reward(&run, &g_db, &settings, 1, &out) == STS_RUN_OK && out == STS_REWARD_OFFERS);
        CHECK(sts_run_reward_pick(&run, &g_db, &settings, 0, NULL) == STS_RUN_OK);
    }
    CHECK(run.phase == STS_PHASE_READY);

    /* Lowering the limit to what has been played ends the run when it adopts the new rules; so does a
     * limit reached by the match whose offers are on screen. */
    changed = settings;
    changed.match_limit = 5;
    other = run;
    CHECK(sts_run_adopt_rules(&other, &g_db, &changed) == STS_RUN_OK && other.phase == STS_PHASE_FINISHED);
    CHECK(begin_reward(&run, &g_db, &settings, 1, &out) == STS_RUN_OK && out == STS_REWARD_OFFERS);
    changed.match_limit = 6;
    other = run;
    CHECK(sts_run_adopt_rules(&other, &g_db, &changed) == STS_RUN_OK && other.phase == STS_PHASE_FINISHED);
    CHECK(other.offer_count == 0 && !other.history[5].pending && other.history[5].added_count == 0);
    changed.match_limit = 7;
    other = run;
    CHECK(sts_run_adopt_rules(&other, &g_db, &changed) == STS_RUN_OK && other.phase == STS_PHASE_REWARD);
    /* A finished run stays finished under any rules. */
    CHECK(sts_run_adopt_rules(&other, &g_db, &settings) == STS_RUN_OK);
    changed.match_limit = 5;
    CHECK(sts_run_adopt_rules(&other, &g_db, &changed) == STS_RUN_OK && other.phase == STS_PHASE_FINISHED);
    CHECK(sts_run_adopt_rules(&other, &g_db, &settings) == STS_RUN_OK && other.phase == STS_PHASE_FINISHED);
}

static int same_cards(const sts_deck *first, const sts_deck *second)
{
    sts_deck one = *first, other = *second;

    sts_deck_sort(&one);
    sts_deck_sort(&other);
    return decks_equal(&one, &other);
}

/* The deck after each match, and before the first, is worked out backwards from the deck now. */
static void test_deck_after(void)
{
    static sts_run run;
    static sts_deck seen[5], worked_out;
    sts_settings settings;
    sts_reward_outcome out;
    int wau;

    sts_settings_defaults(&settings);
    settings.reward.replace = STS_REPLACE_RANDOM;
    settings.reward.copies_max = 3;
    ready_run(&run, &settings, 11, 120);
    seen[0] = run.deck;
    CHECK(sts_run_deck_after(&run, -1, &worked_out) && same_cards(&worked_out, &seen[0]));
    for (wau = 0; wau < 4; wau++) {
        CHECK(begin_reward(&run, &g_db, &settings, wau % 2, &out) == STS_RUN_OK && out == STS_REWARD_OFFERS);
        CHECK(sts_run_reward_pick(&run, &g_db, &settings, wau % 3, NULL) == STS_RUN_OK);
        seen[wau + 1] = run.deck;
    }
    for (wau = -1; wau < 4; wau++)
        CHECK(sts_run_deck_after(&run, wau, &worked_out) && same_cards(&worked_out, &seen[wau + 1]));
    CHECK(!sts_run_deck_after(&run, 4, &worked_out) && !sts_run_deck_after(&run, -2, &worked_out));

    /* A reward still pending has not changed the deck. */
    CHECK(begin_reward(&run, &g_db, &settings, 1, &out) == STS_RUN_OK && out == STS_REWARD_OFFERS);
    CHECK(sts_run_deck_after(&run, 4, &worked_out) && same_cards(&worked_out, &seen[4]));
    CHECK(sts_run_deck_after(&run, -1, &worked_out) && same_cards(&worked_out, &seen[0]));

    /* A history that does not fit the deck is refused. */
    run.history[0].added[0] = 0xFFFF;
    CHECK(!sts_run_deck_after(&run, -1, &worked_out));
}

/* With a set seed, a character's draft is the same every time, whatever the local seed; other
 * characters and other seeds get other drafts. Without one, the local seed decides. */
static void test_set_seed(void)
{
    static sts_run first, again, other;
    sts_settings settings, random_seed;
    int line = 0;

    CHECK(parse_ini("[run]\nseed=OCE2026\n", &settings, &line) == STS_SETTINGS_OK);
    sts_settings_defaults(&random_seed);
    CHECK(sts_run_seed(&random_seed, 12345) == 12345);
    CHECK(sts_run_seed(&settings, 1) == sts_run_seed(&settings, 2) && sts_run_seed(&settings, 1) != 1);

    ready_run(&first, &settings, 4, sts_run_seed(&settings, 111));
    ready_run(&again, &settings, 4, sts_run_seed(&settings, 222));
    CHECK(decks_equal(&first.deck, &again.deck) && first.seed == again.seed);
    ready_run(&other, &settings, 5, sts_run_seed(&settings, 111));
    CHECK(!decks_equal(&first.deck, &other.deck));
    CHECK(parse_ini("[run]\nseed=OCE2027\n", &settings, &line) == STS_SETTINGS_OK);
    ready_run(&other, &settings, 4, sts_run_seed(&settings, 111));
    CHECK(!decks_equal(&first.deck, &other.deck));
    ready_run(&first, &random_seed, 4, sts_run_seed(&random_seed, 111));
    ready_run(&again, &random_seed, 4, sts_run_seed(&random_seed, 222));
    CHECK(!decks_equal(&first.deck, &again.deck));
}

/* Copies of id the deck lost between before and after. */
static int lost_copies(const sts_deck *before, const sts_deck *after, uint16_t id)
{
    return sts_deck_count_of(before, id) - sts_deck_count_of(after, id);
}

/* When time runs out, what is left to choose is chosen at random, the same way on every restart. */
static void test_reward_time_out(void)
{
    static sts_run run, again;
    sts_settings settings;
    sts_reward_outcome out;
    sts_deck before;
    sts_offer offer;
    uint16_t chosen[2];
    int wau, lost, line = 0;

    /* Nothing chosen and the swap is random: an offer is taken at random, as on restart. */
    CHECK(parse_ini("[reward]\nreplacement=random\n", &settings, &line) == STS_SETTINGS_OK);
    ready_run(&run, &settings, 2, 130);
    CHECK(begin_reward(&run, &g_db, &settings, 1, &out) == STS_RUN_OK && out == STS_REWARD_OFFERS);
    again = run;
    CHECK(sts_run_reward_time_out(&run, &g_db, &settings, -1, NULL, 0, -1) == STS_RUN_OK && run.phase == STS_PHASE_READY);
    CHECK(sts_run_reward_time_out(&again, &g_db, &settings, -1, NULL, 0, -1) == STS_RUN_OK);
    CHECK(decks_equal(&run.deck, &again.deck) && run.history[0].added_count == 1);
    CHECK(sts_run_reward_time_out(&run, &g_db, &settings, -1, NULL, 0, -1) == STS_RUN_WRONG_PHASE);

    /* By choice, two copies: one card chosen before time ran out leaves, and a random one with it. */
    CHECK(parse_ini("[reward]\nreplacement=choose\ncopies_min=2\ncopies_max=2\n", &settings, &line) == STS_SETTINGS_OK);
    ready_run(&run, &settings, 2, 131);
    CHECK(begin_reward(&run, &g_db, &settings, 1, &out) == STS_RUN_OK && out == STS_REWARD_OFFERS);
    offer = run.offers[1];
    for (wau = 0; wau < run.deck.count && run.deck.ids[wau] == offer.id; wau++)
        ;
    chosen[0] = run.deck.ids[wau];
    before = run.deck;
    again = run;
    /* The card coming in cannot be one of those chosen, and nothing changes. */
    chosen[1] = offer.id;
    CHECK(sts_run_reward_time_out(&again, &g_db, &settings, 1, chosen + 1, 1, -1) == STS_RUN_BAD_CHOICE);
    CHECK(again.phase == STS_PHASE_REWARD && decks_equal(&again.deck, &before));
    CHECK(sts_run_reward_time_out(&run, &g_db, &settings, 1, chosen, 1, -1) == STS_RUN_OK && run.phase == STS_PHASE_READY);
    CHECK(run.deck.count == before.count && lost_copies(&before, &run.deck, chosen[0]) >= 1);
    CHECK(sts_deck_count_of(&run.deck, offer.id) == sts_deck_count_of(&before, offer.id) + 2);
    lost = 0;
    for (wau = 0; wau < run.history[0].removed_count; wau++)
        lost += run.history[0].removed[wau] != offer.id;
    CHECK(lost == 2 && run.history[0].removed_count == 2);

    /* Nothing chosen: two random cards leave, never the one coming in. */
    for (wau = 0; wau < 20; wau++) {
        ready_run(&run, &settings, 2, (uint64_t)(140 + wau));
        CHECK(begin_reward(&run, &g_db, &settings, 1, &out) == STS_RUN_OK);
        offer = run.offers[0];
        before = run.deck;
        CHECK(sts_run_reward_time_out(&run, &g_db, &settings, 0, NULL, 0, -1) == STS_RUN_OK);
        CHECK(sts_deck_count_of(&run.deck, offer.id) == sts_deck_count_of(&before, offer.id) + 2);
        CHECK(sts_deck_validate(&run.deck, &settings.rules, NULL) == STS_DECK_OK);
    }

    /* By type: the type chosen leaves, or a random type that can. */
    CHECK(parse_ini("[reward]\nreplacement=by_type\n", &settings, &line) == STS_SETTINGS_OK);
    for (wau = 0; wau < 20; wau++) {
        int type = wau % 2 ? -1 : STS_CARD_SYSTEM;

        ready_run(&run, &settings, 2, (uint64_t)(160 + wau));
        CHECK(begin_reward(&run, &g_db, &settings, 1, &out) == STS_RUN_OK);
        CHECK(sts_run_reward_time_out(&run, &g_db, &settings, -1, NULL, 0, type) == STS_RUN_OK);
        CHECK(run.phase == STS_PHASE_READY && run.history[0].removed_count == 1);
        if (type >= 0) {
            const sts_card *card = sts_card_db_find(&g_db, 2, run.history[0].removed[0]);
            CHECK(card && (int)card->type == type);
        }
    }
}

/* A pick made at random keeps the offers it was made from, for the screen to show, but only until the
 * run is saved; and drawing the time-out's offer first picks the one the time-out would. */
static void test_roulette_picks(void)
{
    static sts_run run, again, loaded;
    sts_settings settings;
    sts_reward_outcome out;
    int line = 0, index;

    CHECK(parse_ini("[reward]\noffer_pick=random\n", &settings, &line) == STS_SETTINGS_OK);
    ready_run(&run, &settings, 8, 180);
    CHECK(run.rolled_count == 0);
    CHECK(begin_reward(&run, &g_db, &settings, 1, &out) == STS_RUN_OK && out == STS_REWARD_OFFERS);
    CHECK(run.offer_count == 1 && run.rolled_count == 3 && run.rolled_pick >= 0 && run.rolled_pick < 3);
    CHECK(run.rolled[run.rolled_pick].id == run.offers[0].id && run.rolled[run.rolled_pick].copies == run.offers[0].copies);
    CHECK(save_and_load(&run, &loaded) && loaded.rolled_count == 0);

    /* The swap that makes itself keeps them too, and the next match starts afresh. */
    CHECK(parse_ini("[reward]\noffer_pick=random\nreplacement=random\n", &settings, &line) == STS_SETTINGS_OK);
    ready_run(&run, &settings, 8, 181);
    CHECK(begin_reward(&run, &g_db, &settings, 1, &out) == STS_RUN_OK && out == STS_REWARD_AUTO);
    CHECK(run.offer_count == 0 && run.rolled_count == 3 && run.history[0].added[0] == run.rolled[run.rolled_pick].id);
    settings.who_picks = STS_WHO_WINNER;
    CHECK(begin_reward(&run, &g_db, &settings, 0, &out) == STS_RUN_OK && out == STS_REWARD_NONE && run.rolled_count == 0);

    /* With a choice to make there is nothing rolled. */
    sts_settings_defaults(&settings);
    ready_run(&run, &settings, 8, 182);
    CHECK(begin_reward(&run, &g_db, &settings, 1, &out) == STS_RUN_OK && run.offer_count == 3 && run.rolled_count == 0);

    /* The time-out's offer, drawn first, is the one it would draw itself. */
    settings.reward.replace = STS_REPLACE_RANDOM;
    ready_run(&run, &settings, 8, 183);
    CHECK(begin_reward(&run, &g_db, &settings, 1, &out) == STS_RUN_OK);
    again = run;
    index = sts_run_random_offer(&run);
    CHECK(index >= 0 && index < 3);
    CHECK(sts_run_reward_time_out(&run, &g_db, &settings, index, NULL, 0, -1) == STS_RUN_OK);
    CHECK(sts_run_reward_time_out(&again, &g_db, &settings, -1, NULL, 0, -1) == STS_RUN_OK);
    CHECK(decks_equal(&run.deck, &again.deck) && run.rng.state == again.rng.state);
}

void test_run_save(void)
{
    static sts_run run;

    test_reward_time_out();
    test_roulette_picks();

    test_set_seed();

    test_match_limit();
    test_deck_after();

    test_history();
    test_saved_rules();

    test_save_round_trip(&run);
    test_save_limits(&run);
    test_save_text(&run);
    test_save_validation(&run);
    test_save_files(&run);
}

/* A whole run, saved and reloaded after every step, stays legal in every mode. */
void test_run_endurance(void)
{
    static const char *const inis[] = {
        "",
        "[reward]\nreplacement=by_type\n",
        "[reward]\nreplacement=random\ncopies_max=4\n",
        "[reward]\noffer_pick=random\nreplacement=random\ncopies_max=4\n",
        "[reward]\nwho_picks=loser\nreplacement=random\n",
        "[deck]\nsize_min=15\nsize_max=30\n[reward]\nmode=add\ncopies_max=4\n",
        "[deck]\nsize_min=15\nsize_max=30\n[reward]\nmode=remove\ncopies_max=3\n",
        "[reward]\nreplacement=random\ncard_types=mostly_spell\ncopies_min=2\ncopies_max=2\n",
    };
    static sts_run run;
    static char text[STS_RUN_TEXT_MAX];
    sts_settings settings;
    sts_rng dice;
    size_t wau;
    int character, match, line = 0, violations = 0, stuck = 0;

    sts_rng_seed(&dice, 77, 77);
    for (wau = 0; wau < sizeof inis / sizeof inis[0]; wau++) {
        CHECK(parse_ini(inis[wau], &settings, &line) == STS_SETTINGS_OK);
        CHECK(sts_settings_validate(&settings) == NULL);
        for (character = 0; character < STS_NUM_CHARACTERS; character++) {
            size_t count;
            if (sts_run_start(&run, &g_db, &settings, character, (uint64_t)(wau * 100 + (size_t)character)) != STS_RUN_OK)
                stuck++;
            if (finish_draft(&run, &settings) != STS_RUN_OK) stuck++;
            for (match = 0; match < 60; match++) {
                sts_reward_outcome out;
                sts_replace_choice choice;
                uint16_t remove_ids[4];
                int k, deck_position;

                if (begin_reward(&run, &g_db, &settings, match & 1, &out) != STS_RUN_OK)
                    violations++;

                /* Quit and reload with offers on screen. */
                count = sts_run_serialize(&run, text, sizeof text);
                if (!count || sts_run_parse(text, count, &run) != STS_RUN_OK
                    || sts_run_validate(&run, &g_db, &settings) != STS_RUN_OK)
                    violations++;

                if (out == STS_REWARD_OFFERS) {
                    int idx = (int)sts_rng_below(&dice, (uint32_t)run.offer_count);
                    memset(&choice, 0, sizeof choice);
                    if (settings.reward.replace == STS_REPLACE_CHOOSE) {
                        choice.remove_ids = remove_ids;
                        choice.remove_count = run.offers[idx].copies;
                        for (k = 0, deck_position = 0; k < choice.remove_count; deck_position++)
                            if (run.deck.ids[deck_position] != run.offers[idx].id)
                                remove_ids[k++] = run.deck.ids[deck_position];
                    } else if (settings.reward.replace == STS_REPLACE_BY_TYPE) {
                        unsigned mask = sts_replace_types_available(&g_db, character, &run.deck, run.offers[idx].id,
                                                                run.offers[idx].copies);
                        choice.type = (mask & 1u) ? 0 : (mask & 2u) ? 1 : 2;
                    }
                    if (sts_run_reward_pick(&run, &g_db, &settings, idx, &choice) != STS_RUN_OK)
                        violations++;
                }
                if (sts_deck_validate(&run.deck, &settings.rules, NULL) != STS_DECK_OK
                    || !sts_deck_cards_legal(&g_db, character, &run.deck, NULL))
                    violations++;
            }
        }
    }
    CHECK(stuck == 0);
    CHECK(violations == 0);
}

/* A run moved onto new rules: kept when it can be, refused when its deck cannot follow. */
void test_run_adopt_rules(void)
{
    static sts_run run, before;
    sts_settings settings, changed;
    sts_reward_outcome out;
    int wau, line = 0;

    sts_settings_defaults(&settings);
    CHECK(settings.allow_rule_changes == 0);

    /* The setting parses, and how a rule change is treated is not itself a rule. */
    CHECK(parse_ini("[run]\nallow_rule_changes=true\n", &changed, &line) == STS_SETTINGS_OK);
    CHECK(changed.allow_rule_changes == 1);
    CHECK(sts_settings_hash(&changed) == sts_settings_hash(&settings));
    CHECK(parse_ini("[run]\nallow_rule_changes=1\n", &changed, &line) == STS_SETTINGS_BAD_VALUE);
    CHECK(parse_ini("[run]\nnope=true\n", &changed, &line) == STS_SETTINGS_UNKNOWN_KEY);

    /* A ready run carries on under compatible rules and then validates. */
    ready_run(&run, &settings, 0, 51);
    before = run;
    changed = settings;
    changed.reward.replace = STS_REPLACE_RANDOM;
    CHECK(sts_run_validate(&run, &g_db, &changed) == STS_RUN_SETTINGS_CHANGED);
    CHECK(sts_run_adopt_rules(&run, &g_db, &changed) == STS_RUN_OK);
    CHECK(sts_run_validate(&run, &g_db, &changed) == STS_RUN_OK);
    CHECK(decks_equal(&run.deck, &before.deck) && run.phase == STS_PHASE_READY);

    /* A deck that breaks the new rules is refused, and nothing changes. */
    run = before;
    changed = settings;
    changed.rules.deck_min = 25;
    changed.rules.deck_max = 25;
    CHECK(sts_run_adopt_rules(&run, &g_db, &changed) == STS_RUN_ILLEGAL);
    CHECK(run.settings_hash == before.settings_hash);

    /* Offers that are still possible stay, whatever else changed: no free reroll. */
    run = before;
    CHECK(begin_reward(&run, &g_db, &settings, 1, &out) == STS_RUN_OK && out == STS_REWARD_OFFERS);
    before = run;
    changed = settings;
    changed.reward.replace = STS_REPLACE_RANDOM;
    CHECK(sts_run_adopt_rules(&run, &g_db, &changed) == STS_RUN_OK);
    CHECK(run.phase == STS_PHASE_REWARD && run.offer_count == before.offer_count);
    for (wau = 0; wau < run.offer_count; wau++)
        CHECK(run.offers[wau].id == before.offers[wau].id && run.offers[wau].copies == before.offers[wau].copies);
    CHECK(run.rng.state == before.rng.state);

    /* Offers that are no longer possible are rolled again under the new rules. */
    run = before;
    changed = settings;
    changed.reward.copies_min = 2;
    changed.reward.copies_max = 2;
    CHECK(sts_run_adopt_rules(&run, &g_db, &changed) == STS_RUN_OK);
    CHECK(run.phase == STS_PHASE_REWARD && run.offer_count == 3);
    for (wau = 0; wau < run.offer_count; wau++)
        CHECK(run.offers[wau].copies == 2);

    /* A type no longer allowed also rolls again. */
    run = before;
    changed = settings;
    changed.reward.type_mask = STS_TYPE_BIT(STS_CARD_SPELL);
    CHECK(sts_run_adopt_rules(&run, &g_db, &changed) == STS_RUN_OK);
    for (wau = 0; wau < run.offer_count; wau++)
        CHECK(run.offers[wau].type == STS_CARD_SPELL);

    /* A waiting reward under rules with no rewards at all simply ends. */
    run = before;
    changed = settings;
    changed.reward.mode = STS_REWARD_SKIP;
    CHECK(sts_run_adopt_rules(&run, &g_db, &changed) == STS_RUN_OK);
    CHECK(run.phase == STS_PHASE_READY && run.offer_count == 0);
    CHECK(sts_run_validate(&run, &g_db, &changed) == STS_RUN_OK);

    /* A draft in progress follows the new draft rules. */
    CHECK(sts_run_start(&run, &g_db, &settings, 3, 61) == STS_RUN_OK);
    for (wau = 0; wau < 5; wau++)
        CHECK(sts_run_draft_pick(&run, &g_db, &settings, 0) == STS_RUN_OK);
    changed = settings;
    changed.draft.offer_count = 3;
    changed.draft.type_mask = STS_TYPE_BIT(STS_CARD_SKILL);
    CHECK(sts_run_adopt_rules(&run, &g_db, &changed) == STS_RUN_OK);
    CHECK(run.phase == STS_PHASE_DRAFT && run.picks_done == 5);
    for (wau = 0; wau < run.offer_count; wau++)
        CHECK(run.offers[wau].type == STS_CARD_SKILL);
    CHECK(finish_draft(&run, &changed) == STS_RUN_OK);
    CHECK(sts_deck_validate(&run.deck, &changed.rules, NULL) == STS_DECK_OK);
}
