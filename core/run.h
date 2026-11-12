/* A run: the starting draft, then a card offer after each match.
 *
 * All state, including the random state and any offers on screen, lives in sts_run and
 * is saved with it, so reloading shows the same offers and cannot reroll a pick. Run
 * state stays in the mod's own files; score123.dat and profiles are never touched here.
 */
#ifndef STS_RUN_H
#define STS_RUN_H

#include <stddef.h>
#include <stdint.h>
#include "deck.h"
#include "offer.h"
#include "pool.h"
#include "rng.h"
#include "settings.h"

typedef enum {
    STS_PHASE_DRAFT,    /* building the starting deck; offers are waiting */
    STS_PHASE_READY,    /* the deck is legal and can be played */
    STS_PHASE_REWARD,   /* a match ended and offers are waiting */
    STS_PHASE_FINISHED  /* the run is over: its deck can be viewed but not played */
} sts_run_phase;

/* Matches whose reward waits behind an offer still on screen, as after a disconnect during an
 * offer and a reconnect without taking it. */
#define STS_RUN_OWED_MAX 32

/* How a match went, from this player's side. */
typedef struct {
    int won;
    int rounds_won;
    int rounds_lost;
    int opponent;   /* the opponent's character, or -1 if not known */
} sts_match_result;

/* The most cards one reward can bring in or take out. */
#define STS_RUN_CHANGE_MAX 8

/* The most matches a run remembers, at least the longest match limit. An endless run past this
 * forgets its oldest. */
#define STS_RUN_HISTORY_MAX 100

/* One match of a run and what its reward changed. */
typedef struct {
    uint8_t won;
    uint8_t rounds_won;
    uint8_t rounds_lost;
    int8_t opponent;        /* -1 if not known */
    uint8_t pending;        /* its reward is still on screen or owed, so the change is not known yet */
    uint8_t added_count;
    uint8_t removed_count;
    uint16_t added[STS_RUN_CHANGE_MAX];
    uint16_t removed[STS_RUN_CHANGE_MAX];
} sts_match_record;

typedef struct {
    uint32_t settings_hash;
    int character;
    sts_run_phase phase;
    int picks_done;
    int matches_played;
    sts_rng rng;
    sts_deck deck;
    sts_offer offers[STS_OFFER_MAX];
    int offer_count;
    /* Oldest first: whether each owed match was won. Only non-empty in REWARD. */
    uint8_t owed_won[STS_RUN_OWED_MAX];
    int owed_count;
    /* Added in save format 3; a save from before it loads with these zero, has_rules 0 and an
     * empty history. */
    uint64_t seed;          /* what the run was started with */
    int restarts;           /* how many times this character's run was started over before this one */
    int offer_frames;       /* frames spent on the offers on screen; the mod counts, the core resets */
    int has_rules;
    sts_settings rules;     /* the rules the run was made under (only the rule settings are saved) */
    sts_match_record history[STS_RUN_HISTORY_MAX];   /* oldest first */
    int history_count;
    int history_forgotten;  /* matches dropped from the front, so history[0] is match history_forgotten + 1 */
} sts_run;

typedef enum {
    STS_RUN_OK = 0,
    STS_RUN_WRONG_PHASE,       /* that action is not available right now */
    STS_RUN_BAD_INDEX,         /* no such offer */
    STS_RUN_BAD_CHOICE,        /* the chosen removals are not possible; nothing changed */
    STS_RUN_STUCK,             /* no eligible card remains and the deck is below size_min */
    STS_RUN_BAD_ARGUMENT,
    STS_RUN_IO,                /* no save file, or it could not be read or written */
    STS_RUN_DAMAGED,           /* failed the checksum or could not be parsed */
    STS_RUN_BAD_VERSION,       /* written by a different save format version */
    STS_RUN_SETTINGS_CHANGED,  /* the save was made under different rules */
    STS_RUN_ILLEGAL,           /* intact, but its deck or offers break the rules */
    STS_RUN_BAD_STARTER        /* the starter has a card this character cannot use */
} sts_run_status;

const char *sts_run_status_str(sts_run_status status);

/* ---------- draft ---------- */

/* Starts a run in the draft phase with the first offers ready, the deck holding the starter
 * if it is enabled. Offers only reach the local player, so the seed may be local. */
sts_run_status sts_run_start(sts_run *run, const sts_card_db *db, const sts_settings *settings,
                             int character, uint64_t seed);

/* Returns NULL if every character can use every card of their starter, or a message naming the
 * first one who cannot. sts_settings_validate checks the starter's size and copies. */
const char *sts_run_check_starters(const sts_card_db *db, const sts_settings *settings);

/* The seed sts_run_start should get: random_seed, or one made from the settings' set seed if they
 * have one. sts_run_start gives each character its own sequence from it either way. */
uint64_t sts_run_seed(const sts_settings *settings, uint64_t random_seed);

/* Takes offer `index`. Rolls the next offers, or moves to READY when the picks are used
 * up or the deck is full. */
sts_run_status sts_run_draft_pick(sts_run *run, const sts_card_db *db, const sts_settings *settings, int index);

/* ---------- after a match ---------- */

typedef enum {
    STS_REWARD_NONE,    /* no change to the deck; still READY */
    STS_REWARD_AUTO,    /* nothing to choose (offer_pick and replacement random): the deck has changed; still READY */
    STS_REWARD_OFFERS,  /* offers are waiting; call sts_run_reward_pick */
    STS_REWARD_FINISHED /* that was the last match of the match limit: no offer, the run is FINISHED */
} sts_reward_outcome;

/* Call once when a match ends, from READY or REWARD. The match joins the history. From REWARD the
 * offers on screen stay, the match's reward is owed until they are taken, and *outcome is OFFERS.
 * The match that reaches the match limit gives no offer and finishes the run, when its reward comes
 * up (at once, or once the offers before it are taken). Returns STS_RUN_WRONG_PHASE from DRAFT or
 * FINISHED, after the match limit, or when STS_RUN_OWED_MAX rewards are already owed, leaving
 * *outcome as NONE and the history as it was. */
sts_run_status sts_run_begin_reward(sts_run *run, const sts_card_db *db, const sts_settings *settings,
                                    const sts_match_result *result, sts_reward_outcome *outcome);

/* What a REPLACE removes. Only the field matching settings.reward.replace is read. */
typedef struct {
    const uint16_t *remove_ids;  /* CHOOSE: exactly offer.copies card IDs */
    int remove_count;
    int type;                    /* BY_TYPE: an sts_card_type */
} sts_replace_choice;

/* Takes offer `index` and returns to READY, or starts the oldest owed reward, which may leave
 * the run in REWARD with new offers, or FINISHED if it was the last match. Owed rewards that give no offer are used up on the way (a
 * automatic change, with nothing to choose, is made then). On any error nothing changes and the
 * offers stay, so the screen can simply ask again. choice may be NULL unless the mode needs it. */
sts_run_status sts_run_reward_pick(sts_run *run, const sts_card_db *db, const sts_settings *settings,
                                   int index, const sts_replace_choice *choice);

/* The time limit ran out: takes offer `index`, or one at random for -1, and chooses what is left of
 * the reward at random from the run's random state (so a restart cannot change it). For REPLACE by
 * choice, the cards already chosen (chosen_ids, at most the offer's copies) leave, with random others
 * to make up the number; by type, `type` leaves, or a random type that can for -1. Otherwise as
 * sts_run_reward_pick. */
sts_run_status sts_run_reward_time_out(sts_run *run, const sts_card_db *db, const sts_settings *settings,
                                       int index, const uint16_t *chosen_ids, int chosen_count, int type);

/* ---------- save format ---------- */

/* Big enough for the largest deck, all offers, the rules and a full history. */
#define STS_RUN_TEXT_MAX 16384

/* Writes the run as ASCII text ending in a checksum. Returns bytes written, 0 if cap is
 * too small. */
size_t sts_run_serialize(const sts_run *run, char *buf, size_t cap);

/* Checks the file itself only. Use sts_run_validate before trusting the contents. */
sts_run_status sts_run_parse(const char *text, size_t len, sts_run *out);

/* Writes path.tmp first and then renames it over path, so a crash during a save leaves
 * the previous save or a complete new one, never a partial file. */
sts_run_status sts_run_save(const char *path, const sts_run *run);

/* Prefers a valid path.tmp, which is a save that crashed before its rename and so is
 * newer than path. A partial path.tmp fails its checksum and path is used instead. */
sts_run_status sts_run_load(const char *path, sts_run *out);

/* Checks a loaded run against the current rules: settings fingerprint, character, phase,
 * deck, offers and history. Returns STS_RUN_OK if it can be continued. */
sts_run_status sts_run_validate(const sts_run *run, const sts_card_db *db, const sts_settings *settings);

/* Moves a run made under other rules onto these. Returns STS_RUN_ILLEGAL, changing nothing, if
 * its deck breaks them. Offers on screen are kept if still possible, so editing the settings is
 * not a free reroll; otherwise they are rolled again, which may end a reward or a draft. A match
 * limit the run has already reached finishes it. A finished run stays finished. */
sts_run_status sts_run_adopt_rules(sts_run *run, const sts_card_db *db, const sts_settings *settings);

/* 1 if the run knows the rules it was made under and they still give its fingerprint, so putting
 * them back would let it carry on. A save from before format 3 cannot, and nor can a run whose
 * built-in starter cards have changed since. */
int sts_run_rules_restorable(const sts_run *run);

/* The deck as it was after history[index], or before history[0] for index -1 (the starting deck,
 * if no match has been forgotten), worked out backwards from the deck now. Rewards still pending
 * have not changed it yet. Returns 0 if the history does not fit the deck. */
int sts_run_deck_after(const sts_run *run, int index, sts_deck *out);

#endif
