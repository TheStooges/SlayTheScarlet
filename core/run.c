#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "run.h"
#include "util.h"

#define RUN_FORMAT_VERSION 2   /* 2 added the owed line; version 1 saves still load */
#define CHECK_LINE_SIZE 15   /* "check " + 8 hex digits + newline */
#define MAX_TOKEN 24

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

/* The reward for one match, from READY. */
static void reward_for_match(sts_run *run, const sts_card_db *db, const sts_settings *settings,
                             int won, sts_reward_outcome *outcome)
{
    int gets_offer;

    *outcome = STS_REWARD_NONE;
    switch (settings->who_picks) {
    case STS_WHO_BOTH:   gets_offer = 1; break;
    case STS_WHO_WINNER: gets_offer = won != 0; break;
    case STS_WHO_LOSER:  gets_offer = won == 0; break;
    case STS_WHO_NOBODY: gets_offer = 1; break;
    default:             gets_offer = 0; break;
    }
    if (!gets_offer)
        return;

    /* A replacement type only makes sense if a person picks it. */
    if (settings->who_picks == STS_WHO_NOBODY) {
        sts_offer_settings cfg = settings->reward;

        cfg.replace = STS_REPLACE_RANDOM;
        roll_offers(run, db, &settings->rules, &cfg);
        if (run->offer_count > 0) {
            int wau = (int)sts_rng_below(&run->rng, (uint32_t)run->offer_count);
            sts_offer offer = run->offers[wau];

            run->offer_count = 0;
            if (apply_reward(run, db, settings, &offer, STS_REPLACE_RANDOM, NULL) == STS_APPLY_OK)
                *outcome = STS_REWARD_AUTO;
        }
        return;
    }

    roll_offers(run, db, &settings->rules, &settings->reward);
    if (run->offer_count > 0) {
        run->phase = STS_PHASE_REWARD;
        *outcome = STS_REWARD_OFFERS;
    }
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
                                    int won, sts_reward_outcome *outcome)
{
    *outcome = STS_REWARD_NONE;
    if (run->phase == STS_PHASE_REWARD) {
        if (run->owed_count >= STS_RUN_OWED_MAX)
            return STS_RUN_WRONG_PHASE;
        run->matches_played++;
        run->owed_won[run->owed_count++] = (uint8_t)(won != 0);
        *outcome = STS_REWARD_OFFERS;
        return STS_RUN_OK;
    }
    if (run->phase != STS_PHASE_READY)
        return STS_RUN_WRONG_PHASE;
    run->matches_played++;
    reward_for_match(run, db, settings, won, outcome);
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
    else
        return 0;
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
    if (version != 1 && version != RUN_FORMAT_VERSION)
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

    if (run->phase == STS_PHASE_READY && run->offer_count != 0)
        return STS_RUN_ILLEGAL;
    if (run->phase != STS_PHASE_READY && run->offer_count == 0)
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
    status = sts_run_validate(&updated, db, settings);
    if (status != STS_RUN_OK)
        return status;

    for (wau = 0; wau < updated.offer_count; wau++)
        if (!sts_offer_is_valid(db, updated.character, &updated.deck, &settings->rules, cfg, &updated.offers[wau]))
            offers_valid = 0;

    if (!offers_valid) {
        if (updated.phase == STS_PHASE_DRAFT) {
            status = advance_draft(&updated, db, settings);
            if (status != STS_RUN_OK)
                return status;
        } else {
            roll_offers(&updated, db, &settings->rules, cfg);
            if (updated.offer_count == 0) {
                updated.phase = STS_PHASE_READY;
                start_owed_rewards(&updated, db, settings);
            }
        }
    }
    *run = updated;
    return STS_RUN_OK;
}
