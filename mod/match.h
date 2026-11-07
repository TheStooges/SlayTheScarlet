/* What the scene hooks in main.cpp call each frame, to keep the run deck in place and to notice
 * finished matches. */
#ifndef STS_MATCH_H
#define STS_MATCH_H

/* The title frame only records changes to the slot: the deck editor must keep seeing the player's
 * real deck. The result, title and select frames also open the reward screen after a finished
 * match, whichever comes first; normally that is the result menu. */
void deck_on_title_frame(void);
void deck_on_select_frame(void);
void deck_on_net_select_frame(void);
void deck_on_battle_frame(void);
void deck_on_result_frame(void);

/* True once after a run match that earned no offer: the game's list of card unlocks should
 * not be shown during a run, so the result menu should close without appearing. */
int deck_take_result_skip(void);

/* Records what the Result menu returned while being told to close. */
void deck_log_result_close(int result);

#endif
