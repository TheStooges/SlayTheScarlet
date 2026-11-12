/* Tests: Settings parsing, validation and fingerprint. */
#include "test_util.h"

static char g_text[512];

/* Adds copies of a card to a starter list. */
static void add_to_list(sts_starter_list *list, uint16_t id, int copies)
{
    while (copies-- > 0 && list->count < STS_STARTER_MAX)
        list->ids[list->count++] = id;
}

/* Fills a starter list up to count cards, 4 copies of each new card, so no card goes over the copy
 * limit. */
static void fill_list(sts_starter_list *list, int count)
{
    int wau;

    for (wau = list->count; wau < count && wau < STS_STARTER_MAX; wau++)
        list->ids[wau] = (uint16_t)(1 + wau / 4);
    list->count = wau;
}

static void test_settings_defaults(void)
{
    sts_settings settings, defaults;
    int line = 0;

    /* The defaults are legal, and the shipped file states exactly the defaults. */
    sts_settings_defaults(&defaults);
    CHECK(sts_settings_validate(&defaults) == NULL);
    CHECK(defaults.rules.deck_min == 20 && defaults.rules.deck_max == 20 && defaults.rules.copy_limit == 4);
    CHECK(defaults.draft.offer_count == 3 && defaults.draft.mode == STS_REWARD_ADD);
    CHECK(defaults.draft.copies_min == 1 && defaults.draft.copies_max == 1);
    CHECK(sts_settings_draft_length(&defaults, 0) == 20);
    CHECK(defaults.pick_visibility == STS_VISIBILITY_AFTER_CONFIRM && defaults.items_enabled == 0);
    CHECK(sts_settings_load(STS_SETTINGS_PATH, &settings, &line) == STS_SETTINGS_OK);
    CHECK(sts_settings_hash(&settings) == sts_settings_hash(&defaults));

    /* An empty file gives the defaults; a file only needs what it changes. */
    CHECK(parse_ini("", &settings, &line) == STS_SETTINGS_OK);
    CHECK(sts_settings_hash(&settings) == sts_settings_hash(&defaults));
    CHECK(parse_ini("# c\n; c\n\n  [deck]  \r\n  size_max = 30  \r\n", &settings, &line) == STS_SETTINGS_OK);
    CHECK(settings.rules.deck_max == 30 && settings.rules.deck_min == 20);
}

static void test_settings_every_key(void)
{
    sts_settings settings;
    int line = 0;

    /* Every setting is reachable. */
    CHECK(parse_ini("[deck]\nsize_min=15\nsize_max=25\ncopy_limit=3\n"
                    "[draft]\noffers=4\ncard_types=mostly_spell\n"
                    "[reward]\nmode=add\nwho_picks=loser\noffers=5\ncopies_min=2\ncopies_max=3\n"
                    "card_types=one_of_each\nreplacement=by_type\n"
                    "[netplay]\npick_visibility=live\nopponent_deck=full\n"
                    "[items]\nenabled=true\n", &settings, &line) == STS_SETTINGS_OK);
    CHECK(settings.rules.deck_min == 15 && settings.rules.deck_max == 25 && settings.rules.copy_limit == 3);
    CHECK(settings.draft.offer_count == 4);
    CHECK(sts_settings_validate(&settings) == NULL);

    /* The draft's picks and copies are fixed now. Files from 0.2 and before list them, and they are
     * read and ignored; they are not settings any more. */
    CHECK(parse_ini("[draft]\npicks=15\ncopies_min=2\ncopies_max=3\n", &settings, &line) == STS_SETTINGS_OK);
    {
        sts_settings defaults;

        sts_settings_defaults(&defaults);
        CHECK(sts_settings_equal(&settings, &defaults) && sts_settings_hash(&settings) == sts_settings_hash(&defaults));
        CHECK(sts_settings_set(&settings, "draft", "picks", "15") == STS_SETTINGS_UNKNOWN_KEY);
        CHECK(sts_settings_set(&settings, "draft", "copies_max", "2") == STS_SETTINGS_UNKNOWN_KEY);
        CHECK(sts_settings_set(&settings, "reward", "picks", "15") == STS_SETTINGS_UNKNOWN_KEY);
    }
    CHECK(parse_ini("[reward]\npicks=15\n", &settings, &line) == STS_SETTINGS_UNKNOWN_KEY);
    CHECK(parse_ini("[draft]\nsize_min=15\ncopies_min=2\ncopies_max=3\n", &settings, &line) == STS_SETTINGS_UNKNOWN_KEY);

    /* Every other setting is reachable. */
    CHECK(parse_ini("[deck]\nsize_min=15\nsize_max=25\ncopy_limit=3\n"
                    "[draft]\noffers=4\ncard_types=mostly_spell\n"
                    "[reward]\nmode=add\nwho_picks=loser\noffers=5\ncopies_min=2\ncopies_max=3\n"
                    "card_types=one_of_each\nreplacement=by_type\n"
                    "[netplay]\npick_visibility=live\nopponent_deck=full\n"
                    "[items]\nenabled=true\n", &settings, &line) == STS_SETTINGS_OK);
    CHECK(settings.draft.style == STS_OFFER_WEIGHTED && settings.draft.weights[STS_CARD_SPELL] == 4
          && settings.draft.weights[STS_CARD_SYSTEM] == 1 && settings.draft.type_mask == STS_TYPE_ALL);
    CHECK(settings.reward.mode == STS_REWARD_ADD && settings.who_picks == STS_WHO_LOSER
          && settings.reward.offer_count == 5);
    CHECK(settings.reward.copies_min == 2 && settings.reward.copies_max == 3);
    CHECK(settings.reward.style == STS_OFFER_ONE_OF_EACH && settings.reward.replace == STS_REPLACE_BY_TYPE);
    CHECK(settings.pick_visibility == STS_VISIBILITY_LIVE && settings.opponent_deck == STS_OPPONENT_DECK_FULL);
    CHECK(settings.items_enabled == 1);
    CHECK(sts_settings_validate(&settings) == NULL);
}

static void test_settings_long_lines(void)
{
    sts_settings settings;
    int line = 0;

    /* A long comment is fine; a long setting is refused rather than truncated. */
    memset(g_text, 'x', 300);
    g_text[0] = ' ';
    g_text[1] = '#';
    g_text[300] = '\n';
    g_text[301] = 0;
    CHECK(parse_ini(g_text, &settings, &line) == STS_SETTINGS_OK);
    g_text[1] = 'x';
    CHECK(parse_ini(g_text, &settings, &line) == STS_SETTINGS_BAD_LINE && line == 1);
}

static void test_settings_offer_style(void)
{
    sts_settings settings, defaults;
    int line = 0;

    sts_settings_defaults(&defaults);

    /* card_types, separately for the draft and rewards. Weighted is the default: each type 1. */
    CHECK(defaults.draft.style == STS_OFFER_WEIGHTED && defaults.reward.style == STS_OFFER_WEIGHTED);
    CHECK(defaults.reward.weights[STS_CARD_SYSTEM] == 1 && defaults.reward.weights[STS_CARD_SKILL] == 1
          && defaults.reward.weights[STS_CARD_SPELL] == 1 && defaults.reward.type_mask == STS_TYPE_ALL);
    CHECK(parse_ini("[draft]\ncard_types=one_of_each\n[reward]\ncard_types=mostly_skill\n",
                    &settings, &line) == STS_SETTINGS_OK);
    CHECK(settings.draft.style == STS_OFFER_ONE_OF_EACH && settings.reward.style == STS_OFFER_WEIGHTED);
    CHECK(settings.reward.weights[STS_CARD_SYSTEM] == 1 && settings.reward.weights[STS_CARD_SKILL] == 4
          && settings.reward.weights[STS_CARD_SPELL] == 1);
    CHECK(sts_settings_validate(&settings) == NULL);
    CHECK(parse_ini("[draft]\ncard_types=random\n", &settings, &line) == STS_SETTINGS_OK
          && settings.draft.style == STS_OFFER_BY_CARD);
    CHECK(parse_ini("[reward]\ncard_types=mostly_system\n", &settings, &line) == STS_SETTINGS_OK
          && settings.reward.weights[STS_CARD_SYSTEM] == 4 && settings.reward.weights[STS_CARD_SPELL] == 1);
    CHECK(parse_ini("[reward]\ncard_types=gamble\n", &settings, &line) == STS_SETTINGS_BAD_VALUE);
    CHECK(parse_ini("[reward]\ncard_types=\n", &settings, &line) == STS_SETTINGS_BAD_VALUE);

    /* Every value is a different rule. */
    {
        static const char *const values[] = {"random", "weighted", "mostly_system", "mostly_skill", "mostly_spell",
                                             "one_of_each"};
        uint32_t hashes[6];
        int wau, j;

        for (wau = 0; wau < 6; wau++) {
            sts_settings_defaults(&settings);
            CHECK(sts_settings_set(&settings, "reward", "card_types", values[wau]) == STS_SETTINGS_OK);
            hashes[wau] = sts_settings_hash(&settings);
            for (j = 0; j < wau; j++)
                CHECK(hashes[j] != hashes[wau]);
        }
        CHECK(hashes[1] == sts_settings_hash(&defaults));
    }

    /* The type settings it replaced are read and ignored, so files from 0.3 still load. */
    CHECK(parse_ini("[draft]\ntypes=spell\noffer_style=one_of_each\nsystem_weight=0\n"
                    "[reward]\ntypes=system,skill\nskill_weight=9\nspell_weight=0\n", &settings, &line) == STS_SETTINGS_OK);
    CHECK(sts_settings_equal(&settings, &defaults) && sts_settings_hash(&settings) == sts_settings_hash(&defaults));
    CHECK(sts_settings_set(&settings, "draft", "types", "spell") == STS_SETTINGS_UNKNOWN_KEY);
}

static void test_settings_errors(void)
{
    sts_settings settings, defaults;
    int line = 0;

    sts_settings_defaults(&defaults);

    /* Damage is reported with the right kind and line. */
    CHECK(parse_ini("[deck]\nnonsense\n", &settings, &line) == STS_SETTINGS_BAD_LINE && line == 2);
    CHECK(parse_ini("[deck\n", &settings, &line) == STS_SETTINGS_BAD_LINE && line == 1);
    CHECK(parse_ini("[nope]\n", &settings, &line) == STS_SETTINGS_UNKNOWN_SECTION);
    CHECK(parse_ini("size_min=1\n", &settings, &line) == STS_SETTINGS_UNKNOWN_KEY && line == 1);
    CHECK(parse_ini("[deck]\n\n\nsizemin=1\n", &settings, &line) == STS_SETTINGS_UNKNOWN_KEY && line == 4);
    CHECK(parse_ini("[draft]\nmode=add\n", &settings, &line) == STS_SETTINGS_UNKNOWN_KEY);
    CHECK(parse_ini("[deck]\nsize_min=12x\n", &settings, &line) == STS_SETTINGS_BAD_VALUE);
    CHECK(parse_ini("[deck]\nsize_min=\n", &settings, &line) == STS_SETTINGS_BAD_VALUE);
    CHECK(parse_ini("[deck]\nsize_min=-1\n", &settings, &line) == STS_SETTINGS_BAD_VALUE);
    CHECK(parse_ini("[deck]\nsize_min=99999\n", &settings, &line) == STS_SETTINGS_BAD_VALUE);
    CHECK(parse_ini("[reward]\nmode=gamble\n", &settings, &line) == STS_SETTINGS_BAD_VALUE);
    CHECK(parse_ini("[reward]\ncard_types=trap\n", &settings, &line) == STS_SETTINGS_BAD_VALUE);
    CHECK(parse_ini("[items]\nenabled=1\n", &settings, &line) == STS_SETTINGS_BAD_VALUE);
    CHECK(sts_settings_load("no/such/file.ini", &settings, &line) == STS_SETTINGS_IO);
    CHECK(sts_settings_hash(&settings) == sts_settings_hash(&defaults));    /* defaults even so */
    CHECK(strcmp(sts_settings_status_str(STS_SETTINGS_BAD_VALUE), "bad value") == 0);
}

static void test_settings_validation(void)
{
    sts_settings settings;
    int line = 0;

    /* Combinations that could never work are refused. */
#define BAD_INI(text) do { \
        CHECK(parse_ini(text, &settings, &line) == STS_SETTINGS_OK); \
        CHECK(sts_settings_validate(&settings) != NULL); \
    } while (0)
    BAD_INI("[deck]\nsize_min=30\nsize_max=20\n");
    BAD_INI("[deck]\nsize_min=0\n");
    BAD_INI("[deck]\nsize_max=300\n[draft]\npicks=300\n");
    BAD_INI("[deck]\ncopy_limit=5\n");
    BAD_INI("[deck]\ncopy_limit=0\n");
    BAD_INI("[draft]\noffers=0\n");
    BAD_INI("[draft]\noffers=9\n");
    BAD_INI("[reward]\nmode=add\n");
    BAD_INI("[reward]\nmode=remove\n");
    BAD_INI("[reward]\noffers=0\n");
    BAD_INI("[reward]\ncopies_min=3\ncopies_max=2\n");
    BAD_INI("[reward]\ncopies_max=5\n");
#undef BAD_INI

    /* The problem is named, for the settings screen to word it. */
    {
        sts_settings_problem_info info;
#define PROBLEM(text, expected) do { \
        CHECK(parse_ini(text, &settings, &line) == STS_SETTINGS_OK); \
        CHECK(sts_settings_check(&settings, &info) == (expected)); \
        CHECK(sts_settings_validate(&settings) != NULL); \
    } while (0)
        PROBLEM("[deck]\nsize_min=30\nsize_max=20\n", STS_PROBLEM_DECK_SIZE);
        PROBLEM("[deck]\ncopy_limit=5\n", STS_PROBLEM_COPY_LIMIT);
        PROBLEM("[draft]\noffers=9\n", STS_PROBLEM_DRAFT_OFFERS);
        PROBLEM("[reward]\ncopies_min=3\ncopies_max=2\n", STS_PROBLEM_REWARD_COPIES);
        PROBLEM("[reward]\ncopies_max=5\n", STS_PROBLEM_REWARD_COPY_LIMIT);
        PROBLEM("[reward]\nmode=add\n", STS_PROBLEM_REWARD_SIZE);
#undef PROBLEM
        /* Starter problems can only come from the built-in lists, so they are made by hand. */
        sts_settings_defaults(&settings);
        settings.starter_enabled = 1;
        add_to_list(&settings.starter_common, 0, 3);   /* 5 Talismans */
        CHECK(sts_settings_check(&settings, &info) == STS_PROBLEM_STARTER_COPIES);
        CHECK(info.number == 0 && info.character == 0);
        sts_settings_defaults(&settings);
        settings.starter_enabled = 1;
        settings.rules.deck_max = 21;
        add_to_list(&settings.starter[0], 1, 14);   /* 22 cards in all */
        CHECK(sts_settings_check(&settings, &info) == STS_PROBLEM_STARTER_TOO_BIG && info.character == 0);
        sts_settings_defaults(&settings);
        CHECK(sts_settings_check(&settings, NULL) == STS_PROBLEM_NONE);
    }

    /* Workable variations are accepted, including a skip with otherwise odd offers. */
    CHECK(parse_ini("[reward]\ncopies_min=1\ncopies_max=4\n", &settings, &line) == STS_SETTINGS_OK);
    CHECK(sts_settings_validate(&settings) == NULL);
    CHECK(parse_ini("[reward]\nmode=skip\noffers=0\n", &settings, &line) == STS_SETTINGS_OK);
    CHECK(sts_settings_validate(&settings) == NULL);
    CHECK(parse_ini("[deck]\nsize_min=15\nsize_max=25\n[reward]\nmode=remove\n", &settings, &line) == STS_SETTINGS_OK);
    CHECK(sts_settings_validate(&settings) == NULL);
}

static void test_settings_fingerprint(void)
{
    sts_settings settings, defaults;
    uint32_t base;

    sts_settings_defaults(&defaults);
    base = sts_settings_hash(&defaults);

    /* The fingerprint notices a change to any rule. */
#define HASH_DIFFERS(field, value) do { \
        settings = defaults; settings.field = value; \
        CHECK(sts_settings_hash(&settings) != base); \
    } while (0)
    HASH_DIFFERS(rules.deck_min, 19);
    HASH_DIFFERS(rules.deck_max, 21);
    HASH_DIFFERS(rules.copy_limit, 3);
    HASH_DIFFERS(draft.offer_count, 4);
    HASH_DIFFERS(draft.copies_max, 2);
    HASH_DIFFERS(draft.type_mask, 1u);
    HASH_DIFFERS(reward.mode, STS_REWARD_SKIP);
    HASH_DIFFERS(reward.offer_count, 4);
    HASH_DIFFERS(reward.copies_min, 2);
    HASH_DIFFERS(reward.copies_max, 2);
    HASH_DIFFERS(reward.type_mask, 1u);
    HASH_DIFFERS(reward.replace, STS_REPLACE_RANDOM);
    HASH_DIFFERS(reward.style, STS_OFFER_ONE_OF_EACH);
    HASH_DIFFERS(reward.weights[STS_CARD_SYSTEM], 2);
    HASH_DIFFERS(reward.weights[STS_CARD_SKILL], 2);
    HASH_DIFFERS(reward.weights[STS_CARD_SPELL], 2);
    HASH_DIFFERS(draft.style, STS_OFFER_ONE_OF_EACH);
    HASH_DIFFERS(draft.weights[STS_CARD_SPELL], 2);
    HASH_DIFFERS(who_picks, STS_WHO_WINNER);
    HASH_DIFFERS(pick_visibility, STS_VISIBILITY_LIVE);
    HASH_DIFFERS(opponent_deck, STS_OPPONENT_DECK_FULL);
    HASH_DIFFERS(items_enabled, 1);
#undef HASH_DIFFERS
}

/* Compatible mode always ends up legal, 20 cards, 4 copies, no items. */
static void test_settings_compatible(void)
{
    sts_settings settings, defaults;
    int line = 0;
    static const char *const inis[] = {
        "",
        "[deck]\nsize_min=15\nsize_max=30\n[reward]\nmode=add\n",
        "[deck]\nsize_min=10\nsize_max=10\n",
        "[deck]\nsize_max=40\n[reward]\ncopies_min=2\ncopies_max=2\n",
        "[deck]\ncopy_limit=2\n[reward]\nmode=remove\ncopies_max=2\n[items]\nenabled=true\n",
        "[deck]\nsize_min=15\nsize_max=30\n[reward]\nmode=skip\n[netplay]\npick_visibility=live\n",
    };
    size_t wau;

    sts_settings_defaults(&defaults);
    for (wau = 0; wau < sizeof inis / sizeof inis[0]; wau++) {
        CHECK(parse_ini(inis[wau], &settings, &line) == STS_SETTINGS_OK);
        sts_settings_force_compatible(&settings);
        CHECK(sts_settings_validate(&settings) == NULL);
        CHECK(settings.rules.deck_min == 20 && settings.rules.deck_max == 20 && settings.rules.copy_limit == 4);
        CHECK(settings.items_enabled == 0);
        CHECK(settings.reward.mode == STS_REWARD_REPLACE || settings.reward.mode == STS_REWARD_SKIP);
    }
    CHECK(parse_ini("[reward]\nwho_picks=nobody\n", &settings, &line) == STS_SETTINGS_OK);
    sts_settings_force_compatible(&settings);
    CHECK(settings.who_picks == STS_WHO_BOTH);
    /* An old file's nobody reads as both, and random needs nothing from the other player. */
    CHECK(parse_ini("[reward]\nwho_picks=random\n", &settings, &line) == STS_SETTINGS_OK);
    sts_settings_force_compatible(&settings);
    CHECK(settings.who_picks == STS_WHO_RANDOM);

    /* Rules that are already compatible stay exactly as they were, so a run saved under the
     * defaults is not treated as made under different rules. */
    sts_settings_defaults(&settings);
    sts_settings_force_compatible(&settings);
    CHECK(sts_settings_hash(&settings) == sts_settings_hash(&defaults));

    /* It only changes what it must: other reward and draft choices survive. */
    CHECK(parse_ini("[deck]\nsize_min=15\nsize_max=30\n[draft]\noffers=5\n"
                    "[reward]\nmode=add\noffers=4\nwho_picks=winner\n", &settings, &line) == STS_SETTINGS_OK);
    sts_settings_force_compatible(&settings);
    CHECK(settings.reward.mode == STS_REWARD_REPLACE && settings.reward.offer_count == 4);
    CHECK(settings.draft.offer_count == 5 && settings.who_picks == STS_WHO_WINNER);
}

/* Showing the deck during a draft is on by default, and changing it does not end a run. */
static void test_settings_show_deck(void)
{
    sts_settings settings, defaults;
    int line = 0;

    sts_settings_defaults(&defaults);
    CHECK(defaults.draft_show_deck == 1);
    CHECK(parse_ini("[draft]\nshow_deck=false\n", &settings, &line) == STS_SETTINGS_OK);
    CHECK(settings.draft_show_deck == 0);
    CHECK(sts_settings_validate(&settings) == NULL);
    CHECK(sts_settings_hash(&settings) == sts_settings_hash(&defaults));
    CHECK(parse_ini("[draft]\nshow_deck=1\n", &settings, &line) == STS_SETTINGS_BAD_VALUE);
    CHECK(parse_ini("[reward]\nshow_deck=true\n", &settings, &line) == STS_SETTINGS_UNKNOWN_KEY);
    sts_settings_force_compatible(&defaults);
    CHECK(defaults.draft_show_deck == 1);
}

/* The starter: off by default, with the lists left out of the fingerprint until it is on. */
static void test_settings_starter(void)
{
    sts_settings settings, defaults;
    sts_deck starter;
    int character, line = 0;

    sts_settings_defaults(&defaults);
    CHECK(defaults.starter_enabled == 0 && defaults.starter_common.count == 4);
    for (character = 0; character < STS_NUM_CHARACTERS; character++)
        CHECK(defaults.starter[character].count == 4);
    CHECK(defaults.starter[1].ids[0] == 102 && defaults.starter[1].ids[2] == 212);
    CHECK(defaults.starter[18].ids[1] == 102 && defaults.starter[18].ids[3] == 208);
    sts_settings_starter(&defaults, 0, &starter);
    CHECK(starter.count == 0);

    /* The lists are built in, and the fingerprint only sees them when the starter is on. */
    CHECK(sts_settings_load(STS_SETTINGS_PATH, &settings, &line) == STS_SETTINGS_OK);
    settings.starter_enabled = 1;
    defaults.starter_enabled = 1;
    CHECK(sts_settings_hash(&settings) == sts_settings_hash(&defaults));
    defaults.starter_enabled = 0;
    CHECK(sts_settings_draft_length(&defaults, 0) == 20);

    /* Lists change nothing while the starter is off, so runs made before it keep their rules. */
    settings = defaults;
    settings.starter[0].count = 0;
    settings.starter_common.count = 1;
    CHECK(sts_settings_hash(&settings) == sts_settings_hash(&defaults));

    /* Files from 0.2 list the cards. They are read and ignored: the lists are not settings now. */
    CHECK(parse_ini("[starter]\nreimu=\ncommon=1\nSuwako=201\nmarisa=abc,\n", &settings, &line) == STS_SETTINGS_OK);
    CHECK(sts_settings_equal(&settings, &defaults) && settings.starter[0].count == 4);

    /* On, it is part of the rules, and so is every list. Patchouli's default skill on 214 is 103,
     * and A Penny Saved (12) is not in it. */
    CHECK(parse_ini("[starter]\nenabled=true\n", &settings, &line) == STS_SETTINGS_OK);
    CHECK(sts_settings_validate(&settings) == NULL);
    CHECK(sts_settings_hash(&settings) != sts_settings_hash(&defaults));
    sts_settings_starter(&settings, 4, &starter);
    CHECK(starter.count == 8 && sts_deck_count_of(&starter, 0) == 2 && sts_deck_count_of(&starter, 12) == 0
          && sts_deck_count_of(&starter, 13) == 2 && sts_deck_count_of(&starter, 103) == 2
          && sts_deck_count_of(&starter, 212) == 2);
    CHECK(sts_settings_draft_length(&settings, 4) == 12);
    {
        uint32_t enabled_hash = sts_settings_hash(&settings);
        settings.starter[19].count = 1;
        CHECK(sts_settings_hash(&settings) != enabled_hash);
    }

    /* Only character names and common are old list keys; anything else is still unknown. */
    CHECK(parse_ini("[starter]\nrumia=200\n", &settings, &line) == STS_SETTINGS_UNKNOWN_KEY);
    CHECK(parse_ini("[starter]\nreimux=200\n", &settings, &line) == STS_SETTINGS_UNKNOWN_KEY);

    /* The starter must leave a legal deck in reach. */
    sts_settings_defaults(&settings);
    settings.starter_enabled = 1;
    add_to_list(&settings.starter_common, 0, 3);
    CHECK(sts_settings_validate(&settings) != NULL);
    sts_settings_defaults(&settings);
    settings.starter_enabled = 1;
    fill_list(&settings.starter_common, 20);   /* 24 cards with Reimu's own */
    CHECK(sts_settings_validate(&settings) != NULL);
    CHECK(parse_ini("[starter]\nenabled=true\n", &settings, &line) == STS_SETTINGS_OK);
    CHECK(sts_settings_validate(&settings) == NULL);
    CHECK(sts_settings_draft_length(&settings, 0) == 12);
    settings.starter[0].count = 0;
    CHECK(sts_settings_draft_length(&settings, 0) == 16 && sts_settings_draft_length(&settings, 1) == 12);

    /* Compatible mode keeps a starter that fits, and turns off one that cannot. */
    CHECK(parse_ini("[starter]\nenabled=true\n", &settings, &line) == STS_SETTINGS_OK);
    sts_settings_force_compatible(&settings);
    CHECK(settings.starter_enabled == 1);
    CHECK(parse_ini("[deck]\nsize_min=30\nsize_max=30\n[starter]\nenabled=true\n", &settings, &line) == STS_SETTINGS_OK);
    fill_list(&settings.starter_common, 22);
    CHECK(sts_settings_validate(&settings) == NULL);
    sts_settings_force_compatible(&settings);
    CHECK(settings.starter_enabled == 0 && sts_settings_validate(&settings) == NULL);
}

/* Reading, setting and stepping one setting, as the settings screen does. */
static void test_settings_one_value(void)
{
    sts_settings settings, before;
    char value[256];
    int wau;

    sts_settings_defaults(&settings);
    CHECK(sts_settings_get(&settings, "draft", "offers", value, sizeof value) && strcmp(value, "3") == 0);
    CHECK(!sts_settings_get(&settings, "draft", "picks", value, sizeof value));
    CHECK(sts_settings_get(&settings, "reward", "copies_max", value, sizeof value) && strcmp(value, "1") == 0);
    CHECK(sts_settings_get(&settings, "draft", "show_deck", value, sizeof value) && strcmp(value, "true") == 0);
    CHECK(sts_settings_get(&settings, "draft", "card_types", value, sizeof value) && strcmp(value, "weighted") == 0);
    CHECK(sts_settings_get(&settings, "reward", "who_picks", value, sizeof value) && strcmp(value, "both") == 0);
    CHECK(sts_settings_get(&settings, "reward", "replacement", value, sizeof value) && strcmp(value, "choose") == 0);
    CHECK(sts_settings_get(&settings, "netplay", "pick_visibility", value, sizeof value)
          && strcmp(value, "after_confirm") == 0);
    CHECK(sts_settings_get(&settings, "starter", "enabled", value, sizeof value) && strcmp(value, "false") == 0);
    CHECK(!sts_settings_get(&settings, "starter", "common", value, sizeof value));
    CHECK(!sts_settings_get(&settings, "starter", "reimu", value, sizeof value));
    CHECK(!sts_settings_get(&settings, "draft", "nonsense", value, sizeof value) && value[0] == 0);
    CHECK(!sts_settings_get(&settings, "nonsense", "picks", value, sizeof value));

    /* Set takes the file's text; a bad value or unknown key changes nothing. */
    CHECK(sts_settings_set(&settings, "reward", "who_picks", "loser") == STS_SETTINGS_OK
          && settings.who_picks == STS_WHO_LOSER);
    CHECK(sts_settings_set(&settings, "draft", "card_types", "mostly_spell") == STS_SETTINGS_OK
          && settings.draft.weights[STS_CARD_SPELL] == 4);
    CHECK(sts_settings_get(&settings, "draft", "card_types", value, sizeof value) && strcmp(value, "mostly_spell") == 0);
    before = settings;
    CHECK(sts_settings_set(&settings, "reward", "who_picks", "everyone") == STS_SETTINGS_BAD_VALUE);
    CHECK(sts_settings_set(&settings, "draft", "card_types", "dragon") == STS_SETTINGS_BAD_VALUE);
    CHECK(sts_settings_set(&settings, "starter", "common", "1,2") == STS_SETTINGS_UNKNOWN_KEY);
    CHECK(sts_settings_set(&settings, "draft", "colour", "red") == STS_SETTINGS_UNKNOWN_KEY);
    CHECK(sts_settings_equal(&settings, &before));

    /* Numbers step within the screen's range. */
    sts_settings_defaults(&settings);
    CHECK(sts_settings_step(&settings, "draft", "offers", 1) && settings.draft.offer_count == 4);
    for (wau = 0; wau < 20; wau++)
        sts_settings_step(&settings, "draft", "offers", 1);
    CHECK(settings.draft.offer_count == STS_OFFER_MAX);
    for (wau = 0; wau < 20; wau++)
        sts_settings_step(&settings, "reward", "copies_max", -1);
    CHECK(settings.reward.copies_max == 1);

    /* Choices go round in order, both ways. */
    CHECK(sts_settings_step(&settings, "reward", "who_picks", -1) && settings.who_picks == STS_WHO_RANDOM);
    CHECK(sts_settings_step(&settings, "reward", "who_picks", 1) && settings.who_picks == STS_WHO_BOTH);
    CHECK(sts_settings_step(&settings, "draft", "show_deck", 1) && settings.draft_show_deck == 0);
    CHECK(sts_settings_step(&settings, "draft", "show_deck", 1) && settings.draft_show_deck == 1);
    CHECK(sts_settings_step(&settings, "reward", "card_types", -1) && settings.reward.style == STS_OFFER_BY_CARD);
    CHECK(sts_settings_step(&settings, "reward", "card_types", 1) && settings.reward.style == STS_OFFER_WEIGHTED
          && settings.reward.weights[STS_CARD_SYSTEM] == 1);
    for (wau = 0; wau < 6; wau++) {
        CHECK(sts_settings_step(&settings, "reward", "card_types", 1));
        CHECK(settings.reward.type_mask == STS_TYPE_ALL);
    }
    CHECK(sts_settings_get(&settings, "reward", "card_types", value, sizeof value) && strcmp(value, "weighted") == 0);

    /* The card lists are not settings. */
    before = settings;
    CHECK(!sts_settings_step(&settings, "starter", "common", 1));
    CHECK(!sts_settings_step(&settings, "draft", "nonsense", 1));
    CHECK(sts_settings_equal(&settings, &before));

    /* The chance of a random offer steps by 25 within 0 to 100, and a value between steps moves
     * onto the next one. */
    sts_settings_defaults(&settings);
    CHECK(settings.random_chance == 50);
    CHECK(sts_settings_step(&settings, "reward", "random_chance", 1) && settings.random_chance == 75);
    CHECK(sts_settings_step(&settings, "reward", "random_chance", 1) && settings.random_chance == 100);
    CHECK(sts_settings_step(&settings, "reward", "random_chance", 1) && settings.random_chance == 100);
    settings.random_chance = 25;
    CHECK(sts_settings_step(&settings, "reward", "random_chance", -1) && settings.random_chance == 0);
    CHECK(sts_settings_step(&settings, "reward", "random_chance", -1) && settings.random_chance == 0);
    settings.random_chance = 60;
    CHECK(sts_settings_step(&settings, "reward", "random_chance", 1) && settings.random_chance == 75);
    settings.random_chance = 60;
    CHECK(sts_settings_step(&settings, "reward", "random_chance", -1) && settings.random_chance == 50);
    CHECK(parse_ini("[reward]\nrandom_chance=33\n", &settings, NULL) == STS_SETTINGS_OK && settings.random_chance == 33);
    CHECK(sts_settings_validate(&settings) == NULL);
    CHECK(parse_ini("[reward]\nrandom_chance=101\n", &settings, NULL) == STS_SETTINGS_OK);
    CHECK(sts_settings_check(&settings, NULL) == STS_PROBLEM_RANDOM_CHANCE && sts_settings_validate(&settings) != NULL);

    /* Which card comes in is a setting of its own, and only counts when it is not the default. */
    sts_settings_defaults(&settings);
    before = settings;
    CHECK(sts_settings_step(&settings, "reward", "offer_pick", 1) && settings.offer_pick == STS_OFFER_PICK_RANDOM);
    CHECK(sts_settings_hash(&settings) != sts_settings_hash(&before));
    CHECK(sts_settings_get(&settings, "reward", "offer_pick", value, sizeof value) && strcmp(value, "random") == 0);

    /* It only counts towards the fingerprint while offers are at random. */
    sts_settings_defaults(&settings);
    before = settings;
    settings.random_chance = 75;
    CHECK(sts_settings_hash(&settings) == sts_settings_hash(&before));
    settings.who_picks = before.who_picks = STS_WHO_RANDOM;
    CHECK(sts_settings_hash(&settings) != sts_settings_hash(&before));

    /* Fewest and most copies cannot pass each other. The draft's copies are not settings. */
    sts_settings_defaults(&settings);
    CHECK(!sts_settings_step(&settings, "draft", "copies_max", 1) && settings.draft.copies_max == 1);
    CHECK(sts_settings_step(&settings, "reward", "copies_min", 1) && settings.reward.copies_min == 1);
    CHECK(sts_settings_step(&settings, "reward", "copies_max", 1) && settings.reward.copies_max == 2);
    CHECK(sts_settings_step(&settings, "reward", "copies_min", 1) && settings.reward.copies_min == 2);
    CHECK(sts_settings_step(&settings, "reward", "copies_min", 1) && settings.reward.copies_min == 2);
    CHECK(sts_settings_step(&settings, "reward", "copies_max", -1) && settings.reward.copies_max == 2);
    for (wau = 0; wau < 5; wau++)
        sts_settings_step(&settings, "reward", "copies_max", 1);
    CHECK(settings.reward.copies_max == STS_GAME_COPY_LIMIT);
    settings.reward.copies_min = 3;   /* upside down, from the file */
    settings.reward.copies_max = 2;
    CHECK(sts_settings_step(&settings, "reward", "copies_min", -1) && settings.reward.copies_min == 2);

    /* Equal covers settings outside the fingerprint too. */
    sts_settings_defaults(&settings);
    before = settings;
    settings.allow_rule_changes = !settings.allow_rule_changes;
    CHECK(!sts_settings_equal(&settings, &before));
    CHECK(sts_settings_hash(&settings) == sts_settings_hash(&before));
}

/* Rewrites text with changed settings, checking the result parses back to them. */
static char *rewrite_checked(const char *text, const sts_settings *settings)
{
    sts_settings parsed;
    size_t length = 0;
    char *result = sts_settings_rewrite(text, text ? strlen(text) : 0, settings, &length);

    CHECK(result != NULL);
    if (!result)
        return NULL;
    CHECK(length == strlen(result));
    CHECK(sts_settings_parse(result, length, &parsed, NULL) == STS_SETTINGS_OK);
    CHECK(sts_settings_equal(&parsed, settings));
    return result;
}

static void test_settings_rewrite(void)
{
    sts_settings settings;
    size_t length = 0;
    char *shipped = sts_read_file(STS_SETTINGS_PATH, &length);
    char *result;
    int wau;

    /* Unchanged settings give back the file exactly. */
    CHECK(shipped != NULL);
    if (!shipped)
        return;
    sts_settings_defaults(&settings);
    result = rewrite_checked(shipped, &settings);
    CHECK(result && strcmp(result, shipped) == 0);
    free(result);

    /* Changes rewrite only their own lines; every comment stays. */
    CHECK(sts_settings_set(&settings, "draft", "offers", "2") == STS_SETTINGS_OK);
    CHECK(sts_settings_set(&settings, "starter", "enabled", "true") == STS_SETTINGS_OK);
    CHECK(sts_settings_set(&settings, "reward", "who_picks", "winner") == STS_SETTINGS_OK);
    result = rewrite_checked(shipped, &settings);
    if (result) {
        CHECK(strstr(result, "\noffers=2\n") && strstr(result, "\nenabled=true\n") && strstr(result, "\nwho_picks=winner\n"));
        CHECK(strstr(result, "\nwho_picks=both\n") == NULL);
        /* 3 to 2 keeps the length, false to true is one shorter, both to winner two longer. */
        CHECK(strlen(result) == strlen(shipped) - 1 + 2);
        CHECK(strstr(result, "# How many cards each pick offers. The screen fits up to 3.\noffers=2\n") != NULL);
        free(result);
    }

    /* Missing settings go after the last setting of their section, or into a new section at the end. */
    sts_settings_defaults(&settings);
    settings.draft_show_deck = 0;
    settings.reward.offer_count = 4;
    settings.starter_enabled = 1;
    result = rewrite_checked("[draft]\npicks=20\n\n# rewards\n[reward]\nmode=replace\n", &settings);
    if (result) {
        CHECK(strcmp(result, "[draft]\npicks=20\nshow_deck=false\n\n# rewards\n[reward]\nmode=replace\noffers=4\n"
                             "\n[starter]\nenabled=true\n") == 0);
        free(result);
    }

    /* Windows line endings are kept, for changed and added lines alike. */
    result = rewrite_checked("[draft]\r\npicks = 20\r\n[reward]\r\noffers=3\r\n", &settings);
    if (result) {
        CHECK(strcmp(result, "[draft]\r\npicks = 20\r\nshow_deck=false\r\n[reward]\r\noffers=4\r\n"
                             "\r\n[starter]\r\nenabled=true\r\n") == 0);
        free(result);
    }

    /* A last line without a newline, and spacing around the value. */
    sts_settings_defaults(&settings);
    settings.rules.deck_max = 25;
    settings.draft.offer_count = 4;
    result = rewrite_checked("[deck]\n  size_max =  30\n[draft]\npicks=20", &settings);
    if (result) {
        CHECK(strcmp(result, "[deck]\n  size_max =  25\n[draft]\npicks=20\noffers=4\n") == 0);
        free(result);
    }

    /* Old card lists in a file are left as they are, and new settings go after them. */
    sts_settings_defaults(&settings);
    settings.starter_enabled = 1;
    result = rewrite_checked("[starter]\nReimu=101,101\n", &settings);
    if (result) {
        CHECK(strcmp(result, "[starter]\nReimu=101,101\nenabled=true\n") == 0);
        free(result);
    }

    /* No file at all: only what differs from the defaults is written. */
    sts_settings_defaults(&settings);
    result = rewrite_checked(NULL, &settings);
    if (result) {
        CHECK(result[0] == 0);
        free(result);
    }
    settings.allow_rule_changes = 1;
    result = rewrite_checked("", &settings);
    if (result) {
        CHECK(strcmp(result, "[run]\nallow_rule_changes=true\n") == 0);
        free(result);
    }

    /* Text that does not parse is not rewritten. */
    CHECK(sts_settings_rewrite("[draft]\noffers=many\n", strlen("[draft]\noffers=many\n"), &settings, NULL) == NULL);
    CHECK(sts_settings_rewrite("[nowhere]\n", strlen("[nowhere]\n"), &settings, NULL) == NULL);

    /* Every steppable setting, changed at once, survives a rewrite of the shipped file. */
    sts_settings_defaults(&settings);
    for (wau = 0; wau < 2; wau++) {
        static const char *const keys[][2] = {
            {"deck", "size_min"}, {"deck", "size_max"}, {"deck", "copy_limit"},
            {"draft", "offers"}, {"draft", "show_deck"},
            {"draft", "card_types"}, {"starter", "enabled"}, {"reward", "mode"}, {"reward", "who_picks"},
            {"reward", "random_chance"}, {"reward", "offer_pick"},
            {"reward", "offers"}, {"reward", "copies_min"}, {"reward", "copies_max"}, {"reward", "card_types"},
            {"reward", "replacement"}, {"netplay", "pick_visibility"},
            {"netplay", "opponent_deck"}, {"items", "enabled"}, {"run", "allow_rule_changes"},
        };
        int key;

        for (key = 0; key < (int)(sizeof keys / sizeof keys[0]); key++)
            CHECK(sts_settings_step(&settings, keys[key][0], keys[key][1], wau == 0 ? -1 : 1));
        result = rewrite_checked(shipped, &settings);
        free(result);
    }
    free(shipped);
}

/* The rules a run saves: each written as section.key=value and read back, the settings that are
 * not rules left out, and the fingerprint the same afterwards. */
static void test_settings_rules_text(void)
{
    sts_settings settings, copy;
    char text[64];
    int wau, line = 0, count = sts_settings_rule_count();

    CHECK(count > 10);
    CHECK(parse_ini("[draft]\nshow_deck=false\ncard_types=mostly_skill\n[reward]\nwho_picks=random\n"
                    "random_chance=75\nreplacement=by_type\noffer_pick=random\n[starter]\nenabled=true\n"
                    "[run]\nallow_rule_changes=true\n", &settings, &line) == STS_SETTINGS_OK);
    sts_settings_defaults(&copy);
    for (wau = 0; wau < count; wau++) {
        CHECK(sts_settings_rule_text(&settings, wau, text, sizeof text));
        CHECK(strchr(text, '.') != NULL && strchr(text, '=') != NULL);
        CHECK(strncmp(text, "draft.show_deck=", 16) != 0 && strncmp(text, "run.allow_rule_changes=", 23) != 0);
        CHECK(sts_settings_set_rule(&copy, text) == STS_SETTINGS_OK);
    }
    CHECK(sts_settings_hash(&copy) == sts_settings_hash(&settings));
    CHECK(copy.draft_show_deck == 1 && copy.allow_rule_changes == 0);   /* left as they were */

    CHECK(!sts_settings_rule_text(&settings, count, text, sizeof text));
    CHECK(!sts_settings_rule_text(&settings, 0, text, 4));
    CHECK(sts_settings_set_rule(&copy, "draft.show_deck=true") == STS_SETTINGS_UNKNOWN_KEY);
    CHECK(sts_settings_set_rule(&copy, "run.allow_rule_changes=true") == STS_SETTINGS_UNKNOWN_KEY);
    CHECK(sts_settings_set_rule(&copy, "deck.no_such_key=1") == STS_SETTINGS_UNKNOWN_KEY);
    CHECK(sts_settings_set_rule(&copy, "no dot or equals") == STS_SETTINGS_UNKNOWN_KEY);
    CHECK(sts_settings_set_rule(&copy, "reward.random_chance=often") == STS_SETTINGS_BAD_VALUE);
    CHECK(copy.random_chance == 75);
    CHECK(sts_settings_set_rule(&copy, "run.matches=12") == STS_SETTINGS_OK && copy.match_limit == 12);
}

/* The match limit: 0 (endless) by default, counted in the fingerprint only when set, and no longer
 * than a run's history. */
static void test_settings_match_limit(void)
{
    sts_settings settings, before;
    int line = 0;

    sts_settings_defaults(&settings);
    before = settings;
    CHECK(settings.match_limit == 0);
    CHECK(parse_ini("[run]\nmatches=0\n", &settings, &line) == STS_SETTINGS_OK);
    CHECK(sts_settings_hash(&settings) == sts_settings_hash(&before));
    CHECK(parse_ini("[run]\nmatches=10\n", &settings, &line) == STS_SETTINGS_OK && settings.match_limit == 10);
    CHECK(sts_settings_hash(&settings) != sts_settings_hash(&before) && sts_settings_validate(&settings) == NULL);
    CHECK(parse_ini("[run]\nmatches=20\n", &settings, &line) == STS_SETTINGS_OK && sts_settings_validate(&settings) == NULL);
    CHECK(parse_ini("[run]\nmatches=21\n", &settings, &line) == STS_SETTINGS_OK);
    CHECK(sts_settings_check(&settings, NULL) == STS_PROBLEM_MATCH_LIMIT && sts_settings_validate(&settings) != NULL);
    CHECK(parse_ini("[run]\nmatches=-1\n", &settings, &line) == STS_SETTINGS_BAD_VALUE);

    sts_settings_defaults(&settings);
    CHECK(sts_settings_step(&settings, "run", "matches", -1) && settings.match_limit == 0);
    CHECK(sts_settings_step(&settings, "run", "matches", 1) && settings.match_limit == 1);
    settings.match_limit = STS_MATCH_LIMIT_MAX;
    CHECK(sts_settings_step(&settings, "run", "matches", 1) && settings.match_limit == STS_MATCH_LIMIT_MAX);
}

void test_settings(void)
{
    test_settings_defaults();
    test_settings_show_deck();
    test_settings_every_key();
    test_settings_long_lines();
    test_settings_offer_style();
    test_settings_errors();
    test_settings_validation();
    test_settings_fingerprint();
    test_settings_compatible();
    test_settings_starter();
    test_settings_one_value();
    test_settings_rewrite();
    test_settings_rules_text();
    test_settings_match_limit();
}
