#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "run.h"
#include "util.h"

/* 2 added the owed line; 3 the seed, restarts, offer time, rules and history. Versions 1 and 2 still
 * load. */
#define RUN_FORMAT_VERSION 3
#define CHECK_LINE_SIZE 15   /* "check " + 8 hex digits + newline */
#define MAX_TOKEN 64         /* the longest is a rule, such as "reward.replacement=by_type" */

const char *sts_run_status_str(sts_run_status status)
{
    switch (status) {
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
    case STS_RUN_BAD_STARTER:      return "the starter has a card this character cannot use";
    }
    return "unknown";
}

static void roll_offers(sts_run *run, const sts_card_db *db, const sts_rules *rules, const sts_offer_settings *cfg)
{
    int count = sts_offer_generate(db, run->character, &run->deck, rules, cfg, &run->rng, run->offers, STS_OFFER_MAX);

    run->offer_count = count > 0 ? count : 0;
    run->offer_frames = 0;
}

/* ---------- history ---------- */

static int copies_in(const sts_deck *deck, uint16_t id)
{
    int wau, count = 0;

    for (wau = 0; wau < deck->count; wau++)
        count += deck->ids[wau] == id;
    return count;
}

/* Lists, in out, each card with more copies in the deck `more` than in `fewer`, once per extra copy. */
static int cards_gained(const sts_deck *more, const sts_deck *fewer, uint16_t *out)
{
    int wau, earlier, count = 0;

    for (wau = 0; wau < more->count; wau++) {
        uint16_t id = more->ids[wau];
        int extra;

        for (earlier = 0; earlier < wau; earlier++)
            if (more->ids[earlier] == id)
                break;
        if (earlier < wau)
            continue;   /* counted already */
        for (extra = copies_in(more, id) - copies_in(fewer, id); extra > 0 && count < STS_RUN_CHANGE_MAX; extra--)
            out[count++] = id;
    }
    return count;
}

static void add_to_history(sts_run *run, const sts_match_result *result)
{
    sts_match_record *record;

    if (run->history_count == STS_RUN_HISTORY_MAX) {
        memmove(run->history, run->history + 1, sizeof run->history[0] * (STS_RUN_HISTORY_MAX - 1));
        run->history_count--;
        run->history_forgotten++;
    }
    record = &run->history[run->history_count++];
    memset(record, 0, sizeof *record);
    record->won = (uint8_t)(result->won != 0);
    record->rounds_won = (uint8_t)(result->rounds_won < 0 ? 0 : result->rounds_won > 255 ? 255 : result->rounds_won);
    record->rounds_lost = (uint8_t)(result->rounds_lost < 0 ? 0 : result->rounds_lost > 255 ? 255 : result->rounds_lost);
    record->opponent = (int8_t)(result->opponent >= 0 && result->opponent < STS_NUM_CHARACTERS ? result->opponent : -1);
    record->pending = 1;
}

/* A match's reward is settled: records what it changed, from the deck as it was before. Rewards are
 * settled oldest first, so it belongs to the oldest match still pending. */
static void settle_reward(sts_run *run, const sts_deck *before)
{
    int wau;

    for (wau = 0; wau < run->history_count; wau++) {
        sts_match_record *record = &run->history[wau];

        if (record->pending) {
            record->pending = 0;
            record->added_count = (uint8_t)cards_gained(&run->deck, before, record->added);
            record->removed_count = (uint8_t)cards_gained(before, &run->deck, record->removed);
            return;
        }
    }
}

/* Rolls the next draft offers or finishes the draft, which ends when the deck reaches size_min. */
static sts_run_status advance_draft(sts_run *run, const sts_card_db *db, const sts_settings *settings)
{
    if (run->deck.count < settings->rules.deck_min) {
        roll_offers(run, db, &settings->rules, &settings->draft);
        if (run->offer_count > 0)
            return STS_RUN_OK;
    }
    run->offer_count = 0;
    run->offer_frames = 0;
    if (sts_deck_validate(&run->deck, &settings->rules, NULL) != STS_DECK_OK)
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
    run->seed = seed;
    run->has_rules = 1;
    run->rules = *settings;
    sts_rng_seed(&run->rng, seed, (uint64_t)character);
    sts_settings_starter(settings, character, &run->deck);
    if (!sts_deck_cards_legal(db, character, &run->deck, NULL))
        return STS_RUN_BAD_STARTER;
    return advance_draft(run, db, settings);
}

const char *sts_run_check_starters(const sts_card_db *db, const sts_settings *settings)
{
    static char message[96];
    int character;

    for (character = 0; character < STS_NUM_CHARACTERS; character++) {
        sts_deck starter;
        uint16_t bad_id;

        sts_settings_starter(settings, character, &starter);
        if (!sts_deck_cards_legal(db, character, &starter, &bad_id)) {
            sprintf(message, "%s's starter has card %d, which %s cannot use", sts_character_name(character),
                    bad_id, sts_character_name(character));
            return message;
        }
    }
    return NULL;
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

static sts_apply_status apply_reward(sts_run *run, const sts_card_db *db, const sts_settings *settings,
                                     const sts_offer *offer, sts_replace_control replace,
                                     const sts_replace_choice *choice)
{
    switch (settings->reward.mode) {
    case STS_REWARD_ADD:
        return sts_offer_apply_add(&run->deck, offer, &settings->rules);
    case STS_REWARD_REMOVE:
        return sts_offer_apply_remove(&run->deck, offer, &settings->rules);
    case STS_REWARD_REPLACE:
        switch (replace) {
        case STS_REPLACE_CHOOSE:
            if (!choice || !choice->remove_ids)
                return STS_APPLY_BAD_REMOVAL;
            return sts_offer_replace_chosen(&run->deck, offer, &settings->rules, choice->remove_ids,
                                            choice->remove_count);
        case STS_REPLACE_BY_TYPE:
            if (!choice)
                return STS_APPLY_BAD_REMOVAL;
            return sts_offer_replace_by_type(&run->deck, offer, &settings->rules, db, run->character, choice->type,
                                             &run->rng);
        case STS_REPLACE_RANDOM:
            return sts_offer_replace_random(&run->deck, offer, &settings->rules, &run->rng);
        }
        break;
    case STS_REWARD_SKIP:
        break;
    }
    return STS_APPLY_ILLEGAL;
}

/* The offers roll as usual, so their types follow the settings, and then one is kept. It stays saved
 * as the only offer, so a restart cannot change which card it was. */
static void keep_one_offer_at_random(sts_run *run)
{
    run->offers[0] = run->offers[sts_rng_below(&run->rng, (uint32_t)run->offer_count)];
    run->offer_count = 1;
}

/* The reward for one match, from READY, before it is recorded. */
static void decide_reward(sts_run *run, const sts_card_db *db, const sts_settings *settings,
                          int won, sts_reward_outcome *outcome)
{
    int gets_offer;

    *outcome = STS_REWARD_NONE;
    switch (settings->who_picks) {
    case STS_WHO_BOTH:   gets_offer = 1; break;
    case STS_WHO_WINNER: gets_offer = won != 0; break;
    case STS_WHO_LOSER:  gets_offer = won == 0; break;
    /* From the run's saved random state, so restarting the game cannot reroll it. */
    case STS_WHO_RANDOM: gets_offer = (int)sts_rng_below(&run->rng, 100) < settings->random_chance; break;
    default:             gets_offer = 0; break;
    }
    if (!gets_offer)
        return;

    roll_offers(run, db, &settings->rules, &settings->reward);
    if (run->offer_count == 0)
        return;
    if (settings->offer_pick == STS_OFFER_PICK_RANDOM) {
        keep_one_offer_at_random(run);

        /* With nothing left to choose, the change makes itself. */
        if (settings->reward.mode != STS_REWARD_REPLACE || settings->reward.replace == STS_REPLACE_RANDOM) {
            sts_offer offer = run->offers[0];

            run->offer_count = 0;
            if (apply_reward(run, db, settings, &offer, STS_REPLACE_RANDOM, NULL) == STS_APPLY_OK)
                *outcome = STS_REWARD_AUTO;
            return;
        }
    }
    run->phase = STS_PHASE_REWARD;
    *outcome = STS_REWARD_OFFERS;
}

/* The number of the match whose reward comes next, the oldest still pending, counting from 1. */
static int next_reward_match(const sts_run *run)
{
    int wau;

    for (wau = 0; wau < run->history_count; wau++)
        if (run->history[wau].pending)
            return run->history_forgotten + wau + 1;
    return run->matches_played;
}

/* The run is over: no reward still to come changes the deck. */
static void finish_run(sts_run *run)
{
    while (run->history_count > 0 && run->history[run->history_count - 1].pending)
        settle_reward(run, &run->deck);
    run->offer_count = 0;
    run->offer_frames = 0;
    run->owed_count = 0;
    run->phase = STS_PHASE_FINISHED;
}

static int limit_reached(const sts_settings *settings, int match)
{
    return settings->match_limit > 0 && match >= settings->match_limit;
}

/* The reward for one match, from READY. Unless offers wait, it is settled at once. The last match
 * of the match limit has no reward, as its card would never be played: it finishes the run. */
static void reward_for_match(sts_run *run, const sts_card_db *db, const sts_settings *settings,
                             int won, sts_reward_outcome *outcome)
{
    sts_deck before = run->deck;

    if (limit_reached(settings, next_reward_match(run))) {
        finish_run(run);
        *outcome = STS_REWARD_FINISHED;
        return;
    }
    decide_reward(run, db, settings, won, outcome);
    if (*outcome != STS_REWARD_OFFERS)
        settle_reward(run, &before);
}

/* From READY: starts owed rewards, oldest first, until one gives offers or none are left. */
static void start_owed_rewards(sts_run *run, const sts_card_db *db, const sts_settings *settings)
{
    sts_reward_outcome outcome;

    while (run->phase == STS_PHASE_READY && run->owed_count > 0) {
        int won = run->owed_won[0];

        run->owed_count--;
        memmove(run->owed_won, run->owed_won + 1, (size_t)run->owed_count);
        reward_for_match(run, db, settings, won, &outcome);
    }
}

sts_run_status sts_run_begin_reward(sts_run *run, const sts_card_db *db, const sts_settings *settings,
                                    const sts_match_result *result, sts_reward_outcome *outcome)
{
    *outcome = STS_REWARD_NONE;
    if (!result)
        return STS_RUN_BAD_ARGUMENT;
    if (limit_reached(settings, run->matches_played))
        return STS_RUN_WRONG_PHASE;
    if (run->phase == STS_PHASE_REWARD) {
        if (run->owed_count >= STS_RUN_OWED_MAX)
            return STS_RUN_WRONG_PHASE;
        run->matches_played++;
        add_to_history(run, result);
        run->owed_won[run->owed_count++] = (uint8_t)(result->won != 0);
        *outcome = STS_REWARD_OFFERS;
        return STS_RUN_OK;
    }
    if (run->phase != STS_PHASE_READY)
        return STS_RUN_WRONG_PHASE;
    run->matches_played++;
    add_to_history(run, result);
    reward_for_match(run, db, settings, result->won, outcome);
    return STS_RUN_OK;
}

sts_run_status sts_run_reward_pick(sts_run *run, const sts_card_db *db, const sts_settings *settings,
                                   int index, const sts_replace_choice *choice)
{
    sts_deck before = run->deck;

    if (run->phase != STS_PHASE_REWARD)
        return STS_RUN_WRONG_PHASE;
    if (index < 0 || index >= run->offer_count)
        return STS_RUN_BAD_INDEX;
    if (apply_reward(run, db, settings, &run->offers[index], settings->reward.replace, choice) != STS_APPLY_OK)
        return STS_RUN_BAD_CHOICE;
    settle_reward(run, &before);
    run->offer_count = 0;
    run->offer_frames = 0;
    run->phase = STS_PHASE_READY;
    start_owed_rewards(run, db, settings);
    return STS_RUN_OK;
}

/* ---------- save format ---------- */

static const char *phase_name(sts_run_phase phase)
{
    switch (phase) {
    case STS_PHASE_DRAFT:  return "draft";
    case STS_PHASE_READY:  return "ready";
    case STS_PHASE_REWARD: return "reward";
    case STS_PHASE_FINISHED: return "finished";
    }
    return "?";
}

size_t sts_run_serialize(const sts_run *run, char *buf, size_t cap)
{
    size_t offset = 0;
    uint32_t check;
    int wau;

#define PUT(...) do { \
        int written = snprintf(buf + offset, cap - offset, __VA_ARGS__); \
        if (written < 0 || (size_t)written >= cap - offset) return 0; \
        offset += (size_t)written; \
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
    for (wau = 0; wau < run->deck.count; wau++)
        PUT(" %u", (unsigned)run->deck.ids[wau]);
    PUT("\n");
    PUT("offers %d", run->offer_count);
    for (wau = 0; wau < run->offer_count; wau++)
        PUT(" %u %u %d", (unsigned)run->offers[wau].id, (unsigned)run->offers[wau].type, run->offers[wau].copies);
    PUT("\n");
    PUT("owed %d", run->owed_count);
    for (wau = 0; wau < run->owed_count; wau++)
        PUT(" %d", (int)run->owed_won[wau]);
    PUT("\n");
    PUT("seed %08x %08x\n", (unsigned)(run->seed >> 32), (unsigned)(run->seed & 0xFFFFFFFFu));
    PUT("restarts %d\n", run->restarts);
    PUT("offer_frames %d\n", run->offer_frames);
    if (run->has_rules) {
        int count = sts_settings_rule_count();

        PUT("rules %d", count);
        for (wau = 0; wau < count; wau++) {
            char rule[MAX_TOKEN];

            if (!sts_settings_rule_text(&run->rules, wau, rule, sizeof rule))
                return 0;
            PUT(" %s", rule);
        }
        PUT("\n");
    } else {
        PUT("rules 0\n");
    }
    /* One line per match: won, rounds won and lost, opponent + 1 (0 if not known), pending, then
     * the cards that came in and the cards that left, each as a count and the IDs. */
    PUT("history %d %d\n", run->history_count, run->history_forgotten);
    for (wau = 0; wau < run->history_count; wau++) {
        const sts_match_record *record = &run->history[wau];
        int card;

        PUT("match %d %d %d %d %d %d", (int)record->won, (int)record->rounds_won, (int)record->rounds_lost,
            record->opponent + 1, (int)record->pending, (int)record->added_count);
        for (card = 0; card < record->added_count; card++)
            PUT(" %u", (unsigned)record->added[card]);
        PUT(" %d", (int)record->removed_count);
        for (card = 0; card < record->removed_count; card++)
            PUT(" %u", (unsigned)record->removed[card]);
        PUT("\n");
    }
    check = sts_fnv1a(STS_FNV_START, buf, offset);
    PUT("check %08x\n", (unsigned)check);
#undef PUT
    return offset;
}

/* A cursor over whitespace-separated tokens. */
typedef struct {
    const char *cursor;
    const char *end;
} reader;

static int next_token(reader *stream, char *out)
{
    size_t length = 0;

    while (stream->cursor < stream->end && (*stream->cursor == ' ' || *stream->cursor == '\n'))
        stream->cursor++;
    while (stream->cursor < stream->end && *stream->cursor != ' ' && *stream->cursor != '\n') {
        if (length + 1 >= MAX_TOKEN)
            return 0;
        out[length++] = *stream->cursor++;
    }
    out[length] = 0;
    return length > 0;
}

static int expect(reader *stream, const char *word)
{
    char token[MAX_TOKEN];
    return next_token(stream, token) && strcmp(token, word) == 0;
}

static int read_int(reader *stream, int max, int *out)
{
    char token[MAX_TOKEN];
    char *stop;
    unsigned long value;

    if (!next_token(stream, token) || token[0] < '0' || token[0] > '9')
        return 0;
    value = strtoul(token, &stop, 10);
    if (*stop || value > (unsigned long)max)
        return 0;
    *out = (int)value;
    return 1;
}

static int read_hex32(reader *stream, uint32_t *out)
{
    char token[MAX_TOKEN];
    char *stop;

    if (!next_token(stream, token) || strlen(token) != 8)
        return 0;
    *out = (uint32_t)strtoul(token, &stop, 16);
    return *stop == 0;
}

static int read_phase(reader *stream, sts_run_phase *out)
{
    char token[MAX_TOKEN];

    if (!next_token(stream, token))
        return 0;
    if (strcmp(token, "draft") == 0)
        *out = STS_PHASE_DRAFT;
    else if (strcmp(token, "ready") == 0)
        *out = STS_PHASE_READY;
    else if (strcmp(token, "reward") == 0)
        *out = STS_PHASE_REWARD;
    else if (strcmp(token, "finished") == 0)
        *out = STS_PHASE_FINISHED;
    else
        return 0;
    return 1;
}

/* A count and that many card IDs. */
static int read_cards(reader *stream, uint16_t *ids, uint8_t *count)
{
    int number, wau, value;

    if (!read_int(stream, STS_RUN_CHANGE_MAX, &number))
        return 0;
    for (wau = 0; wau < number; wau++) {
        if (!read_int(stream, 0xFFFF, &value))
            return 0;
        ids[wau] = (uint16_t)value;
    }
    *count = (uint8_t)number;
    return 1;
}

/* The lines version 3 added. Rules this version does not know (or values it refuses) are not
 * damage: the run just cannot put its rules back. */
static int parse_version_3(reader *stream, sts_run *out)
{
    uint32_t seed_high, seed_low;
    int count, wau, value;

    if (!expect(stream, "seed") || !read_hex32(stream, &seed_high) || !read_hex32(stream, &seed_low)
        || !expect(stream, "restarts") || !read_int(stream, 1000000000, &out->restarts)
        || !expect(stream, "offer_frames") || !read_int(stream, 1000000000, &out->offer_frames)
        || !expect(stream, "rules") || !read_int(stream, 1000, &count))
        return 0;
    out->seed = ((uint64_t)seed_high << 32) | seed_low;

    sts_settings_defaults(&out->rules);
    out->has_rules = count > 0;
    for (wau = 0; wau < count; wau++) {
        char rule[MAX_TOKEN];

        if (!next_token(stream, rule))
            return 0;
        if (sts_settings_set_rule(&out->rules, rule) != STS_SETTINGS_OK)
            out->has_rules = 0;
    }
    if (!out->has_rules)
        memset(&out->rules, 0, sizeof out->rules);

    if (!expect(stream, "history") || !read_int(stream, STS_RUN_HISTORY_MAX, &out->history_count)
        || !read_int(stream, 1000000000, &out->history_forgotten))
        return 0;
    for (wau = 0; wau < out->history_count; wau++) {
        sts_match_record *record = &out->history[wau];

        if (!expect(stream, "match") || !read_int(stream, 1, &value))
            return 0;
        record->won = (uint8_t)value;
        if (!read_int(stream, 255, &value))
            return 0;
        record->rounds_won = (uint8_t)value;
        if (!read_int(stream, 255, &value))
            return 0;
        record->rounds_lost = (uint8_t)value;
        if (!read_int(stream, STS_NUM_CHARACTERS, &value))
            return 0;
        record->opponent = (int8_t)(value - 1);
        if (!read_int(stream, 1, &value))
            return 0;
        record->pending = (uint8_t)value;
        if (!read_cards(stream, record->added, &record->added_count)
            || !read_cards(stream, record->removed, &record->removed_count))
            return 0;
    }
    return 1;
}

sts_run_status sts_run_parse(const char *text, size_t len, sts_run *out)
{
    char line[CHECK_LINE_SIZE + 1];
    reader stream;
    uint32_t stored, state_high, state_low, inc_high, inc_low;
    int version, count, wau, value;
    size_t body;

    /* The checksum goes first: nothing below can then be trusted to be merely damaged. */
    if (len < CHECK_LINE_SIZE)
        return STS_RUN_DAMAGED;
    body = len - CHECK_LINE_SIZE;
    memcpy(line, text + body, CHECK_LINE_SIZE);
    line[CHECK_LINE_SIZE] = 0;
    stream.cursor = line;
    stream.end = line + CHECK_LINE_SIZE;
    if (line[CHECK_LINE_SIZE - 1] != '\n' || !expect(&stream, "check") || !read_hex32(&stream, &stored))
        return STS_RUN_DAMAGED;
    if (stored != sts_fnv1a(STS_FNV_START, text, body))
        return STS_RUN_DAMAGED;

    memset(out, 0, sizeof *out);
    stream.cursor = text;
    stream.end = text + body;
    if (!expect(&stream, "sts_run") || !read_int(&stream, 1000000, &version))
        return STS_RUN_DAMAGED;
    if (version < 1 || version > RUN_FORMAT_VERSION)
        return STS_RUN_BAD_VERSION;

    if (!expect(&stream, "settings") || !read_hex32(&stream, &out->settings_hash)
        || !expect(&stream, "character") || !read_int(&stream, 1000, &out->character)
        || !expect(&stream, "phase") || !read_phase(&stream, &out->phase)
        || !expect(&stream, "picks") || !read_int(&stream, 1000000, &out->picks_done)
        || !expect(&stream, "matches") || !read_int(&stream, 1000000000, &out->matches_played)
        || !expect(&stream, "rng") || !read_hex32(&stream, &state_high) || !read_hex32(&stream, &state_low)
        || !read_hex32(&stream, &inc_high) || !read_hex32(&stream, &inc_low))
        return STS_RUN_DAMAGED;
    out->rng.state = ((uint64_t)state_high << 32) | state_low;
    out->rng.inc = ((uint64_t)inc_high << 32) | inc_low;

    if (!expect(&stream, "deck") || !read_int(&stream, STS_MAX_DECK, &count))
        return STS_RUN_DAMAGED;
    for (wau = 0; wau < count; wau++) {
        if (!read_int(&stream, 0xFFFF, &value))
            return STS_RUN_DAMAGED;
        out->deck.ids[wau] = (uint16_t)value;
    }
    out->deck.count = count;

    if (!expect(&stream, "offers") || !read_int(&stream, STS_OFFER_MAX, &count))
        return STS_RUN_DAMAGED;
    for (wau = 0; wau < count; wau++) {
        int type;
        if (!read_int(&stream, 0xFFFF, &value) || !read_int(&stream, STS_CARD_SPELL, &type)
            || !read_int(&stream, STS_MAX_DECK, &out->offers[wau].copies))
            return STS_RUN_DAMAGED;
        out->offers[wau].id = (uint16_t)value;
        out->offers[wau].type = (uint8_t)type;
    }
    out->offer_count = count;

    /* Version 1 had no owed rewards. */
    if (version >= 2) {
        if (!expect(&stream, "owed") || !read_int(&stream, STS_RUN_OWED_MAX, &count))
            return STS_RUN_DAMAGED;
        for (wau = 0; wau < count; wau++) {
            if (!read_int(&stream, 1, &value))
                return STS_RUN_DAMAGED;
            out->owed_won[wau] = (uint8_t)value;
        }
        out->owed_count = count;
    }
    if (version >= 3 && !parse_version_3(&stream, out))
        return STS_RUN_DAMAGED;
    return STS_RUN_OK;
}

sts_run_status sts_run_save(const char *path, const sts_run *run)
{
    char text[STS_RUN_TEXT_MAX];
    char tmp[1024];
    size_t count = sts_run_serialize(run, text, sizeof text);
    FILE *file;
    int ok;

    if (count == 0 || strlen(path) + 5 > sizeof tmp)
        return STS_RUN_IO;
    sprintf(tmp, "%s.tmp", path);
    file = fopen(tmp, "wb");
    if (!file)
        return STS_RUN_IO;
    ok = fwrite(text, 1, count, file) == count;
    ok = fflush(file) == 0 && ok;
    ok = fclose(file) == 0 && ok;
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
    sts_run_status status;

    if (!text)
        return STS_RUN_IO;
    status = sts_run_parse(text, len, out);
    free(text);
    return status;
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
    int wau;

    if (run->settings_hash != sts_settings_hash(settings))
        return STS_RUN_SETTINGS_CHANGED;
    if (run->character < 0 || run->character >= STS_NUM_CHARACTERS)
        return STS_RUN_ILLEGAL;
    if (run->picks_done > settings->rules.deck_min)
        return STS_RUN_ILLEGAL;
    if (!sts_deck_cards_legal(db, run->character, &run->deck, NULL))
        return STS_RUN_ILLEGAL;

    /* A draft in progress is the only time the deck may be below size_min. */
    if (run->phase == STS_PHASE_DRAFT)
        partial.deck_min = 0;
    if (sts_deck_validate(&run->deck, &partial, NULL) != STS_DECK_OK)
        return STS_RUN_ILLEGAL;

    /* Offers wait in the draft and after a match, and at no other time. */
    if ((run->phase == STS_PHASE_DRAFT || run->phase == STS_PHASE_REWARD) != (run->offer_count != 0))
        return STS_RUN_ILLEGAL;
    if (run->history_count < 0 || run->history_count > STS_RUN_HISTORY_MAX || run->history_forgotten < 0)
        return STS_RUN_ILLEGAL;
    if (run->phase == STS_PHASE_DRAFT && run->history_count + run->history_forgotten > 0)
        return STS_RUN_ILLEGAL;
    if (run->owed_count < 0 || run->owed_count > STS_RUN_OWED_MAX
        || (run->owed_count > 0 && run->phase != STS_PHASE_REWARD))
        return STS_RUN_ILLEGAL;
    for (wau = 0; wau < run->offer_count; wau++) {
        const sts_card *card = sts_card_db_find(db, run->character, run->offers[wau].id);
        if (!card || card->type != run->offers[wau].type || run->offers[wau].copies < 1)
            return STS_RUN_ILLEGAL;
    }
    return STS_RUN_OK;
}

sts_run_status sts_run_adopt_rules(sts_run *run, const sts_card_db *db, const sts_settings *settings)
{
    const sts_offer_settings *cfg = run->phase == STS_PHASE_DRAFT ? &settings->draft : &settings->reward;
    sts_run updated = *run;
    sts_run_status status;
    int wau, offers_valid = 1;

    updated.settings_hash = sts_settings_hash(settings);
    updated.has_rules = 1;
    updated.rules = *settings;
    status = sts_run_validate(&updated, db, settings);
    if (status != STS_RUN_OK)
        return status;

    for (wau = 0; wau < updated.offer_count; wau++)
        if (!sts_offer_is_valid(db, updated.character, &updated.deck, &settings->rules, cfg, &updated.offers[wau]))
            offers_valid = 0;

    /* A lower match limit than the matches already played ends the run now. */
    if ((updated.phase == STS_PHASE_READY && limit_reached(settings, updated.matches_played))
        || (updated.phase == STS_PHASE_REWARD && limit_reached(settings, next_reward_match(&updated)))) {
        finish_run(&updated);
        *run = updated;
        return STS_RUN_OK;
    }
    if (!offers_valid) {
        if (updated.phase == STS_PHASE_DRAFT) {
            status = advance_draft(&updated, db, settings);
            if (status != STS_RUN_OK)
                return status;
        } else {
            roll_offers(&updated, db, &settings->rules, cfg);
            if (updated.offer_count == 0) {
                /* Nothing can be offered under the new rules, so this reward ends with no change. */
                settle_reward(&updated, &updated.deck);
                updated.phase = STS_PHASE_READY;
                start_owed_rewards(&updated, db, settings);
            } else if (settings->offer_pick == STS_OFFER_PICK_RANDOM) {
                keep_one_offer_at_random(&updated);
            }
        }
    }
    *run = updated;
    return STS_RUN_OK;
}

int sts_run_rules_restorable(const sts_run *run)
{
    return run->has_rules && sts_settings_hash(&run->rules) == run->settings_hash;
}

/* Takes one copy of id out of the deck. Returns 0 if it holds none. */
static int remove_one_copy(sts_deck *deck, uint16_t id)
{
    int wau;

    for (wau = 0; wau < deck->count; wau++) {
        if (deck->ids[wau] == id) {
            deck->ids[wau] = deck->ids[--deck->count];
            return 1;
        }
    }
    return 0;
}

int sts_run_deck_after(const sts_run *run, int index, sts_deck *out)
{
    int match, card;

    *out = run->deck;
    if (index < -1 || index >= run->history_count)
        return 0;
    for (match = run->history_count - 1; match > index; match--) {
        const sts_match_record *record = &run->history[match];

        for (card = 0; card < record->added_count; card++)
            if (!remove_one_copy(out, record->added[card]))
                return 0;
        for (card = 0; card < record->removed_count; card++)
            if (sts_deck_add(out, record->removed[card]) != 0)
                return 0;
    }
    sts_deck_sort(out);
    return 1;
}
