/* Every rule of a run, loaded from the mod's .ini. The community has not settled the
 * rules, so nothing here is hardcoded elsewhere; these defaults are provisional until
 * playtesting. See data/settings.ini for the file format. */
#ifndef STS_SETTINGS_H
#define STS_SETTINGS_H

#include <stddef.h>
#include <stdint.h>
#include "offer.h"
#include "rules.h"

/* The game drops a 5th copy and then silently starts with no deck, and raising the
 * limit would need its own patch, so settings may not go above this for now. */
#define STS_GAME_COPY_LIMIT 4

/* Who gets a card offer after a match. A player left out keeps their deck unchanged. */
typedef enum {
    STS_WHO_BOTH,
    STS_WHO_WINNER,
    STS_WHO_LOSER,
    STS_WHO_NOBODY      /* nobody chooses: an offer and any removals are rolled at random */
} sts_who_picks;

typedef enum {
    STS_VISIBILITY_HIDDEN,
    STS_VISIBILITY_AFTER_CONFIRM,
    STS_VISIBILITY_LIVE
} sts_pick_visibility;

typedef enum {
    STS_OPPONENT_DECK_CHANGE,
    STS_OPPONENT_DECK_FULL
} sts_opponent_deck;

typedef struct {
    sts_rules rules;
    int draft_picks;
    sts_offer_settings draft;    /* mode is always ADD */
    sts_offer_settings reward;
    sts_who_picks who_picks;
    sts_pick_visibility pick_visibility;
    sts_opponent_deck opponent_deck;
    int items_enabled;
} sts_settings;

typedef enum {
    STS_SETTINGS_OK = 0,
    STS_SETTINGS_IO,               /* file could not be read */
    STS_SETTINGS_BAD_LINE,         /* neither [section], key=value, comment nor blank */
    STS_SETTINGS_UNKNOWN_SECTION,
    STS_SETTINGS_UNKNOWN_KEY,      /* rejected so that a typo cannot silently do nothing */
    STS_SETTINGS_BAD_VALUE
} sts_settings_status;

void sts_settings_defaults(sts_settings *s);

/* Starts from the defaults, so a file only has to list what it changes. Stops at the
 * first error and sets *err_line (if non-NULL) to its 1-based line number. '#' and ';'
 * start comment lines. Does not check that the values make sense together; use
 * sts_settings_validate for that. */
sts_settings_status sts_settings_parse(const char *text, size_t len, sts_settings *out, int *err_line);

sts_settings_status sts_settings_load(const char *path, sts_settings *out, int *err_line);

const char *sts_settings_status_str(sts_settings_status s);

/* Returns NULL if the settings can work, or a message saying why they cannot. */
const char *sts_settings_validate(const sts_settings *s);

/* Compatible mode (opponent is vanilla): 20 cards, 4 copies, no items, and no rule
 * that needs another deck size. The result always passes sts_settings_validate. */
void sts_settings_force_compatible(sts_settings *s);

/* Fingerprint of every rule, stable across machines. Differs if any setting differs. */
uint32_t sts_settings_hash(const sts_settings *s);

#endif
