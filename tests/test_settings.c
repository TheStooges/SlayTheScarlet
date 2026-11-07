/* Tests: Settings parsing, validation and fingerprint. */
#include "test_util.h"

static char g_text[512];

void test_settings(void)
{
    sts_settings s, d;
    int line = 0;

    /* The defaults are legal, and the shipped file states exactly the defaults. */
    sts_settings_defaults(&d);
    CHECK(sts_settings_validate(&d) == NULL);
    CHECK(d.rules.deck_min == 20 && d.rules.deck_max == 20 && d.rules.copy_limit == 4);
    CHECK(d.draft_picks == 20 && d.draft.offer_count == 3 && d.draft.mode == STS_REWARD_ADD);
    CHECK(d.pick_visibility == STS_VISIBILITY_AFTER_CONFIRM && d.items_enabled == 0);
    CHECK(sts_settings_load(STS_SETTINGS_PATH, &s, &line) == STS_SETTINGS_OK);
    CHECK(sts_settings_hash(&s) == sts_settings_hash(&d));

    /* An empty file gives the defaults; a file only needs what it changes. */
    CHECK(parse_ini("", &s, &line) == STS_SETTINGS_OK);
    CHECK(sts_settings_hash(&s) == sts_settings_hash(&d));
    CHECK(parse_ini("# c\n; c\n\n  [deck]  \r\n  size_max = 30  \r\n", &s, &line) == STS_SETTINGS_OK);
    CHECK(s.rules.deck_max == 30 && s.rules.deck_min == 20);

    /* Every setting is reachable. */
    CHECK(parse_ini("[deck]\nsize_min=15\nsize_max=25\ncopy_limit=3\n"
                    "[draft]\npicks=22\noffers=4\ncopies_min=1\ncopies_max=2\ntypes=skill,spell\n"
                    "[reward]\nmode=add\nwho_picks=loser\noffers=5\ncopies_min=2\ncopies_max=3\n"
                    "types=system\nreplacement=by_type\n"
                    "[netplay]\npick_visibility=live\nopponent_deck=full\n"
                    "[items]\nenabled=true\n", &s, &line) == STS_SETTINGS_OK);
    CHECK(s.rules.deck_min == 15 && s.rules.deck_max == 25 && s.rules.copy_limit == 3);
    CHECK(s.draft_picks == 22 && s.draft.offer_count == 4 && s.draft.copies_max == 2);
    CHECK(s.draft.type_mask == (STS_TYPE_BIT(STS_CARD_SKILL) | STS_TYPE_BIT(STS_CARD_SPELL)));
    CHECK(s.reward.mode == STS_REWARD_ADD && s.who_picks == STS_WHO_LOSER && s.reward.offer_count == 5);
    CHECK(s.reward.copies_min == 2 && s.reward.copies_max == 3);
    CHECK(s.reward.type_mask == STS_TYPE_BIT(STS_CARD_SYSTEM) && s.reward.replace == STS_REPLACE_BY_TYPE);
    CHECK(s.pick_visibility == STS_VISIBILITY_LIVE && s.opponent_deck == STS_OPPONENT_DECK_FULL);
    CHECK(s.items_enabled == 1);
    CHECK(sts_settings_validate(&s) == NULL);

    /* A long comment is fine; a long setting is refused rather than truncated. */
    memset(g_text, 'x', 300);
    g_text[0] = ' ';
    g_text[1] = '#';
    g_text[300] = '\n';
    g_text[301] = 0;
    CHECK(parse_ini(g_text, &s, &line) == STS_SETTINGS_OK);
    g_text[1] = 'x';
    CHECK(parse_ini(g_text, &s, &line) == STS_SETTINGS_BAD_LINE && line == 1);

    /* Damage is reported with the right kind and line. */
    CHECK(parse_ini("[deck]\nnonsense\n", &s, &line) == STS_SETTINGS_BAD_LINE && line == 2);
    CHECK(parse_ini("[deck\n", &s, &line) == STS_SETTINGS_BAD_LINE && line == 1);
    CHECK(parse_ini("[nope]\n", &s, &line) == STS_SETTINGS_UNKNOWN_SECTION);
    CHECK(parse_ini("size_min=1\n", &s, &line) == STS_SETTINGS_UNKNOWN_KEY && line == 1);
    CHECK(parse_ini("[deck]\n\n\nsizemin=1\n", &s, &line) == STS_SETTINGS_UNKNOWN_KEY && line == 4);
    CHECK(parse_ini("[draft]\nmode=add\n", &s, &line) == STS_SETTINGS_UNKNOWN_KEY);
    CHECK(parse_ini("[deck]\nsize_min=12x\n", &s, &line) == STS_SETTINGS_BAD_VALUE);
    CHECK(parse_ini("[deck]\nsize_min=\n", &s, &line) == STS_SETTINGS_BAD_VALUE);
    CHECK(parse_ini("[deck]\nsize_min=-1\n", &s, &line) == STS_SETTINGS_BAD_VALUE);
    CHECK(parse_ini("[deck]\nsize_min=99999\n", &s, &line) == STS_SETTINGS_BAD_VALUE);
    CHECK(parse_ini("[reward]\nmode=gamble\n", &s, &line) == STS_SETTINGS_BAD_VALUE);
    CHECK(parse_ini("[reward]\ntypes=system,trap\n", &s, &line) == STS_SETTINGS_BAD_VALUE);
    CHECK(parse_ini("[reward]\ntypes=\n", &s, &line) == STS_SETTINGS_BAD_VALUE);
    CHECK(parse_ini("[items]\nenabled=1\n", &s, &line) == STS_SETTINGS_BAD_VALUE);
    CHECK(sts_settings_load("no/such/file.ini", &s, &line) == STS_SETTINGS_IO);
    CHECK(sts_settings_hash(&s) == sts_settings_hash(&d));    /* defaults even so */
    CHECK(strcmp(sts_settings_status_str(STS_SETTINGS_BAD_VALUE), "bad value") == 0);

    /* Combinations that could never work are refused. */
#define BAD_INI(text) do { \
        CHECK(parse_ini(text, &s, &line) == STS_SETTINGS_OK); \
        CHECK(sts_settings_validate(&s) != NULL); \
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
#undef BAD_INI

    /* Workable variations are accepted, including a skip with otherwise odd offers. */
    CHECK(parse_ini("[draft]\ncopies_min=2\ncopies_max=2\npicks=10\n", &s, &line) == STS_SETTINGS_OK);
    CHECK(sts_settings_validate(&s) == NULL);
    CHECK(parse_ini("[draft]\ncopies_min=1\ncopies_max=4\n", &s, &line) == STS_SETTINGS_OK);
    CHECK(sts_settings_validate(&s) == NULL);
    CHECK(parse_ini("[reward]\nmode=skip\noffers=0\n", &s, &line) == STS_SETTINGS_OK);
    CHECK(sts_settings_validate(&s) == NULL);
    CHECK(parse_ini("[deck]\nsize_min=15\nsize_max=25\n[reward]\nmode=remove\n", &s, &line) == STS_SETTINGS_OK);
    CHECK(sts_settings_validate(&s) == NULL);

    /* The fingerprint notices a change to any rule. */
    {
        uint32_t base = sts_settings_hash(&d);
#define HASH_DIFFERS(field, value) do { \
            s = d; s.field = value; \
            CHECK(sts_settings_hash(&s) != base); \
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
        HASH_DIFFERS(who_picks, STS_WHO_WINNER);
        HASH_DIFFERS(pick_visibility, STS_VISIBILITY_LIVE);
        HASH_DIFFERS(opponent_deck, STS_OPPONENT_DECK_FULL);
        HASH_DIFFERS(items_enabled, 1);
#undef HASH_DIFFERS
    }

    /* Compatible mode always ends up legal, 20 cards, 4 copies, no items. */
    {
        static const char *const inis[] = {
            "",
            "[deck]\nsize_min=15\nsize_max=30\n[reward]\nmode=add\n",
            "[deck]\nsize_min=10\nsize_max=10\n[draft]\npicks=10\n",
            "[deck]\nsize_max=40\n[draft]\ncopies_min=2\ncopies_max=2\npicks=20\n",
            "[deck]\ncopy_limit=2\n[reward]\nmode=remove\ncopies_max=2\n[items]\nenabled=true\n",
            "[deck]\nsize_min=15\nsize_max=30\n[reward]\nmode=skip\n[netplay]\npick_visibility=live\n",
        };
        size_t i;
        for (i = 0; i < sizeof inis / sizeof inis[0]; i++) {
            CHECK(parse_ini(inis[i], &s, &line) == STS_SETTINGS_OK);
            sts_settings_force_compatible(&s);
            CHECK(sts_settings_validate(&s) == NULL);
            CHECK(s.rules.deck_min == 20 && s.rules.deck_max == 20 && s.rules.copy_limit == 4);
            CHECK(s.items_enabled == 0);
            CHECK(s.reward.mode == STS_REWARD_REPLACE || s.reward.mode == STS_REWARD_SKIP);
        }

        /* Rules that are already compatible stay exactly as they were, so a run saved under the
         * defaults is not treated as made under different rules. */
        sts_settings_defaults(&s);
        sts_settings_force_compatible(&s);
        CHECK(sts_settings_hash(&s) == sts_settings_hash(&d));

        /* It only changes what it must: other reward and draft choices survive. */
        CHECK(parse_ini("[deck]\nsize_min=15\nsize_max=30\n[draft]\noffers=5\n"
                        "[reward]\nmode=add\noffers=4\nwho_picks=winner\n", &s, &line) == STS_SETTINGS_OK);
        sts_settings_force_compatible(&s);
        CHECK(s.reward.mode == STS_REWARD_REPLACE && s.reward.offer_count == 4);
        CHECK(s.draft.offer_count == 5 && s.who_picks == STS_WHO_WINNER);
    }
}
