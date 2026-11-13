/* What the mod remembers between runs, in runs/state.txt: which run is in use, how many runs each
 * character has started over under each set of rules, and which cards have been offered in a run (the
 * gallery shows only those).
 *
 * The count is kept here rather than in a run's save because starting over or deleting a run
 * removes its save. It is counted per rules fingerprint, so an event's count is not mixed with other
 * play, and changing the rules and back does not reset it. It only makes starting over visible: the
 * file is the player's own, so it can be edited like any other.
 */
#ifndef STS_STATE_H
#define STS_STATE_H

#include <stddef.h>
#include <stdint.h>
#include "pf.h"

/* The most counts kept. Past this the oldest is forgotten. */
#define STS_STATE_COUNTS_MAX 128

typedef struct {
    int character;
    uint32_t rules_hash;
    int restarts;
} sts_restart_count;

/* Cards found: one set for each character and one for the system cards, of card IDs below this. */
#define STS_STATE_CARD_IDS 256
#define STS_STATE_FOUND_SYSTEM STS_NUM_CHARACTERS
#define STS_STATE_FOUND_SETS (STS_NUM_CHARACTERS + 1)

typedef struct {
    int active;   /* the character whose run is in use, or -1 */
    sts_restart_count counts[STS_STATE_COUNTS_MAX];   /* oldest first */
    int count;
    uint8_t found[STS_STATE_FOUND_SETS][STS_STATE_CARD_IDS / 8];   /* a bit for each card ID */
    int gallery_unlocked;   /* 1 once the player chose to show every card, found or not */
} sts_state;

void sts_state_init(sts_state *state);

/* How many runs of this character were started over under these rules. */
int sts_state_restarts(const sts_state *state, int character, uint32_t rules_hash);

/* One more run of this character, made under these rules, was started over or deleted. */
void sts_state_add_restart(sts_state *state, int character, uint32_t rules_hash);

/* Cards found in a run. Owner is a character, or STS_STATE_FOUND_SYSTEM for the system cards.
 * sts_state_add_found returns 1 if the card was not found before. IDs from STS_STATE_CARD_IDS up are
 * never found. */
int sts_state_found(const sts_state *state, int owner, unsigned id);
int sts_state_add_found(sts_state *state, int owner, unsigned id);

/* Big enough for every count and every card found. */
#define STS_STATE_TEXT_MAX 32768

/* Writes the state as ASCII text. Returns bytes written, 0 if cap is too small. */
size_t sts_state_serialize(const sts_state *state, char *buf, size_t cap);

/* Reads text written by sts_state_serialize. Returns 1, or 0 (with *out as from sts_state_init) if
 * it cannot be read. The cards found and the gallery unlock come after everything else, so a file
 * without them (from before they were kept) reads with none found and the gallery locked, and older
 * builds read the rest and ignore them. */
int sts_state_parse(const char *text, size_t len, sts_state *out);

#endif
