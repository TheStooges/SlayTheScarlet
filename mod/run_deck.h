/* The run deck in the game. */
#ifndef STS_RUN_DECK_H
#define STS_RUN_DECK_H

/* Is this the run deck in use, for that character? Cards in any order. */
int deck_matches_active(int character, const unsigned short *ids, int count);

#endif
