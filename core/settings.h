/* Every rule of a run, loaded from the mod's .ini. The community has not settled the
 * rules, so nothing here is hardcoded elsewhere; these defaults are provisional until
 * playtesting. See data/settings.ini for the file format. */
#ifndef STS_SETTINGS_H
#define STS_SETTINGS_H

#include <stddef.h>
#include <stdint.h>
#include "deck.h"
#include "offer.h"
#include "pf.h"
#include "rules.h"

/* The game drops a 5th copy and then silently starts with no deck, and raising the
 * limit would need its own patch, so settings may not go above this for now. */
#define STS_GAME_COPY_LIMIT 4

/* A set seed: up to this many letters, digits, '-' and '_', and a NUL. */
#define STS_SEED_SIZE 25

/* The reward's time limit, in seconds: the longest, the default, and its step on the settings screen. */
#define STS_TIME_LIMIT_MAX 90
#define STS_TIME_LIMIT_DEFAULT 0   /* no limit, for now */
#define STS_TIME_LIMIT_STEP 15

/* The longest match limit. A run's history keeps more (STS_RUN_HISTORY_MAX), so a finished run
 * always has every match. */
#define STS_MATCH_LIMIT_MAX 20

/* When this player gets a card offer, from their own result alone. Each player's mod applies it
 * by itself, so nothing is exchanged with the opponent. With no offer the deck stays unchanged. */
typedef enum {
    STS_WHO_BOTH,
    STS_WHO_WINNER,
    STS_WHO_LOSER,
    STS_WHO_RANDOM      /* a random_chance in 100 chance of an offer after each match, win or lose */
} sts_who_picks;

/* Which of the cards offered comes in. */
typedef enum {
    STS_OFFER_PICK_CHOOSE,   /* the player chooses */
    STS_OFFER_PICK_RANDOM    /* one is taken at random; with replacement random too, the swap makes itself */
} sts_offer_pick;

typedef enum {
    STS_VISIBILITY_HIDDEN,
    STS_VISIBILITY_AFTER_CONFIRM,
    STS_VISIBILITY_LIVE
} sts_pick_visibility;

typedef enum {
    STS_OPPONENT_DECK_CHANGE,
    STS_OPPONENT_DECK_FULL
} sts_opponent_deck;

/* Cards a draft starts from, before the first pick. A card listed twice is two copies. */
#define STS_STARTER_MAX 32

typedef struct {
    uint16_t ids[STS_STARTER_MAX];
    int count;
} sts_starter_list;

typedef struct {
    sts_rules rules;
    /* The draft adds one copy per pick until the deck holds size_min cards (rewards may grow it
     * towards size_max later): mode is always ADD and copies always 1. Only the offers, card types and their weights can be set. */
    sts_offer_settings draft;
    sts_offer_settings reward;
    sts_who_picks who_picks;
    int random_chance;   /* out of 100, for STS_WHO_RANDOM */
    /* Seconds to take a reward, 0 for no limit. When it runs out, what is left to choose is chosen at
     * random, so an opponent never waits long. */
    int time_limit;
    sts_offer_pick offer_pick;
    sts_pick_visibility pick_visibility;
    sts_opponent_deck opponent_deck;
    int items_enabled;
    /* The starting deck: every character gets starter_common, plus their own list. A draft
     * starts from these cards and the picks fill the rest. Only starter_enabled is a setting; the
     * lists are built in. */
    int starter_enabled;
    sts_starter_list starter_common;
    sts_starter_list starter[STS_NUM_CHARACTERS];
    /* May a run made under other rules carry on under these? This says how to treat a
     * rule change rather than being a rule, so it is not part of the fingerprint. */
    int allow_rule_changes;
    /* How many matches a run lasts, 0 for no limit. The last one gives no offer: the run is finished. */
    int match_limit;
    /* A set seed for events, or empty for a random one. With one, every run of a character starts from
     * the same random state, so the draft is the same for everyone, and on starting over. Capitals
     * and small letters differ. */
    char seed[STS_SEED_SIZE];
    /* Is the deck so far shown while drafting? It changes nothing about the deck or what is
     * offered, so a run can carry on when it changes: not part of the fingerprint. */
    int draft_show_deck;
} sts_settings;

typedef enum {
    STS_SETTINGS_OK = 0,
    STS_SETTINGS_IO,               /* file could not be read */
    STS_SETTINGS_BAD_LINE,         /* neither [section], key=value, comment nor blank */
    STS_SETTINGS_UNKNOWN_SECTION,
    STS_SETTINGS_UNKNOWN_KEY,      /* rejected so that a typo cannot silently do nothing */
    STS_SETTINGS_BAD_VALUE
} sts_settings_status;

void sts_settings_defaults(sts_settings *settings);

/* Starts from the defaults, so a file only has to list what it changes. Stops at the
 * first error and sets *err_line (if non-NULL) to its 1-based line number. '#' and ';'
 * start comment lines. Does not check that the values make sense together; use
 * sts_settings_validate for that. */
sts_settings_status sts_settings_parse(const char *text, size_t len, sts_settings *out, int *err_line);

sts_settings_status sts_settings_load(const char *path, sts_settings *out, int *err_line);

const char *sts_settings_status_str(sts_settings_status status);

/* Why settings cannot work. The offer problems are listed in the same order for the draft and the
 * reward. */
typedef enum {
    STS_PROBLEM_NONE = 0,
    STS_PROBLEM_DECK_SIZE,          /* size_min below 1 or above size_max */
    STS_PROBLEM_DECK_TOO_BIG,       /* size_max above STS_MAX_DECK */
    STS_PROBLEM_COPY_LIMIT,         /* copy_limit outside 1 to STS_GAME_COPY_LIMIT */
    STS_PROBLEM_DRAFT_OFFERS,       /* offers outside 1 to STS_OFFER_MAX */
    STS_PROBLEM_DRAFT_COPIES,       /* copies_min below 1 or above copies_max (not settable for the draft) */
    STS_PROBLEM_DRAFT_COPY_LIMIT,   /* copies_max above copy_limit (not settable for the draft) */
    STS_PROBLEM_STARTER_TOO_BIG,    /* the character's starter is above size_max */
    STS_PROBLEM_STARTER_COPIES,     /* number: the card with more than copy_limit copies */
    STS_PROBLEM_REWARD_OFFERS,
    STS_PROBLEM_REWARD_COPIES,
    STS_PROBLEM_REWARD_COPY_LIMIT,
    STS_PROBLEM_REWARD_SIZE,        /* add or remove with a fixed deck size */
    STS_PROBLEM_RANDOM_CHANCE,      /* random_chance above 100 */
    STS_PROBLEM_MATCH_LIMIT,        /* matches above STS_MATCH_LIMIT_MAX */
    STS_PROBLEM_TIME_LIMIT          /* time_limit above STS_TIME_LIMIT_MAX */
} sts_settings_problem;

typedef struct {
    int character;   /* the character a problem is about, or -1 */
    int number;      /* see the problem */
} sts_settings_problem_info;

/* The first reason the settings cannot work, or STS_PROBLEM_NONE. info may be NULL. Starter cards
 * are only counted here; sts_run_check_starters checks each character can use them. */
sts_settings_problem sts_settings_check(const sts_settings *settings, sts_settings_problem_info *info);

/* Returns NULL if the settings can work, or a message saying why they cannot, for the log and for
 * errors in settings.ini. The settings screen words them for players from sts_settings_check. */
const char *sts_settings_validate(const sts_settings *settings);

/* The cards this character's draft starts from: none unless the starter is enabled. */
void sts_settings_starter(const sts_settings *settings, int character, sts_deck *out);

/* How many picks this character's draft takes: one per card from its starter up to size_min. */
int sts_settings_draft_length(const sts_settings *settings, int character);

/* Compatible mode (opponent is vanilla): 20 cards, 4 copies, no items, no rule that needs another
 * deck size. The result always passes sts_settings_validate. Visibility settings are
 * left alone: they need messages this mode never sends, so they have no effect here, and
 * rules already compatible keep their fingerprint. */
void sts_settings_force_compatible(sts_settings *settings);

/* Fingerprint of every rule, stable across machines. Differs if any setting differs. */
uint32_t sts_settings_hash(const sts_settings *settings);

/* One setting, by its section and key as in the file, for the settings screen. */

/* Writes the setting's value as the file would hold it ("true", "system,spell", "3").
 * Returns 0 for an unknown setting. */
int sts_settings_get(const sts_settings *settings, const char *section, const char *key, char *out, size_t size);

/* Sets a setting from text as the file would hold it. A bad value changes nothing. */
sts_settings_status sts_settings_set(sts_settings *settings, const char *section, const char *key, const char *value);

/* Moves a setting to its next value (direction 1) or previous one (-1): numbers by one within the
 * range the screen offers (fewest copies up to most copies, and most copies from fewest copies up
 * to copy_limit; random_chance by 25, onto the next multiple of 25), choices in order and wrapping round. Returns 0 for an unknown setting. Does not
 * validate. */
int sts_settings_step(sts_settings *settings, const char *section, const char *key, int direction);

/* The rule settings one by one, for a run to save the rules it was made under: every setting except
 * the ones that say how to play rather than being rules (show_deck, allow_rule_changes). */
int sts_settings_rule_count(void);

/* Writes the index-th rule as "section.key=value". Returns 0 past the end or if it does not fit. */
int sts_settings_rule_text(const sts_settings *settings, int index, char *out, size_t size);

/* Sets one rule from text written by sts_settings_rule_text. Any other setting is refused as
 * STS_SETTINGS_UNKNOWN_KEY, and a bad value changes nothing. */
sts_settings_status sts_settings_set_rule(sts_settings *settings, const char *text);

/* 1 if every setting is the same, including ones outside the fingerprint. */
int sts_settings_equal(const sts_settings *first, const sts_settings *second);

/* Rewrites the text of a settings file so it holds these settings. Only values that change are
 * rewritten, so comments and layout stay; a setting the file does not list is added after the last
 * setting of its section, or in a new section at the end. Returns text to free(), with its length in
 * *out_len, or NULL if the original text does not parse (or memory runs out). */
char *sts_settings_rewrite(const char *text, size_t len, const sts_settings *settings, size_t *out_len);

#endif
