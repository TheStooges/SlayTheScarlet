/* Tests: the state file (the run in use and the counts of runs started over). */
#include "test_util.h"
#include "state.h"

static int states_equal(const sts_state *first, const sts_state *second)
{
    int wau;

    if (first->active != second->active || first->count != second->count
        || memcmp(first->found, second->found, sizeof first->found) != 0
        || first->gallery_unlocked != second->gallery_unlocked)
        return 0;
    for (wau = 0; wau < first->count; wau++)
        if (first->counts[wau].character != second->counts[wau].character
            || first->counts[wau].rules_hash != second->counts[wau].rules_hash
            || first->counts[wau].restarts != second->counts[wau].restarts)
            return 0;
    return 1;
}

/* Text given as a C string. */
static int parse_text(const char *text, sts_state *out)
{
    return sts_state_parse(text, strlen(text), out);
}

void test_state(void)
{
    static sts_state state, loaded;
    static char text[STS_STATE_TEXT_MAX];
    size_t length;
    int wau;

    sts_state_init(&state);
    CHECK(state.active == -1 && state.count == 0 && sts_state_restarts(&state, 3, 0x1234) == 0);

    /* Counted per character and per rules. */
    sts_state_add_restart(&state, 3, 0x1234);
    sts_state_add_restart(&state, 3, 0x1234);
    sts_state_add_restart(&state, 3, 0x9999);
    sts_state_add_restart(&state, 4, 0x1234);
    CHECK(sts_state_restarts(&state, 3, 0x1234) == 2 && sts_state_restarts(&state, 3, 0x9999) == 1);
    CHECK(sts_state_restarts(&state, 4, 0x1234) == 1 && sts_state_restarts(&state, 5, 0x1234) == 0);
    CHECK(state.count == 3);

    /* Round trip. */
    state.active = 7;
    length = sts_state_serialize(&state, text, sizeof text);
    CHECK(length > 0 && sts_state_parse(text, length, &loaded) && states_equal(&state, &loaded));
    state.active = -1;
    length = sts_state_serialize(&state, text, sizeof text);
    CHECK(length > 0 && sts_state_parse(text, length, &loaded) && loaded.active == -1);

    /* Full: the least recently counted is forgotten, and the whole state still fits. */
    sts_state_init(&state);
    for (wau = 0; wau < STS_STATE_COUNTS_MAX; wau++)
        sts_state_add_restart(&state, wau % 20, (uint32_t)wau);
    sts_state_add_restart(&state, 0, 0);   /* counted again, so now the most recent */
    sts_state_add_restart(&state, 19, 0xFFFFFFFFu);
    CHECK(state.count == STS_STATE_COUNTS_MAX);
    CHECK(sts_state_restarts(&state, 0, 0) == 2 && sts_state_restarts(&state, 1, 1) == 0);
    CHECK(sts_state_restarts(&state, 19, 0xFFFFFFFFu) == 1);
    for (wau = 0; wau < STS_STATE_COUNTS_MAX; wau++)
        state.counts[wau].restarts = 1000000000;
    state.active = 19;
    length = sts_state_serialize(&state, text, sizeof text);
    CHECK(length > 0 && sts_state_parse(text, length, &loaded) && states_equal(&state, &loaded));
    CHECK(sts_state_serialize(&state, text, 10) == 0);

    /* Anything else reads as a fresh state. */
    CHECK(!sts_state_parse("", 0, &loaded) && loaded.active == -1 && loaded.count == 0);
    CHECK(!sts_state_parse("3\n", 2, &loaded) && loaded.active == -1);   /* the old active.txt */
    CHECK(!parse_text("sts_state 2\nactive 1\nrestarts 0\n", &loaded));
    CHECK(!parse_text("sts_state 1\nactive 99\nrestarts 0\n", &loaded));
    CHECK(!parse_text("sts_state 1\nactive 1\nrestarts 2\n0 00000001 1\n", &loaded) && loaded.count == 0);
    CHECK(!parse_text("sts_state 1\nactive 1\nrestarts 1\n0 00000001 -1\n", &loaded));
    CHECK(parse_text("sts_state 1\nactive 1\nrestarts 1\n0 0000000a 4\n", &loaded));
    CHECK(loaded.active == 1 && sts_state_restarts(&loaded, 0, 10) == 4);
    /* A file from before the cards found were kept has none found. */
    CHECK(!sts_state_found(&loaded, 0, 100) && !sts_state_found(&loaded, STS_STATE_FOUND_SYSTEM, 0));

    /* Cards found: per character and for the system cards, each counted once. */
    sts_state_init(&state);
    CHECK(sts_state_add_found(&state, 0, 100) && !sts_state_add_found(&state, 0, 100));
    CHECK(sts_state_add_found(&state, 0, 219) && sts_state_add_found(&state, STS_STATE_FOUND_SYSTEM, 0));
    CHECK(sts_state_add_found(&state, STS_STATE_FOUND_SYSTEM, 20) && sts_state_add_found(&state, 19, 255));
    CHECK(sts_state_found(&state, 0, 100) && !sts_state_found(&state, 1, 100) && !sts_state_found(&state, 0, 101));
    CHECK(!sts_state_add_found(&state, 0, 256) && !sts_state_add_found(&state, -1, 1)
          && !sts_state_add_found(&state, STS_STATE_FOUND_SETS, 1) && !sts_state_found(&state, 0, 256));
    state.active = 2;
    sts_state_add_restart(&state, 2, 0xABCD);
    length = sts_state_serialize(&state, text, sizeof text);
    CHECK(length > 0 && sts_state_parse(text, length, &loaded) && states_equal(&state, &loaded));
    CHECK(strstr(text, "found 3\n0 100 219\n19 255\n20 0 20\n") != NULL);
    /* Every card of every set found still fits. */
    for (wau = 0; wau < STS_STATE_FOUND_SETS * STS_STATE_CARD_IDS; wau++)
        sts_state_add_found(&state, wau / STS_STATE_CARD_IDS, (unsigned)(wau % STS_STATE_CARD_IDS));
    length = sts_state_serialize(&state, text, sizeof text);
    CHECK(length > 0 && sts_state_parse(text, length, &loaded) && states_equal(&state, &loaded));
    /* Damage in the cards found makes the whole file unreadable, as damage elsewhere does. */
    CHECK(!parse_text("sts_state 1\nactive 1\nrestarts 0\nfound 1\n21 5\n", &loaded));
    CHECK(!parse_text("sts_state 1\nactive 1\nrestarts 0\nfound 1\n0 300\n", &loaded));
    CHECK(!parse_text("sts_state 1\nactive 1\nrestarts 0\nfound 1\n0 1x\n", &loaded));
    CHECK(!parse_text("sts_state 1\nactive 1\nrestarts 0\nfound 99\n", &loaded));
    CHECK(parse_text("sts_state 1\nactive 1\nrestarts 0\nfound 1\n3 104 200\n", &loaded));
    CHECK(sts_state_found(&loaded, 3, 104) && sts_state_found(&loaded, 3, 200) && !sts_state_found(&loaded, 3, 105));
    CHECK(!loaded.gallery_unlocked);

    /* The gallery unlock: kept, and read only as 0 or 1. */
    sts_state_init(&state);
    CHECK(!state.gallery_unlocked);
    state.gallery_unlocked = 1;
    length = sts_state_serialize(&state, text, sizeof text);
    CHECK(length > 0 && sts_state_parse(text, length, &loaded) && loaded.gallery_unlocked == 1);
    CHECK(parse_text("sts_state 1\nactive 1\nrestarts 0\nfound 0\ngallery_unlocked 0\n", &loaded)
          && !loaded.gallery_unlocked);
    CHECK(!parse_text("sts_state 1\nactive 1\nrestarts 0\nfound 0\ngallery_unlocked 2\n", &loaded));
}
