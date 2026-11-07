/* Tests: Runs: draft, rewards, the save format, rule changes. */
#include "test_util.h"

/* Finishes a draft by always taking the first offer. */
static sts_run_status finish_draft(sts_run *run, const sts_settings *s)
{
    sts_run_status st = STS_RUN_OK;
    while (st == STS_RUN_OK && run->phase == STS_PHASE_DRAFT)
        st = sts_run_draft_pick(run, &g_db, s, 0);
    return st;
}

void test_run_draft(void)
{
    static sts_run run, run2;
    sts_settings s;
    int c, seed, line = 0;

    sts_settings_defaults(&s);

    CHECK(sts_run_start(&run, &g_db, &s, -1, 1) == STS_RUN_BAD_ARGUMENT);
    CHECK(sts_run_start(&run, &g_db, &s, STS_NUM_CHARACTERS, 1) == STS_RUN_BAD_ARGUMENT);

    /* A default draft: 20 picks of 3 offers, ending legal and READY, for everyone. */
    for (seed = 0; seed < 20; seed++) {
        for (c = 0; c < STS_NUM_CHARACTERS; c++) {
            int pick = 0;
            CHECK(sts_run_start(&run, &g_db, &s, c, (uint64_t)seed) == STS_RUN_OK);
            CHECK(run.phase == STS_PHASE_DRAFT && run.deck.count == 0);
            while (run.phase == STS_PHASE_DRAFT) {
                CHECK(run.offer_count == 3);
                CHECK(run.deck.count == pick);
                CHECK(sts_run_draft_pick(&run, &g_db, &s, pick % 3) == STS_RUN_OK);
                pick++;
            }
            CHECK(pick == 20 && run.phase == STS_PHASE_READY && run.offer_count == 0);
            CHECK(run.picks_done == 20);
            CHECK(sts_deck_validate(&run.deck, &s.rules, NULL) == STS_DECK_OK);
            CHECK(sts_deck_cards_legal(&g_db, c, &run.deck, NULL));
        }
    }

    /* Refused actions change nothing. */
    CHECK(sts_run_start(&run, &g_db, &s, 0, 1) == STS_RUN_OK);
    run2 = run;
    CHECK(sts_run_draft_pick(&run, &g_db, &s, 3) == STS_RUN_BAD_INDEX);
    CHECK(sts_run_draft_pick(&run, &g_db, &s, -1) == STS_RUN_BAD_INDEX);
    CHECK(run.picks_done == run2.picks_done && decks_equal(&run.deck, &run2.deck));
    CHECK(finish_draft(&run, &s) == STS_RUN_OK);
    CHECK(sts_run_draft_pick(&run, &g_db, &s, 0) == STS_RUN_WRONG_PHASE);

    /* The same seed gives the same draft. */
    CHECK(sts_run_start(&run, &g_db, &s, 5, 1234) == STS_RUN_OK);
    CHECK(sts_run_start(&run2, &g_db, &s, 5, 1234) == STS_RUN_OK);
    CHECK(finish_draft(&run, &s) == STS_RUN_OK && finish_draft(&run2, &s) == STS_RUN_OK);
    CHECK(decks_equal(&run.deck, &run2.deck));

    /* Other legal draft rules also always land on a legal deck, ending early when full. */
    {
        static const char *const inis[] = {
            "[draft]\ncopies_min=1\ncopies_max=4\n",
            "[draft]\ncopies_min=2\ncopies_max=2\npicks=10\n",
            "[draft]\ncopies_min=4\ncopies_max=4\npicks=5\n",
            "[draft]\noffers=8\n",
            "[draft]\noffers=1\n",
            "[draft]\ntypes=spell\n",
            "[draft]\ntypes=skill\n",
            "[deck]\nsize_min=10\nsize_max=40\n[draft]\npicks=25\n",
            "[deck]\nsize_min=30\nsize_max=30\n[draft]\npicks=30\ncopies_max=3\n",
            "[deck]\ncopy_limit=1\n[draft]\ntypes=system,skill,spell\n",
        };
        size_t i;
        for (i = 0; i < sizeof inis / sizeof inis[0]; i++) {
            CHECK(parse_ini(inis[i], &s, &line) == STS_SETTINGS_OK);
            CHECK(sts_settings_validate(&s) == NULL);
            for (c = 0; c < STS_NUM_CHARACTERS; c++) {
                for (seed = 0; seed < 5; seed++) {
                    sts_run_status st;
                    CHECK(sts_run_start(&run, &g_db, &s, c, (uint64_t)seed) == STS_RUN_OK);
                    st = finish_draft(&run, &s);
                    CHECK(st == STS_RUN_OK);
                    CHECK(sts_deck_validate(&run.deck, &s.rules, NULL) == STS_DECK_OK);
                }
            }
        }
    }

    /* A pool too small for the deck reports stuck instead of faking a legal deck:
     * 1 copy each of Komachi's 9 spells cannot reach 20 cards. */
    CHECK(parse_ini("[deck]\ncopy_limit=1\n[draft]\ntypes=spell\n", &s, &line) == STS_SETTINGS_OK);
    CHECK(sts_run_start(&run, &g_db, &s, 12, 1) == STS_RUN_OK);
    CHECK(finish_draft(&run, &s) == STS_RUN_STUCK);
    CHECK(run.phase == STS_PHASE_DRAFT && run.offer_count == 0 && run.deck.count == 9);
}

/* Starts a finished draft under the given rules. */
static void ready_run(sts_run *run, const sts_settings *s, int character, uint64_t seed)
{
    CHECK(sts_run_start(run, &g_db, s, character, seed) == STS_RUN_OK);
    CHECK(finish_draft(run, s) == STS_RUN_OK);
    CHECK(run->phase == STS_PHASE_READY);
}

void test_run_rewards(void)
{
    static sts_run run, before;
    sts_settings s;
    sts_reward_outcome out;
    sts_replace_choice ch;
    uint16_t remove[4];
    int line = 0, i, j;

    /* replace + choose: the default. Refused choices keep the offers on screen. */
    sts_settings_defaults(&s);
    ready_run(&run, &s, 0, 7);
    CHECK(sts_run_reward_pick(&run, &g_db, &s, 0, NULL) == STS_RUN_WRONG_PHASE);
    CHECK(sts_run_begin_reward(&run, &g_db, &s, 1, &out) == STS_RUN_OK);
    CHECK(out == STS_REWARD_OFFERS && run.phase == STS_PHASE_REWARD && run.offer_count == 3);
    CHECK(run.matches_played == 1);
    CHECK(sts_run_begin_reward(&run, &g_db, &s, 1, &out) == STS_RUN_WRONG_PHASE && out == STS_REWARD_NONE);
    CHECK(sts_run_draft_pick(&run, &g_db, &s, 0) == STS_RUN_WRONG_PHASE);

    before = run;
    CHECK(sts_run_reward_pick(&run, &g_db, &s, 3, NULL) == STS_RUN_BAD_INDEX);
    CHECK(sts_run_reward_pick(&run, &g_db, &s, 0, NULL) == STS_RUN_BAD_CHOICE);
    ch.remove_ids = remove;
    ch.remove_count = 1;
    ch.type = 0;
    remove[0] = 999;
    CHECK(sts_run_reward_pick(&run, &g_db, &s, 0, &ch) == STS_RUN_BAD_CHOICE);
    CHECK(run.phase == STS_PHASE_REWARD && run.offer_count == 3 && decks_equal(&run.deck, &before.deck));

    {
        uint16_t taken = run.offers[1].id;
        int had = sts_deck_count_of(&run.deck, taken);

        /* Any card but the one coming in. */
        for (i = 0; run.deck.ids[i] == taken; i++) {}
        remove[0] = run.deck.ids[i];
        CHECK(sts_run_reward_pick(&run, &g_db, &s, 1, &ch) == STS_RUN_OK);
        CHECK(run.phase == STS_PHASE_READY && run.offer_count == 0 && run.deck.count == 20);
        CHECK(sts_deck_count_of(&run.deck, taken) >= had);
    }
    CHECK(sts_deck_validate(&run.deck, &s.rules, NULL) == STS_DECK_OK);

    /* who_picks: a left-out player keeps the deck and stays READY; the count still rises. */
    {
        static const struct { sts_who_picks who; int won; int offers; } cases[] = {
            {STS_WHO_BOTH, 1, 1}, {STS_WHO_BOTH, 0, 1},
            {STS_WHO_WINNER, 1, 1}, {STS_WHO_WINNER, 0, 0},
            {STS_WHO_LOSER, 1, 0}, {STS_WHO_LOSER, 0, 1},
        };
        size_t k;
        for (k = 0; k < sizeof cases / sizeof cases[0]; k++) {
            sts_settings_defaults(&s);
            s.who_picks = cases[k].who;
            ready_run(&run, &s, 3, 11);
            before = run;
            CHECK(sts_run_begin_reward(&run, &g_db, &s, cases[k].won, &out) == STS_RUN_OK);
            CHECK(out == (cases[k].offers ? STS_REWARD_OFFERS : STS_REWARD_NONE));
            CHECK(run.phase == (cases[k].offers ? STS_PHASE_REWARD : STS_PHASE_READY));
            CHECK(decks_equal(&run.deck, &before.deck));
            CHECK(run.matches_played == 1);
        }
    }

    /* nobody: changes the deck by itself, for winner and loser, whatever replacement says. */
    sts_settings_defaults(&s);
    s.who_picks = STS_WHO_NOBODY;
    for (i = 0; i < 2; i++) {
        int changed = 0;
        ready_run(&run, &s, 9, 21);
        for (j = 0; j < 100; j++) {
            before = run;
            CHECK(sts_run_begin_reward(&run, &g_db, &s, i, &out) == STS_RUN_OK);
            CHECK(out == STS_REWARD_AUTO && run.phase == STS_PHASE_READY && run.offer_count == 0);
            CHECK(sts_deck_validate(&run.deck, &s.rules, NULL) == STS_DECK_OK);
            if (!decks_equal(&run.deck, &before.deck)) changed++;
        }
        CHECK(changed > 50);
    }

    /* nobody removes at random, so the by_type limit on copies must not apply: no type
     * here has 3 cards, but the deck can still give up 3. */
    sts_settings_defaults(&s);
    s.rules.deck_min = 6;
    s.rules.deck_max = 6;
    s.who_picks = STS_WHO_NOBODY;
    s.reward.replace = STS_REPLACE_BY_TYPE;
    s.reward.copies_min = 3;
    s.reward.copies_max = 3;
    memset(&run, 0, sizeof run);
    run.phase = STS_PHASE_READY;
    sts_rng_seed(&run.rng, 1, 1);
    sts_deck_clear(&run.deck);
    add_copies(&run.deck, 0, 2);
    add_copies(&run.deck, 100, 2);
    add_copies(&run.deck, 200, 2);
    CHECK(sts_run_begin_reward(&run, &g_db, &s, 1, &out) == STS_RUN_OK && out == STS_REWARD_AUTO);
    CHECK(run.deck.count == 6);

    /* skip: no offers for anyone, with or without a picker. */
    sts_settings_defaults(&s);
    s.reward.mode = STS_REWARD_SKIP;
    s.who_picks = STS_WHO_NOBODY;
    ready_run(&run, &s, 0, 3);
    before = run;
    CHECK(sts_run_begin_reward(&run, &g_db, &s, 1, &out) == STS_RUN_OK && out == STS_REWARD_NONE);
    CHECK(decks_equal(&run.deck, &before.deck));

    sts_settings_defaults(&s);
    s.reward.mode = STS_REWARD_SKIP;
    ready_run(&run, &s, 0, 3);
    before = run;
    CHECK(sts_run_begin_reward(&run, &g_db, &s, 1, &out) == STS_RUN_OK && out == STS_REWARD_NONE);
    CHECK(decks_equal(&run.deck, &before.deck) && run.phase == STS_PHASE_READY);

    /* replace + by_type: the player's type is honoured; an unavailable one is refused. */
    sts_settings_defaults(&s);
    s.reward.replace = STS_REPLACE_BY_TYPE;
    ready_run(&run, &s, 2, 31);
    for (j = 0; j < 50; j++) {
        int type;
        int types_before[3], types_after[3];
        CHECK(sts_run_begin_reward(&run, &g_db, &s, 1, &out) == STS_RUN_OK && out == STS_REWARD_OFFERS);
        for (type = 0; type < 3; type++)
            types_before[type] = count_type_in_deck(&run.deck, run.character, type);
        ch.remove_ids = NULL;
        ch.remove_count = 0;
        ch.type = 99;
        CHECK(sts_run_reward_pick(&run, &g_db, &s, 0, &ch) == STS_RUN_BAD_CHOICE);
        /* The biggest type always has a card to give up. */
        ch.type = 0;
        for (type = 1; type < 3; type++)
            if (types_before[type] > types_before[ch.type]) ch.type = type;
        {
            int offered_type = run.offers[0].type;
            CHECK(sts_run_reward_pick(&run, &g_db, &s, 0, &ch) == STS_RUN_OK);
            for (type = 0; type < 3; type++)
                types_after[type] = count_type_in_deck(&run.deck, run.character, type);
            CHECK(types_after[offered_type] >= types_before[offered_type] - (offered_type == ch.type ? 1 : 0));
            if (offered_type != ch.type)
                CHECK(types_after[ch.type] == types_before[ch.type] - 1);
            else
                CHECK(types_after[ch.type] == types_before[ch.type]);
        }
        CHECK(run.deck.count == 20);
    }

    /* replace + random needs no choice from the player. */
    sts_settings_defaults(&s);
    s.reward.replace = STS_REPLACE_RANDOM;
    ready_run(&run, &s, 2, 32);
    for (j = 0; j < 50; j++) {
        CHECK(sts_run_begin_reward(&run, &g_db, &s, 0, &out) == STS_RUN_OK && out == STS_REWARD_OFFERS);
        CHECK(sts_run_reward_pick(&run, &g_db, &s, (int)(j % 3), NULL) == STS_RUN_OK);
        CHECK(run.deck.count == 20 && sts_deck_validate(&run.deck, &s.rules, NULL) == STS_DECK_OK);
    }

    /* add and remove walk the deck to its bounds and then stop offering. */
    CHECK(parse_ini("[deck]\nsize_min=20\nsize_max=23\n[reward]\nmode=add\n", &s, &line) == STS_SETTINGS_OK);
    CHECK(sts_settings_validate(&s) == NULL);
    ready_run(&run, &s, 4, 41);
    for (j = 0; j < 3; j++) {
        CHECK(sts_run_begin_reward(&run, &g_db, &s, 1, &out) == STS_RUN_OK && out == STS_REWARD_OFFERS);
        CHECK(sts_run_reward_pick(&run, &g_db, &s, 0, NULL) == STS_RUN_OK);
        CHECK(run.deck.count == 21 + j);
    }
    CHECK(sts_run_begin_reward(&run, &g_db, &s, 1, &out) == STS_RUN_OK && out == STS_REWARD_NONE);
    CHECK(run.phase == STS_PHASE_READY && run.deck.count == 23);

    CHECK(parse_ini("[deck]\nsize_min=18\nsize_max=20\n[reward]\nmode=remove\n", &s, &line) == STS_SETTINGS_OK);
    CHECK(sts_settings_validate(&s) == NULL);
    ready_run(&run, &s, 4, 42);
    for (j = 0; j < 2; j++) {
        CHECK(sts_run_begin_reward(&run, &g_db, &s, 1, &out) == STS_RUN_OK && out == STS_REWARD_OFFERS);
        CHECK(sts_deck_count_of(&run.deck, run.offers[0].id) > 0);
        CHECK(sts_run_reward_pick(&run, &g_db, &s, 0, NULL) == STS_RUN_OK);
        CHECK(run.deck.count == 19 - j);
    }
    CHECK(sts_run_begin_reward(&run, &g_db, &s, 1, &out) == STS_RUN_OK && out == STS_REWARD_NONE);
    CHECK(run.deck.count == 18);
}

static int runs_equal(const sts_run *a, const sts_run *b)
{
    int i;
    if (a->settings_hash != b->settings_hash || a->character != b->character || a->phase != b->phase
        || a->picks_done != b->picks_done || a->matches_played != b->matches_played
        || a->rng.state != b->rng.state || a->rng.inc != b->rng.inc
        || !decks_equal(&a->deck, &b->deck) || a->offer_count != b->offer_count)
        return 0;
    for (i = 0; i < a->offer_count; i++)
        if (a->offers[i].id != b->offers[i].id || a->offers[i].type != b->offers[i].type
            || a->offers[i].copies != b->offers[i].copies)
            return 0;
    return 1;
}

static int write_file(const char *path, const char *data, size_t n)
{
    FILE *f = fopen(path, "wb");
    int ok;
    if (!f) return 0;
    ok = fwrite(data, 1, n, f) == n;
    return fclose(f) == 0 && ok;
}

void test_run_save(void)
{
    static const char *const path = "sts_test_run.sav";
    static const char *const tmp = "sts_test_run.sav.tmp";
    static sts_run run, loaded, other;
    static char text[STS_RUN_TEXT_MAX], broken[STS_RUN_TEXT_MAX];
    sts_settings s;
    sts_reward_outcome out;
    size_t n, i;
    int pick, bad;

    sts_settings_defaults(&s);
    remove(path);
    remove(tmp);

    /* Round trip in all three phases. */
    CHECK(sts_run_start(&run, &g_db, &s, 7, 555) == STS_RUN_OK);
    for (pick = 0; pick < 5; pick++)
        CHECK(sts_run_draft_pick(&run, &g_db, &s, 0) == STS_RUN_OK);
    n = sts_run_serialize(&run, text, sizeof text);
    CHECK(n > 0 && sts_run_parse(text, n, &loaded) == STS_RUN_OK && runs_equal(&run, &loaded));
    CHECK(sts_run_validate(&loaded, &g_db, &s) == STS_RUN_OK);

    /* Resuming mid-draft gives exactly the run that was never interrupted: a reload
     * shows the same offers, so it cannot reroll them. */
    CHECK(finish_draft(&run, &s) == STS_RUN_OK);
    CHECK(finish_draft(&loaded, &s) == STS_RUN_OK);
    CHECK(runs_equal(&run, &loaded));

    n = sts_run_serialize(&run, text, sizeof text);
    CHECK(n > 0 && sts_run_parse(text, n, &loaded) == STS_RUN_OK && runs_equal(&run, &loaded));
    CHECK(loaded.phase == STS_PHASE_READY && sts_run_validate(&loaded, &g_db, &s) == STS_RUN_OK);

    CHECK(sts_run_begin_reward(&run, &g_db, &s, 1, &out) == STS_RUN_OK && out == STS_REWARD_OFFERS);
    n = sts_run_serialize(&run, text, sizeof text);
    CHECK(n > 0 && sts_run_parse(text, n, &loaded) == STS_RUN_OK && runs_equal(&run, &loaded));
    CHECK(loaded.phase == STS_PHASE_REWARD && sts_run_validate(&loaded, &g_db, &s) == STS_RUN_OK);

    /* The largest deck still fits; a small buffer is reported. */
    other = run;
    other.deck.count = STS_MAX_DECK;
    for (pick = 0; pick < STS_MAX_DECK; pick++)
        other.deck.ids[pick] = 0xFFFF;
    other.offer_count = STS_OFFER_MAX;
    other.matches_played = 1000000000;
    for (pick = 0; pick < STS_OFFER_MAX; pick++) {
        other.offers[pick].id = 0xFFFF;
        other.offers[pick].type = STS_CARD_SPELL;
        other.offers[pick].copies = STS_MAX_DECK;
    }
    i = sts_run_serialize(&other, text, sizeof text);
    CHECK(i > 0 && sts_run_parse(text, i, &loaded) == STS_RUN_OK && runs_equal(&other, &loaded));
    CHECK(sts_run_serialize(&run, text, 10) == 0);
    CHECK(sts_run_serialize(&run, text, 0) == 0);

    /* The output is plain ASCII. */
    n = sts_run_serialize(&run, text, sizeof text);
    bad = 0;
    for (i = 0; i < n; i++)
        if ((unsigned char)text[i] > 127) bad++;
    CHECK(bad == 0);

    /* Flipping any single byte, or cutting the file anywhere, is detected. */
    bad = 0;
    for (i = 0; i < n; i++) {
        memcpy(broken, text, n);
        broken[i] ^= 0x01;
        if (sts_run_parse(broken, n, &loaded) != STS_RUN_DAMAGED) bad++;
    }
    CHECK(bad == 0);
    bad = 0;
    for (i = 0; i < n; i++)
        if (sts_run_parse(text, i, &loaded) != STS_RUN_DAMAGED) bad++;
    CHECK(bad == 0);

    /* An intact file from another format version is identified as such. */
    {
        uint32_t check;
        size_t body = n - 15, w;
        memcpy(broken, text, body);
        broken[8] = '2';
        check = sts_fnv1a(STS_FNV_START, broken, body);
        w = body + (size_t)sprintf(broken + body, "check %08x\n", (unsigned)check);
        CHECK(sts_run_parse(broken, w, &loaded) == STS_RUN_BAD_VERSION);
    }

    /* Validation catches what a checksum cannot: changed rules and illegal contents. */
    {
        sts_settings changed = s;
        n = sts_run_serialize(&run, text, sizeof text);
        CHECK(sts_run_parse(text, n, &loaded) == STS_RUN_OK);
        changed.rules.copy_limit = 3;
        CHECK(sts_run_validate(&loaded, &g_db, &changed) == STS_RUN_SETTINGS_CHANGED);

        other = loaded;
        other.character = 20;
        CHECK(sts_run_validate(&other, &g_db, &s) == STS_RUN_ILLEGAL);
        other = loaded;
        other.deck.ids[0] = 999;      /* not a real card */
        CHECK(sts_run_validate(&other, &g_db, &s) == STS_RUN_ILLEGAL);
        other = loaded;
        other.deck.count = 19;        /* below size_min outside the draft */
        CHECK(sts_run_validate(&other, &g_db, &s) == STS_RUN_ILLEGAL);
        other = loaded;
        other.offer_count = 0;        /* REWARD with nothing to pick */
        CHECK(sts_run_validate(&other, &g_db, &s) == STS_RUN_ILLEGAL);
        other = loaded;
        other.offers[0].id = 999;
        CHECK(sts_run_validate(&other, &g_db, &s) == STS_RUN_ILLEGAL);
        other = loaded;
        other.offers[0].type = (uint8_t)(other.offers[0].type == STS_CARD_SPELL ? STS_CARD_SKILL : STS_CARD_SPELL);
        CHECK(sts_run_validate(&other, &g_db, &s) == STS_RUN_ILLEGAL);
        other = loaded;
        other.offers[0].copies = 0;
        CHECK(sts_run_validate(&other, &g_db, &s) == STS_RUN_ILLEGAL);
        other = loaded;
        other.picks_done = 21;
        CHECK(sts_run_validate(&other, &g_db, &s) == STS_RUN_ILLEGAL);
        other = loaded;
        other.phase = STS_PHASE_READY;  /* READY must not carry offers */
        CHECK(sts_run_validate(&other, &g_db, &s) == STS_RUN_ILLEGAL);
    }

    /* Files: no save yet, then a normal save and overwrite, leaving no temp file. */
    CHECK(sts_run_load(path, &loaded) == STS_RUN_IO);
    CHECK(sts_run_save(path, &run) == STS_RUN_OK);
    CHECK(sts_run_load(path, &loaded) == STS_RUN_OK && runs_equal(&run, &loaded));
    other = run;
    other.matches_played = run.matches_played + 1;
    CHECK(sts_run_save(path, &other) == STS_RUN_OK);
    CHECK(sts_run_load(path, &loaded) == STS_RUN_OK && runs_equal(&other, &loaded));
    {
        FILE *f = fopen(tmp, "rb");
        CHECK(f == NULL);
        if (f) fclose(f);
    }

    /* A crash during the temp write: the partial file is ignored, the old save loads. */
    n = sts_run_serialize(&run, text, sizeof text);
    CHECK(write_file(tmp, text, n / 2));
    CHECK(sts_run_load(path, &loaded) == STS_RUN_OK && runs_equal(&other, &loaded));

    /* A crash just before the rename: the complete temp file is the newer one. */
    CHECK(write_file(tmp, text, n));
    CHECK(sts_run_load(path, &loaded) == STS_RUN_OK && runs_equal(&run, &loaded));

    /* Even with the main file gone, as after a remove with the rename not done. */
    remove(path);
    CHECK(sts_run_load(path, &loaded) == STS_RUN_OK && runs_equal(&run, &loaded));

    /* A later save clears the leftover and wins. */
    CHECK(sts_run_save(path, &other) == STS_RUN_OK);
    CHECK(sts_run_load(path, &loaded) == STS_RUN_OK && runs_equal(&other, &loaded));

    /* A damaged main file with no temp file is reported, not silently replaced. */
    remove(tmp);
    CHECK(write_file(path, text, n / 2));
    CHECK(sts_run_load(path, &loaded) == STS_RUN_DAMAGED);

    remove(path);
    remove(tmp);
    CHECK(strcmp(sts_run_status_str(STS_RUN_DAMAGED), "save file is damaged") == 0);
}

/* A whole run, saved and reloaded after every step, stays legal in every mode. */
void test_run_endurance(void)
{
    static const char *const inis[] = {
        "",
        "[reward]\nreplacement=by_type\n",
        "[reward]\nreplacement=random\ncopies_max=4\n",
        "[reward]\nwho_picks=nobody\ncopies_max=4\n",
        "[reward]\nwho_picks=loser\nreplacement=random\n",
        "[deck]\nsize_min=15\nsize_max=30\n[reward]\nmode=add\ncopies_max=4\n",
        "[deck]\nsize_min=15\nsize_max=30\n[reward]\nmode=remove\ncopies_max=3\n",
        "[reward]\nreplacement=random\ntypes=spell\ncopies_min=2\ncopies_max=2\n",
    };
    static sts_run run;
    static char text[STS_RUN_TEXT_MAX];
    sts_settings s;
    sts_rng dice;
    size_t i;
    int c, m, line = 0, violations = 0, stuck = 0;

    sts_rng_seed(&dice, 77, 77);
    for (i = 0; i < sizeof inis / sizeof inis[0]; i++) {
        CHECK(parse_ini(inis[i], &s, &line) == STS_SETTINGS_OK);
        CHECK(sts_settings_validate(&s) == NULL);
        for (c = 0; c < STS_NUM_CHARACTERS; c++) {
            size_t n;
            if (sts_run_start(&run, &g_db, &s, c, (uint64_t)(i * 100 + (size_t)c)) != STS_RUN_OK) stuck++;
            if (finish_draft(&run, &s) != STS_RUN_OK) stuck++;
            for (m = 0; m < 60; m++) {
                sts_reward_outcome out;
                sts_replace_choice ch;
                uint16_t remove_ids[4];
                int k, d;

                if (sts_run_begin_reward(&run, &g_db, &s, m & 1, &out) != STS_RUN_OK)
                    violations++;

                /* Quit and reload with offers on screen. */
                n = sts_run_serialize(&run, text, sizeof text);
                if (!n || sts_run_parse(text, n, &run) != STS_RUN_OK
                    || sts_run_validate(&run, &g_db, &s) != STS_RUN_OK)
                    violations++;

                if (out == STS_REWARD_OFFERS) {
                    int idx = (int)sts_rng_below(&dice, (uint32_t)run.offer_count);
                    memset(&ch, 0, sizeof ch);
                    if (s.reward.replace == STS_REPLACE_CHOOSE) {
                        ch.remove_ids = remove_ids;
                        ch.remove_count = run.offers[idx].copies;
                        for (k = 0, d = 0; k < ch.remove_count; d++)
                            if (run.deck.ids[d] != run.offers[idx].id)
                                remove_ids[k++] = run.deck.ids[d];
                    } else if (s.reward.replace == STS_REPLACE_BY_TYPE) {
                        unsigned mask = sts_replace_types_available(&g_db, c, &run.deck, run.offers[idx].id, run.offers[idx].copies);
                        ch.type = (mask & 1u) ? 0 : (mask & 2u) ? 1 : 2;
                    }
                    if (sts_run_reward_pick(&run, &g_db, &s, idx, &ch) != STS_RUN_OK)
                        violations++;
                }
                if (sts_deck_validate(&run.deck, &s.rules, NULL) != STS_DECK_OK
                    || !sts_deck_cards_legal(&g_db, c, &run.deck, NULL))
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
    sts_settings s, changed;
    sts_reward_outcome out;
    int i, line = 0;

    sts_settings_defaults(&s);
    CHECK(s.allow_rule_changes == 0);

    /* The setting parses, and how a rule change is treated is not itself a rule. */
    CHECK(parse_ini("[run]\nallow_rule_changes=true\n", &changed, &line) == STS_SETTINGS_OK);
    CHECK(changed.allow_rule_changes == 1);
    CHECK(sts_settings_hash(&changed) == sts_settings_hash(&s));
    CHECK(parse_ini("[run]\nallow_rule_changes=1\n", &changed, &line) == STS_SETTINGS_BAD_VALUE);
    CHECK(parse_ini("[run]\nnope=true\n", &changed, &line) == STS_SETTINGS_UNKNOWN_KEY);

    /* A ready run carries on under compatible rules and then validates. */
    ready_run(&run, &s, 0, 51);
    before = run;
    changed = s;
    changed.reward.replace = STS_REPLACE_RANDOM;
    CHECK(sts_run_validate(&run, &g_db, &changed) == STS_RUN_SETTINGS_CHANGED);
    CHECK(sts_run_adopt_rules(&run, &g_db, &changed) == STS_RUN_OK);
    CHECK(sts_run_validate(&run, &g_db, &changed) == STS_RUN_OK);
    CHECK(decks_equal(&run.deck, &before.deck) && run.phase == STS_PHASE_READY);

    /* A deck that breaks the new rules is refused, and nothing changes. */
    run = before;
    changed = s;
    changed.rules.deck_min = 25;
    changed.rules.deck_max = 25;
    changed.draft_picks = 25;
    CHECK(sts_run_adopt_rules(&run, &g_db, &changed) == STS_RUN_ILLEGAL);
    CHECK(run.settings_hash == before.settings_hash);

    /* Offers that are still possible stay, whatever else changed: no free reroll. */
    run = before;
    CHECK(sts_run_begin_reward(&run, &g_db, &s, 1, &out) == STS_RUN_OK && out == STS_REWARD_OFFERS);
    before = run;
    changed = s;
    changed.reward.replace = STS_REPLACE_RANDOM;
    CHECK(sts_run_adopt_rules(&run, &g_db, &changed) == STS_RUN_OK);
    CHECK(run.phase == STS_PHASE_REWARD && run.offer_count == before.offer_count);
    for (i = 0; i < run.offer_count; i++)
        CHECK(run.offers[i].id == before.offers[i].id && run.offers[i].copies == before.offers[i].copies);
    CHECK(run.rng.state == before.rng.state);

    /* Offers that are no longer possible are rolled again under the new rules. */
    run = before;
    changed = s;
    changed.reward.copies_min = 2;
    changed.reward.copies_max = 2;
    CHECK(sts_run_adopt_rules(&run, &g_db, &changed) == STS_RUN_OK);
    CHECK(run.phase == STS_PHASE_REWARD && run.offer_count == 3);
    for (i = 0; i < run.offer_count; i++)
        CHECK(run.offers[i].copies == 2);

    /* A type no longer allowed also rolls again. */
    run = before;
    changed = s;
    changed.reward.type_mask = STS_TYPE_BIT(STS_CARD_SPELL);
    CHECK(sts_run_adopt_rules(&run, &g_db, &changed) == STS_RUN_OK);
    for (i = 0; i < run.offer_count; i++)
        CHECK(run.offers[i].type == STS_CARD_SPELL);

    /* A waiting reward under rules with no rewards at all simply ends. */
    run = before;
    changed = s;
    changed.reward.mode = STS_REWARD_SKIP;
    CHECK(sts_run_adopt_rules(&run, &g_db, &changed) == STS_RUN_OK);
    CHECK(run.phase == STS_PHASE_READY && run.offer_count == 0);
    CHECK(sts_run_validate(&run, &g_db, &changed) == STS_RUN_OK);

    /* A draft in progress follows the new draft rules. */
    CHECK(sts_run_start(&run, &g_db, &s, 3, 61) == STS_RUN_OK);
    for (i = 0; i < 5; i++)
        CHECK(sts_run_draft_pick(&run, &g_db, &s, 0) == STS_RUN_OK);
    changed = s;
    changed.draft.offer_count = 3;
    changed.draft.type_mask = STS_TYPE_BIT(STS_CARD_SKILL);
    CHECK(sts_run_adopt_rules(&run, &g_db, &changed) == STS_RUN_OK);
    CHECK(run.phase == STS_PHASE_DRAFT && run.picks_done == 5);
    for (i = 0; i < run.offer_count; i++)
        CHECK(run.offers[i].type == STS_CARD_SKILL);
    CHECK(finish_draft(&run, &changed) == STS_RUN_OK);
    CHECK(sts_deck_validate(&run.deck, &changed.rules, NULL) == STS_DECK_OK);
}
