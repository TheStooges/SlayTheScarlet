#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "run.h"
#include "util.h"

#define RUN_FORMAT_VERSION 1
#define CHECK_LINE_SIZE 15   /* "check " + 8 hex digits + newline */
#define MAX_TOKEN 24

const char *sts_run_status_str(sts_run_status s)
{
    switch (s) {
    case STS_RUN_OK:               return "ok";
    case STS_RUN_WRONG_PHASE:      return "not available in this phase";
    case STS_RUN_BAD_INDEX:        return "no such offer";
    case STS_RUN_BAD_CHOICE:       return "those cards cannot be removed";
    case STS_RUN_STUCK:            return "no eligible cards and the deck is too small";
    case STS_RUN_BAD_ARGUMENT:     return "bad argument";
    case STS_RUN_IO:               return "save file could not be read or written";
    case STS_RUN_DAMAGED:          return "save file is damaged";
    case STS_RUN_BAD_VERSION:      return "save file is from a different version";
    case STS_RUN_SETTINGS_CHANGED: return "save file was made under different rules";
    case STS_RUN_ILLEGAL:          return "save file breaks the rules";
    }
    return "unknown";
}

static void roll_offers(sts_run *run, const sts_card_db *db, const sts_rules *rules, const sts_offer_settings *cfg)
{
    int n = sts_offer_generate(db, run->character, &run->deck, rules, cfg, &run->rng, run->offers, STS_OFFER_MAX);

    run->offer_count = n > 0 ? n : 0;
}

/* Rolls the next draft offers or finishes the draft. A full deck ends it early, because
 * the generator then finds nothing eligible. */
static sts_run_status advance_draft(sts_run *run, const sts_card_db *db, const sts_settings *s)
{
    if (run->picks_done < s->draft_picks) {
        roll_offers(run, db, &s->rules, &s->draft);
        if (run->offer_count > 0)
            return STS_RUN_OK;
    }
    run->offer_count = 0;
    if (sts_deck_validate(&run->deck, &s->rules, NULL) != STS_DECK_OK)
        return STS_RUN_STUCK;
    run->phase = STS_PHASE_READY;
    return STS_RUN_OK;
}

sts_run_status sts_run_start(sts_run *run, const sts_card_db *db, const sts_settings *settings,
                             int character, uint64_t seed)
{
    if (character < 0 || character >= STS_NUM_CHARACTERS)
        return STS_RUN_BAD_ARGUMENT;
    memset(run, 0, sizeof *run);
    run->settings_hash = sts_settings_hash(settings);
    run->character = character;
    run->phase = STS_PHASE_DRAFT;
    sts_rng_seed(&run->rng, seed, (uint64_t)character);
    sts_deck_clear(&run->deck);
    return advance_draft(run, db, settings);
}

sts_run_status sts_run_draft_pick(sts_run *run, const sts_card_db *db, const sts_settings *settings, int index)
{
    if (run->phase != STS_PHASE_DRAFT)
        return STS_RUN_WRONG_PHASE;
    if (index < 0 || index >= run->offer_count)
        return STS_RUN_BAD_INDEX;
    if (sts_offer_apply_add(&run->deck, &run->offers[index], &settings->rules) != STS_APPLY_OK)
        return STS_RUN_BAD_CHOICE;
    run->picks_done++;
    return advance_draft(run, db, settings);
}

static sts_apply_status apply_reward(sts_run *run, const sts_card_db *db, const sts_settings *s,
                                     const sts_offer *offer, sts_replace_control replace,
                                     const sts_replace_choice *choice)
{
    switch (s->reward.mode) {
    case STS_REWARD_ADD:
        return sts_offer_apply_add(&run->deck, offer, &s->rules);
    case STS_REWARD_REMOVE:
        return sts_offer_apply_remove(&run->deck, offer, &s->rules);
    case STS_REWARD_REPLACE:
        switch (replace) {
        case STS_REPLACE_CHOOSE:
            if (!choice || !choice->remove_ids)
                return STS_APPLY_BAD_REMOVAL;
            return sts_offer_replace_chosen(&run->deck, offer, &s->rules, choice->remove_ids, choice->remove_count);
        case STS_REPLACE_BY_TYPE:
            if (!choice)
                return STS_APPLY_BAD_REMOVAL;
            return sts_offer_replace_by_type(&run->deck, offer, &s->rules, db, run->character, choice->type, &run->rng);
        case STS_REPLACE_RANDOM:
            return sts_offer_replace_random(&run->deck, offer, &s->rules, &run->rng);
        }
        break;
    case STS_REWARD_SKIP:
        break;
    }
    return STS_APPLY_ILLEGAL;
}

sts_run_status sts_run_begin_reward(sts_run *run, const sts_card_db *db, const sts_settings *settings,
                                    int won, sts_reward_outcome *outcome)
{
    int gets_offer;

    *outcome = STS_REWARD_NONE;
    if (run->phase != STS_PHASE_READY)
        return STS_RUN_WRONG_PHASE;
    run->matches_played++;

    switch (settings->who_picks) {
    case STS_WHO_BOTH:   gets_offer = 1; break;
    case STS_WHO_WINNER: gets_offer = won != 0; break;
    case STS_WHO_LOSER:  gets_offer = won == 0; break;
    case STS_WHO_NOBODY: gets_offer = 1; break;
    default:             gets_offer = 0; break;
    }
    if (!gets_offer)
        return STS_RUN_OK;

    /* A replacement type only makes sense if a person picks it. */
    if (settings->who_picks == STS_WHO_NOBODY) {
        sts_offer_settings cfg = settings->reward;

        cfg.replace = STS_REPLACE_RANDOM;
        roll_offers(run, db, &settings->rules, &cfg);
        if (run->offer_count > 0) {
            int i = (int)sts_rng_below(&run->rng, (uint32_t)run->offer_count);
            sts_offer offer = run->offers[i];

            run->offer_count = 0;
            if (apply_reward(run, db, settings, &offer, STS_REPLACE_RANDOM, NULL) == STS_APPLY_OK)
                *outcome = STS_REWARD_AUTO;
        }
        return STS_RUN_OK;
    }

    roll_offers(run, db, &settings->rules, &settings->reward);
    if (run->offer_count > 0) {
        run->phase = STS_PHASE_REWARD;
        *outcome = STS_REWARD_OFFERS;
    }
    return STS_RUN_OK;
}

sts_run_status sts_run_reward_pick(sts_run *run, const sts_card_db *db, const sts_settings *settings,
                                   int index, const sts_replace_choice *choice)
{
    if (run->phase != STS_PHASE_REWARD)
        return STS_RUN_WRONG_PHASE;
    if (index < 0 || index >= run->offer_count)
        return STS_RUN_BAD_INDEX;
    if (apply_reward(run, db, settings, &run->offers[index], settings->reward.replace, choice) != STS_APPLY_OK)
        return STS_RUN_BAD_CHOICE;
    run->offer_count = 0;
    run->phase = STS_PHASE_READY;
    return STS_RUN_OK;
}

/* ---------- save format ---------- */

static const char *phase_name(sts_run_phase p)
{
    switch (p) {
    case STS_PHASE_DRAFT:  return "draft";
    case STS_PHASE_READY:  return "ready";
    case STS_PHASE_REWARD: return "reward";
    }
    return "?";
}

size_t sts_run_serialize(const sts_run *run, char *buf, size_t cap)
{
    size_t o = 0;
    uint32_t check;
    int i;

#define PUT(...) do { \
        int w_ = snprintf(buf + o, cap - o, __VA_ARGS__); \
        if (w_ < 0 || (size_t)w_ >= cap - o) return 0; \
        o += (size_t)w_; \
    } while (0)

    if (cap == 0)
        return 0;
    PUT("sts_run %d\n", RUN_FORMAT_VERSION);
    PUT("settings %08x\n", (unsigned)run->settings_hash);
    PUT("character %d\n", run->character);
    PUT("phase %s\n", phase_name(run->phase));
    PUT("picks %d\n", run->picks_done);
    PUT("matches %d\n", run->matches_played);
    PUT("rng %08x %08x %08x %08x\n",
        (unsigned)(run->rng.state >> 32), (unsigned)(run->rng.state & 0xFFFFFFFFu),
        (unsigned)(run->rng.inc >> 32), (unsigned)(run->rng.inc & 0xFFFFFFFFu));
    PUT("deck %d", run->deck.count);
    for (i = 0; i < run->deck.count; i++)
        PUT(" %u", (unsigned)run->deck.ids[i]);
    PUT("\n");
    PUT("offers %d", run->offer_count);
    for (i = 0; i < run->offer_count; i++)
        PUT(" %u %u %d", (unsigned)run->offers[i].id, (unsigned)run->offers[i].type, run->offers[i].copies);
    PUT("\n");
    check = sts_fnv1a(STS_FNV_START, buf, o);
    PUT("check %08x\n", (unsigned)check);
#undef PUT
    return o;
}

/* A cursor over whitespace-separated tokens. */
typedef struct {
    const char *p;
    const char *end;
} reader;

static int next_token(reader *r, char *out)
{
    size_t n = 0;

    while (r->p < r->end && (*r->p == ' ' || *r->p == '\n'))
        r->p++;
    while (r->p < r->end && *r->p != ' ' && *r->p != '\n') {
        if (n + 1 >= MAX_TOKEN)
            return 0;
        out[n++] = *r->p++;
    }
    out[n] = 0;
    return n > 0;
}

static int expect(reader *r, const char *word)
{
    char t[MAX_TOKEN];
    return next_token(r, t) && strcmp(t, word) == 0;
}

static int read_int(reader *r, int max, int *out)
{
    char t[MAX_TOKEN];
    char *stop;
    unsigned long v;

    if (!next_token(r, t) || t[0] < '0' || t[0] > '9')
        return 0;
    v = strtoul(t, &stop, 10);
    if (*stop || v > (unsigned long)max)
        return 0;
    *out = (int)v;
    return 1;
}

static int read_hex32(reader *r, uint32_t *out)
{
    char t[MAX_TOKEN];
    char *stop;

    if (!next_token(r, t) || strlen(t) != 8)
        return 0;
    *out = (uint32_t)strtoul(t, &stop, 16);
    return *stop == 0;
}

static int read_phase(reader *r, sts_run_phase *out)
{
    char t[MAX_TOKEN];

    if (!next_token(r, t))
        return 0;
    if (strcmp(t, "draft") == 0)
        *out = STS_PHASE_DRAFT;
    else if (strcmp(t, "ready") == 0)
        *out = STS_PHASE_READY;
    else if (strcmp(t, "reward") == 0)
        *out = STS_PHASE_REWARD;
    else
        return 0;
    return 1;
}

sts_run_status sts_run_parse(const char *text, size_t len, sts_run *out)
{
    char line[CHECK_LINE_SIZE + 1];
    reader r;
    uint32_t stored, a, b, c, d;
    int version, count, i, v;
    size_t body;

    /* The checksum goes first: nothing below can then be trusted to be merely damaged. */
    if (len < CHECK_LINE_SIZE)
        return STS_RUN_DAMAGED;
    body = len - CHECK_LINE_SIZE;
    memcpy(line, text + body, CHECK_LINE_SIZE);
    line[CHECK_LINE_SIZE] = 0;
    r.p = line;
    r.end = line + CHECK_LINE_SIZE;
    if (line[CHECK_LINE_SIZE - 1] != '\n' || !expect(&r, "check") || !read_hex32(&r, &stored))
        return STS_RUN_DAMAGED;
    if (stored != sts_fnv1a(STS_FNV_START, text, body))
        return STS_RUN_DAMAGED;

    memset(out, 0, sizeof *out);
    r.p = text;
    r.end = text + body;
    if (!expect(&r, "sts_run") || !read_int(&r, 1000000, &version))
        return STS_RUN_DAMAGED;
    if (version != RUN_FORMAT_VERSION)
        return STS_RUN_BAD_VERSION;

    if (!expect(&r, "settings") || !read_hex32(&r, &out->settings_hash)
        || !expect(&r, "character") || !read_int(&r, 1000, &out->character)
        || !expect(&r, "phase") || !read_phase(&r, &out->phase)
        || !expect(&r, "picks") || !read_int(&r, 1000000, &out->picks_done)
        || !expect(&r, "matches") || !read_int(&r, 1000000000, &out->matches_played)
        || !expect(&r, "rng") || !read_hex32(&r, &a) || !read_hex32(&r, &b)
        || !read_hex32(&r, &c) || !read_hex32(&r, &d))
        return STS_RUN_DAMAGED;
    out->rng.state = ((uint64_t)a << 32) | b;
    out->rng.inc = ((uint64_t)c << 32) | d;

    if (!expect(&r, "deck") || !read_int(&r, STS_MAX_DECK, &count))
        return STS_RUN_DAMAGED;
    for (i = 0; i < count; i++) {
        if (!read_int(&r, 0xFFFF, &v))
            return STS_RUN_DAMAGED;
        out->deck.ids[i] = (uint16_t)v;
    }
    out->deck.count = count;

    if (!expect(&r, "offers") || !read_int(&r, STS_OFFER_MAX, &count))
        return STS_RUN_DAMAGED;
    for (i = 0; i < count; i++) {
        int type;
        if (!read_int(&r, 0xFFFF, &v) || !read_int(&r, STS_CARD_SPELL, &type)
            || !read_int(&r, STS_MAX_DECK, &out->offers[i].copies))
            return STS_RUN_DAMAGED;
        out->offers[i].id = (uint16_t)v;
        out->offers[i].type = (uint8_t)type;
    }
    out->offer_count = count;
    return STS_RUN_OK;
}

sts_run_status sts_run_save(const char *path, const sts_run *run)
{
    char text[STS_RUN_TEXT_MAX];
    char tmp[1024];
    size_t n = sts_run_serialize(run, text, sizeof text);
    FILE *f;
    int ok;

    if (n == 0 || strlen(path) + 5 > sizeof tmp)
        return STS_RUN_IO;
    sprintf(tmp, "%s.tmp", path);
    f = fopen(tmp, "wb");
    if (!f)
        return STS_RUN_IO;
    ok = fwrite(text, 1, n, f) == n;
    ok = fflush(f) == 0 && ok;
    ok = fclose(f) == 0 && ok;
    if (!ok) {
        remove(tmp);
        return STS_RUN_IO;
    }
    /* On Windows rename will not replace an existing file. If the remove lands and the
     * rename does not, path.tmp is still complete and sts_run_load will use it. */
    if (rename(tmp, path) != 0) {
        remove(path);
        if (rename(tmp, path) != 0)
            return STS_RUN_IO;
    }
    return STS_RUN_OK;
}

static sts_run_status load_one(const char *path, sts_run *out)
{
    size_t len = 0;
    char *text = sts_read_file(path, &len);
    sts_run_status st;

    if (!text)
        return STS_RUN_IO;
    st = sts_run_parse(text, len, out);
    free(text);
    return st;
}

sts_run_status sts_run_load(const char *path, sts_run *out)
{
    char tmp[1024];

    if (strlen(path) + 5 > sizeof tmp)
        return STS_RUN_IO;
    sprintf(tmp, "%s.tmp", path);
    if (load_one(tmp, out) == STS_RUN_OK)
        return STS_RUN_OK;
    return load_one(path, out);
}

sts_run_status sts_run_validate(const sts_run *run, const sts_card_db *db, const sts_settings *settings)
{
    const sts_rules *rules = &settings->rules;
    sts_rules partial = *rules;
    int i;

    if (run->settings_hash != sts_settings_hash(settings))
        return STS_RUN_SETTINGS_CHANGED;
    if (run->character < 0 || run->character >= STS_NUM_CHARACTERS)
        return STS_RUN_ILLEGAL;
    if (run->picks_done > settings->draft_picks)
        return STS_RUN_ILLEGAL;
    if (!sts_deck_cards_legal(db, run->character, &run->deck, NULL))
        return STS_RUN_ILLEGAL;

    /* A draft in progress is the only time the deck may be below size_min. */
    if (run->phase == STS_PHASE_DRAFT)
        partial.deck_min = 0;
    if (sts_deck_validate(&run->deck, &partial, NULL) != STS_DECK_OK)
        return STS_RUN_ILLEGAL;

    if (run->phase == STS_PHASE_READY && run->offer_count != 0)
        return STS_RUN_ILLEGAL;
    if (run->phase != STS_PHASE_READY && run->offer_count == 0)
        return STS_RUN_ILLEGAL;
    for (i = 0; i < run->offer_count; i++) {
        const sts_card *c = sts_card_db_find(db, run->character, run->offers[i].id);
        if (!c || c->type != run->offers[i].type || run->offers[i].copies < 1)
            return STS_RUN_ILLEGAL;
    }
    return STS_RUN_OK;
}

sts_run_status sts_run_adopt_rules(sts_run *run, const sts_card_db *db, const sts_settings *settings)
{
    const sts_offer_settings *cfg = run->phase == STS_PHASE_DRAFT ? &settings->draft : &settings->reward;
    sts_run updated = *run;
    sts_run_status st;
    int i, offers_valid = 1;

    updated.settings_hash = sts_settings_hash(settings);
    st = sts_run_validate(&updated, db, settings);
    if (st != STS_RUN_OK)
        return st;

    for (i = 0; i < updated.offer_count; i++)
        if (!sts_offer_is_valid(db, updated.character, &updated.deck, &settings->rules, cfg, &updated.offers[i]))
            offers_valid = 0;

    if (!offers_valid) {
        if (updated.phase == STS_PHASE_DRAFT) {
            st = advance_draft(&updated, db, settings);
            if (st != STS_RUN_OK)
                return st;
        } else {
            roll_offers(&updated, db, &settings->rules, cfg);
            if (updated.offer_count == 0)
                updated.phase = STS_PHASE_READY;
        }
    }
    *run = updated;
    return STS_RUN_OK;
}
