/* Keeping the run deck in place from one scene to the next, and working out whether a
 * finished match earned a reward. */
#include <stdio.h>
#include <string.h>
#include "internal.h"

/* What the last battle scene showed, kept for when the scene has gone. */
static int g_match_used_run_deck = 0;
static int g_match_run_on_right = 0;
static int g_match_score_left = 0;
static int g_match_score_right = 0;
int g_match_reward_due = 0;
int g_match_won = 0;
int g_match_no_offer = 0;

/* Called when a scene after the battle starts: was that a finished match with the run deck? */
static void finish_match(void)
{
    int left = g_match_score_left, right = g_match_score_right;

    if (!g_match_used_run_deck)
        return;
    g_match_used_run_deck = 0;
    slog("match over: rounds won %d to %d", left, right);
    if (left == right || (left < 2 && right < 2)) {
        slog("match was not finished, no reward");
        return;
    }
    g_match_won = g_match_run_on_right ? right > left : left > right;
    g_match_reward_due = 1;
}

void deck_on_result_frame(void)
{
    finish_match();
    maybe_start_reward();
}

/* Only changes: the menu returns the same value on every frame of its exit fade. */
int deck_take_result_skip(void)
{
    int skip = g_match_no_offer;

    g_match_no_offer = 0;
    return skip;
}

void deck_log_result_close(int result)
{
    static int last = -1;

    if (result != last)
        slog("result menu closing, it returned %d", result);
    last = result;
}

void deck_on_title_frame(void)
{
    resume_active_run();
    finish_match();
    maybe_start_reward();
    note_slot_change("title");
}

/* Online, only the deck is kept up. Pausing an online scene for a late reward could stall the
 * connection, so a reward that did not reach the screen after the match waits for the title. */
void deck_on_net_select_frame(void)
{
    char why[96] = "";

    if (!g_active)
        return;
    note_slot_change("online character select");
    if (g_slot_differs) {
        remember_real_deck();
        if (write_active_deck(why, sizeof why)) {
            slog("wrote the run deck again");
            g_slot_differs = 0;
        } else {
            slog("could not write the run deck again: %s", why);
        }
    }
}

/* Character select is the point a write is known to reach the next match. */
void deck_on_select_frame(void)
{
    static DWORD last_tick = 0;
    DWORD now = GetTickCount();
    char why[96] = "";

    if (now - last_tick > 1000)
        slog("character select entered, run deck %s", g_active ? "active" : "not active");
    last_tick = now;

    finish_match();
    maybe_start_reward();

    if (!g_active)
        return;
    note_slot_change("character select");
    if (g_slot_differs) {
        /* The game put the player's deck back, possibly with edits from the deck editor. */
        remember_real_deck();
        if (write_active_deck(why, sizeof why)) {
            slog("wrote the run deck again");
            g_slot_differs = 0;
        } else {
            slog("could not write the run deck again: %s", why);
        }
    }
}

/* Returns 1 if this side is playing the active run deck. */
static int log_battle_side(const char *name, int character, int deck_slot, SokuLib::CharacterManager &player)
{
    /* Player::deckInfo.original; see the probe findings in CLAUDE.md */
    auto *original = reinterpret_cast<SokuLib::Dequeue<unsigned short> *>(
        reinterpret_cast<char *>(&player) + 0x57C + 0x20);
    static sts_deck loaded, expected;
    char text[1100];
    int wau;

    ids_text(text, sizeof text, *original);
    slog("match %s: character %d, deck choice %d, loaded %d cards: %s",
         name, character, deck_slot, original->size, text);
    if (!g_active || character != g_active_character)
        return 0;

    sts_deck_clear(&loaded);
    for (wau = 0; wau < original->size && wau < STS_MAX_DECK; wau++)
        sts_deck_add(&loaded, (*original)[wau]);
    sts_deck_sort(&loaded);
    expected = g_active_deck;
    sts_deck_sort(&expected);
    if (loaded.count == expected.count && memcmp(loaded.ids, expected.ids, (size_t)loaded.count * 2) == 0) {
        slog("match %s uses the RUN deck", name);
        return 1;
    }
    slog("match %s does NOT use the run deck", name);
    return 0;
}

void deck_on_battle_frame(void)
{
    static DWORD last_tick = 0;
    DWORD now = GetTickCount();

    /* Diagnostics only, and they read game memory directly: a bad read must not end the match. */
    if (now - last_tick > 1000) {
        __try {
            auto &mgr = SokuLib::getBattleMgr();
            int offline = SokuLib::mainMode == SokuLib::BATTLE_MODE_VSCOM
                || SokuLib::mainMode == SokuLib::BATTLE_MODE_VSPLAYER;
            /* SokuLib's names are from the opponent's view: "VSCLIENT" means you are the host. */
            int host = SokuLib::mainMode == SokuLib::BATTLE_MODE_VSCLIENT;
            int client = SokuLib::mainMode == SokuLib::BATTLE_MODE_VSSERVER;
            int left = log_battle_side("left", (int)SokuLib::leftPlayerInfo.character, SokuLib::leftPlayerInfo.deck,
                                       mgr.leftCharacterManager);
            int right = log_battle_side("right", (int)SokuLib::rightPlayerInfo.character, SokuLib::rightPlayerInfo.deck,
                                        mgr.rightCharacterManager);

            /* Only VS matches played by a person earn a reward: not practice, replays or
             * spectating. Offline the run deck only reaches the left player; online, the local
             * player's own side. */
            g_match_run_on_right = client;
            g_match_used_run_deck = SokuLib::subMode != SokuLib::BATTLE_SUBMODE_REPLAY
                && (((offline || host) && left) || (client && right));
            g_match_score_left = 0;
            g_match_score_right = 0;
            slog("match mode %d submode %d, counts for a reward: %d",
                 (int)SokuLib::mainMode, (int)SokuLib::subMode, g_match_used_run_deck);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            slog("match: could not read the decks");
        }
    }
    last_tick = now;

    __try {
        auto &mgr = SokuLib::getBattleMgr();
        g_match_score_left = mgr.leftCharacterManager.score;
        g_match_score_right = mgr.rightCharacterManager.score;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}
