/* Tests: Settings parsing, validation and fingerprint. */
#include "test_util.h"

static char g_text[512];

static void test_settings_defaults(void)
{
    sts_settings settings, defaults;
    int line = 0;

    /* The defaults are legal, and the shipped file states exactly the defaults. */
    sts_settings_defaults(&defaults);
    CHECK(sts_settings_validate(&defaults) == NULL);
    CHECK(defaults.rules.deck_min == 20 && defaults.rules.deck_max == 20 && defaults.rules.copy_limit == 4);
    CHECK(defaults.draft_picks == 20 && defaults.draft.offer_count == 3 && defaults.draft.mode == STS_REWARD_ADD);
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
                    "[draft]\npicks=22\noffers=4\ncopies_min=1\ncopies_max=2\ntypes=skill,spell\n"
                    "[reward]\nmode=add\nwho_picks=loser\noffers=5\ncopies_min=2\ncopies_max=3\n"
                    "types=system\nreplacement=by_type\n"
                    "[netplay]\npick_visibility=live\nopponent_deck=full\n"
                    "[items]\nenabled=true\n", &settings, &line) == STS_SETTINGS_OK);
    CHECK(settings.rules.deck_min == 15 && settings.rules.deck_max == 25 && settings.rules.copy_limit == 3);
    CHECK(settings.draft_picks == 22 && settings.draft.offer_count == 4 && settings.draft.copies_max == 2);
    CHECK(settings.draft.type_mask == (STS_TYPE_BIT(STS_CARD_SKILL) | STS_TYPE_BIT(STS_CARD_SPELL)));
    CHECK(settings.reward.mode == STS_REWARD_ADD && settings.who_picks == STS_WHO_LOSER
          && settings.reward.offer_count == 5);
    CHECK(settings.reward.copies_min == 2 && settings.reward.copies_max == 3);
    CHECK(settings.reward.type_mask == STS_TYPE_BIT(STS_CARD_SYSTEM) && settings.reward.replace == STS_REPLACE_BY_TYPE);
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

    /* Offer style and weights, separately for the draft and rewards. */
    CHECK(defaults.draft.style == STS_OFFER_WEIGHTED && defaults.reward.style == STS_OFFER_WEIGHTED);
    CHECK(defaults.reward.weights[STS_CARD_SYSTEM] == 1 && defaults.reward.weights[STS_CARD_SKILL] == 1
          && defaults.reward.weights[STS_CARD_SPELL] == 1);
    CHECK(parse_ini("[draft]\noffer_style=one_of_each\n[reward]\nsystem_weight=2\nskill_weight=3\nspell_weight=4\n",
                    &settings, &line) == STS_SETTINGS_OK);
    CHECK(settings.draft.style == STS_OFFER_ONE_OF_EACH && settings.reward.style == STS_OFFER_WEIGHTED);
    CHECK(settings.reward.weights[STS_CARD_SYSTEM] == 2 && settings.reward.weights[STS_CARD_SKILL] == 3
          && settings.reward.weights[STS_CARD_SPELL] == 4);
    CHECK(settings.draft.weights[STS_CARD_SYSTEM] == 1);
    CHECK(sts_settings_validate(&settings) == NULL);
    CHECK(parse_ini("[reward]\noffer_style=gamble\n", &settings, &line) == STS_SETTINGS_BAD_VALUE);
    CHECK(parse_ini("[reward]\nspell_weight=-1\n", &settings, &line) == STS_SETTINGS_BAD_VALUE);
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
    CHECK(parse_ini("[reward]\ntypes=system,trap\n", &settings, &line) == STS_SETTINGS_BAD_VALUE);
    CHECK(parse_ini("[reward]\ntypes=\n", &settings, &line) == STS_SETTINGS_BAD_VALUE);
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
    BAD_INI("[draft]\npicks=19\n");
    BAD_INI("[draft]\npicks=0\n");
    BAD_INI("[draft]\noffers=0\n");
    BAD_INI("[draft]\noffers=9\n");
    BAD_INI("[draft]\ncopies_max=5\n");
    BAD_INI("[draft]\ncopies_min=2\ncopies_max=3\npicks=10\n");
    BAD_INI("[draft]\ncopies_min=2\ncopies_max=2\npicks=9\n");
    BAD_INI("[draft]\ncopies_min=2\ncopies_max=2\npicks=11\n");
    BAD_INI("[reward]\nmode=add\n");
    BAD_INI("[reward]\nmode=remove\n");
    BAD_INI("[reward]\noffers=0\n");
    BAD_INI("[reward]\ncopies_min=3\ncopies_max=2\n");
    BAD_INI("[reward]\ncopies_max=5\n");
    BAD_INI("[reward]\nsystem_weight=0\nskill_weight=0\nspell_weight=0\n");
    BAD_INI("[draft]\nsystem_weight=0\nskill_weight=0\nspell_weight=0\n");
    BAD_INI("[reward]\ntypes=spell\nspell_weight=0\n");
#undef BAD_INI

    /* The weights only matter to the style that uses them. */
    CHECK(parse_ini("[reward]\noffer_style=one_of_each\nsystem_weight=0\nskill_weight=0\nspell_weight=0\n",
                    &settings, &line) == STS_SETTINGS_OK);
    CHECK(sts_settings_validate(&settings) == NULL);

    /* Workable variations are accepted, including a skip with otherwise odd offers. */
    CHECK(parse_ini("[draft]\ncopies_min=2\ncopies_max=2\npicks=10\n", &settings, &line) == STS_SETTINGS_OK);
    CHECK(sts_settings_validate(&settings) == NULL);
    CHECK(parse_ini("[draft]\ncopies_min=1\ncopies_max=4\n", &settings, &line) == STS_SETTINGS_OK);
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
    HASH_DIFFERS(draft_picks, 21);
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
        "[deck]\nsize_min=10\nsize_max=10\n[draft]\npicks=10\n",
        "[deck]\nsize_max=40\n[draft]\ncopies_min=2\ncopies_max=2\npicks=20\n",
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
    CHECK(defaults.starter[1].ids[0] == 102 && defaults.starter[1].ids[2] == 208);
    CHECK(defaults.starter[18].ids[1] == 101 && defaults.starter[18].ids[3] == 203);
    sts_settings_starter(&defaults, 0, &starter);
    CHECK(starter.count == 0);

    /* The shipped file states the default lists too, which the fingerprint only sees when on. */
    CHECK(sts_settings_load(STS_SETTINGS_PATH, &settings, &line) == STS_SETTINGS_OK);
    settings.starter_enabled = 1;
    defaults.starter_enabled = 1;
    CHECK(sts_settings_hash(&settings) == sts_settings_hash(&defaults));
    defaults.starter_enabled = 0;
    CHECK(sts_settings_draft_length(&defaults, 0) == 20);

    /* Lists change nothing while the starter is off, so runs made before it keep their rules. */
    CHECK(parse_ini("[starter]\nreimu=\ncommon=1\n", &settings, &line) == STS_SETTINGS_OK);
    CHECK(settings.starter[0].count == 0 && settings.starter_common.count == 1);
    CHECK(sts_settings_hash(&settings) == sts_settings_hash(&defaults));

    /* On, it is part of the rules, and so is every list. Patchouli's default skill on 421 is 104,
     * and A Penny Saved (12) is not in it. */
    CHECK(parse_ini("[starter]\nenabled=true\n", &settings, &line) == STS_SETTINGS_OK);
    CHECK(sts_settings_validate(&settings) == NULL);
    CHECK(sts_settings_hash(&settings) != sts_settings_hash(&defaults));
    sts_settings_starter(&settings, 4, &starter);
    CHECK(starter.count == 8 && sts_deck_count_of(&starter, 0) == 2 && sts_deck_count_of(&starter, 12) == 0
          && sts_deck_count_of(&starter, 13) == 2 && sts_deck_count_of(&starter, 104) == 2
          && sts_deck_count_of(&starter, 200) == 2);
    CHECK(sts_settings_draft_length(&settings, 4) == 12);
    {
        uint32_t enabled_hash = sts_settings_hash(&settings);
        CHECK(parse_ini("[starter]\nenabled=true\nSuwako=201\n", &settings, &line) == STS_SETTINGS_OK);
        CHECK(settings.starter[19].count == 1);
        CHECK(sts_settings_hash(&settings) != enabled_hash);
    }

    /* Bad lists and unknown names are refused. */
    CHECK(parse_ini("[starter]\nreimu=200,\n", &settings, &line) == STS_SETTINGS_BAD_VALUE);
    CHECK(parse_ini("[starter]\nreimu=200,,201\n", &settings, &line) == STS_SETTINGS_BAD_VALUE);
    CHECK(parse_ini("[starter]\nreimu=abc\n", &settings, &line) == STS_SETTINGS_BAD_VALUE);
    CHECK(parse_ini("[starter]\nreimu=70000\n", &settings, &line) == STS_SETTINGS_BAD_VALUE);
    CHECK(parse_ini("[starter]\nrumia=200\n", &settings, &line) == STS_SETTINGS_UNKNOWN_KEY);
    CHECK(parse_ini("[starter]\nreimux=200\n", &settings, &line) == STS_SETTINGS_UNKNOWN_KEY);
    CHECK(parse_ini("[starter]\ncommon=1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,"
                    "1,2,3,4,5,6,7,8,9,10,11,12,13\n", &settings, &line) == STS_SETTINGS_BAD_VALUE);

    /* The starter must leave a legal deck in reach. */
    CHECK(parse_ini("[starter]\nenabled=true\ncommon=0,0,0,0,0\n", &settings, &line) == STS_SETTINGS_OK);
    CHECK(sts_settings_validate(&settings) != NULL);
    CHECK(parse_ini("[starter]\nenabled=true\ncommon=0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4\n",
                    &settings, &line) == STS_SETTINGS_OK);
    CHECK(sts_settings_validate(&settings) != NULL);
    CHECK(parse_ini("[starter]\nenabled=true\n[draft]\ncopies_min=2\ncopies_max=2\npicks=10\n",
                    &settings, &line) == STS_SETTINGS_OK);
    CHECK(sts_settings_validate(&settings) != NULL);
    CHECK(parse_ini("[starter]\nenabled=true\n[draft]\ncopies_min=2\ncopies_max=2\npicks=6\n",
                    &settings, &line) == STS_SETTINGS_OK);
    CHECK(sts_settings_validate(&settings) == NULL);
    CHECK(sts_settings_draft_length(&settings, 0) == 6);
    CHECK(parse_ini("[starter]\nenabled=true\n[draft]\npicks=11\n", &settings, &line) == STS_SETTINGS_OK);
    CHECK(sts_settings_validate(&settings) != NULL);

    /* Compatible mode keeps a starter that fits, and turns off one that cannot. */
    CHECK(parse_ini("[starter]\nenabled=true\n", &settings, &line) == STS_SETTINGS_OK);
    sts_settings_force_compatible(&settings);
    CHECK(settings.starter_enabled == 1);
    CHECK(parse_ini("[deck]\nsize_min=30\nsize_max=30\n[draft]\npicks=30\n[starter]\nenabled=true\n"
                    "common=0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5\n", &settings, &line) == STS_SETTINGS_OK);
    CHECK(sts_settings_validate(&settings) == NULL);
    sts_settings_force_compatible(&settings);
    CHECK(settings.starter_enabled == 0 && sts_settings_validate(&settings) == NULL);
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
}
