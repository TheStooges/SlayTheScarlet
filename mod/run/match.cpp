/* Keeping the run deck in place from one scene to the next, and working out whether a
 * finished match earned a reward. */
#include <stdio.h>
#include <string.h>
#include "internal.h"

/* What the last battle scene showed, kept for when the scene has gone, for each player: whether their
 * run deck was played, on which side, and against whom. */
static int g_match_used_run_deck[RUN_PLAYERS];
static int g_match_run_on_right[RUN_PLAYERS];
static int g_match_opponent[RUN_PLAYERS] = {-1, -1};
static int g_match_score_left = 0;
static int g_match_score_right = 0;
int g_match_reward_due[RUN_PLAYERS];
int g_match_new[RUN_PLAYERS];
sts_match_result g_match_result[RUN_PLAYERS] = {{0, 0, 0, -1}, {0, 0, 0, -1}};
int g_match_no_offer = 0;

/* Called when a scene after the battle starts: was that a finished match with a run deck? Each side
 * that played its player's run deck gets its own result and reward. */
static void finish_match(void)
{
    int left = g_match_score_left, right = g_match_score_right, player;

    for (player = 0; player < RUN_PLAYERS; player++) {
        sts_match_result *result = &g_match_result[player];

        if (!g_match_used_run_deck[player])
            continue;
        g_match_used_run_deck[player] = 0;
        slog("match over for player %d: rounds won %d to %d", player + 1, left, right);
        if (left == right || (left < 2 && right < 2)) {
            slog("match was not finished, no reward");
            continue;
        }
        result->rounds_won = g_match_run_on_right[player] ? right : left;
        result->rounds_lost = g_match_run_on_right[player] ? left : right;
        result->won = result->rounds_won > result->rounds_lost;
        result->opponent = g_match_opponent[player];
        g_match_reward_due[player] = 1;
        g_match_new[player] = 1;
    }
}

/* An online battle went straight on to character select. If the screen after the match was skipped
 * (Esc on the win quote), the match has not been counted yet: it is now, and player 1's reward is
 * started without a screen, so it waits with the run for the next one. After a screen that ran
 * normally this finds nothing to do. */
void deck_on_skipped_result(void)
{
    int selected = g_player;

    /* A no-offer match whose screen was skipped must not close the next match's screen. */
    g_match_no_offer = 0;
    finish_match();
    if (!g_match_reward_due[0])
        return;
    select_player(0);
    record_skipped_reward();
    select_player(selected);
}

/* The battle manager's match state during the win quote, after the last round. */
#define MATCH_STATE_WIN_QUOTE 6
/* How many frames the game has seen each watched key held, one int per DirectInput scan code, updated
 * from the raw keyboard buffer (0x8A01B8) before the scene runs (0x40A950 on the object at 0x8998D8).
 * The game's key check (0x43DE30, through 0x40AA60) takes a key as pressed when its count is exactly 1;
 * the win quote's Esc is checked that way (scan code 1). Clearing the raw buffer comes too late. */
#define KEY_HELD_FRAMES 0x8998D8u
#define SCAN_ESCAPE 1

int deck_on_win_quote_frame(void)
{
    static int in_quote = 0, moving_on = 0;
    int state, selected = g_player;
    int *held_frames = (int *)KEY_HELD_FRAMES;

    __try {
        state = SokuLib::getBattleMgr().matchState;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        state = -1;
    }
    if (state != MATCH_STATE_WIN_QUOTE) {
        in_quote = 0;
        moving_on = 0;
        return 0;
    }
    if (SokuLib::mainMode != SokuLib::BATTLE_MODE_VSCLIENT && SokuLib::mainMode != SokuLib::BATTLE_MODE_VSSERVER)
        return 0;
    if (!in_quote) {
        in_quote = 1;
        /* The match is decided: its reward starts now, so closing the game here keeps it. */
        finish_match();
        if (g_match_reward_due[0]) {
            select_player(0);
            begin_reward_early();
            select_player(selected);
        }
    }
    /* With a reward to show, Esc would skip the screen it is on: the game goes there instead. */
    if (reward_waits_for_screen() && held_frames[SCAN_ESCAPE] == 1) {
        /* Held, not pressed: the game counts on from 2 while it stays down, so it never sees a press. */
        held_frames[SCAN_ESCAPE] = 2;
        if (!moving_on)
            slog("win quote: Esc caught, going on to the screen after the match");
        moving_on = 1;
    }
    return moving_on;
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
    int selected = g_player, player;

    players_on_title_frame();
    finish_match();
    maybe_start_reward();
    for (player = 0; player < RUN_PLAYERS; player++) {
        select_player(player);
        note_slot_change("title");
    }
    select_player(selected);
}

/* Puts the selected player's run deck back in its slot if the game replaced it (with the player's deck,
 * possibly edited in the deck editor). */
static void keep_run_deck_in_place(const char *where)
{
    char why[96] = "";

    if (!g_active)
        return;
    note_slot_change(where);
    if (!g_slot_differs)
        return;
    remember_real_deck();
    if (write_active_deck(why, sizeof why)) {
        slog("wrote player %d's run deck again", g_player + 1);
        g_slot_differs = 0;
    } else {
        slog("could not write player %d's run deck again: %s", g_player + 1, why);
    }
}

/* Online, only player 1's deck is kept up: player 2 never plays online. Pausing an online scene for a
 * late reward could stall the connection, so a reward that did not reach the screen after the match
 * waits for the title. */
void deck_on_net_select_frame(void)
{
    int selected = g_player;

    select_player(0);
    keep_run_deck_in_place("online character select");
    select_player(selected);
}

/* Character select is the point a write is known to reach the next match. */
void deck_on_select_frame(void)
{
    static DWORD last_tick = 0;
    DWORD now = GetTickCount();
    int selected = g_player, player;

    if (now - last_tick > 1000)
        slog("character select entered, run deck %s", g_active ? "active" : "not active");
    last_tick = now;

    finish_match();
    maybe_start_reward();

    /* Player 2's run deck only plays in VS Player, against player 1. */
    for (player = 0; player < RUN_PLAYERS; player++) {
        if (player == 1 && (SokuLib::mainMode != SokuLib::BATTLE_MODE_VSPLAYER || !player_available(1)))
            continue;
        select_player(player);
        keep_run_deck_in_place("character select");
    }
    select_player(selected);
}

/* Returns 1 if this side is playing the selected player's run deck. */
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
            int two_players = SokuLib::mainMode == SokuLib::BATTLE_MODE_VSPLAYER && player_available(1);
            /* SokuLib's names are from the opponent's view: "VSCLIENT" means you are the host. */
            int host = SokuLib::mainMode == SokuLib::BATTLE_MODE_VSCLIENT;
            int client = SokuLib::mainMode == SokuLib::BATTLE_MODE_VSSERVER;
            int replay = SokuLib::subMode == SokuLib::BATTLE_SUBMODE_REPLAY;
            int selected = g_player, first_left, first_right, second_right = 0;

            /* Player 1's run deck: on the left offline and as host, on the right as client. */
            select_player(0);
            first_left = log_battle_side("left", (int)SokuLib::leftPlayerInfo.character, SokuLib::leftPlayerInfo.deck,
                                         mgr.leftCharacterManager);
            first_right = log_battle_side("right", (int)SokuLib::rightPlayerInfo.character, SokuLib::rightPlayerInfo.deck,
                                          mgr.rightCharacterManager);
            /* Player 2's: on the right, in VS Player. */
            if (two_players) {
                select_player(1);
                second_right = log_battle_side("right", (int)SokuLib::rightPlayerInfo.character,
                                               SokuLib::rightPlayerInfo.deck, mgr.rightCharacterManager);
            }
            select_player(selected);

            /* Only VS matches played by a person earn a reward: not practice, replays or spectating. */
            g_match_run_on_right[0] = client;
            g_match_opponent[0] = (int)(client ? SokuLib::leftPlayerInfo.character : SokuLib::rightPlayerInfo.character);
            g_match_used_run_deck[0] = !replay && (((offline || host) && first_left) || (client && first_right));
            g_match_run_on_right[1] = 1;
            g_match_opponent[1] = (int)SokuLib::leftPlayerInfo.character;
            g_match_used_run_deck[1] = !replay && two_players && second_right;
            g_match_score_left = 0;
            g_match_score_right = 0;
            slog("match mode %d submode %d, counts for a reward: player 1 %d, player 2 %d", (int)SokuLib::mainMode,
                 (int)SokuLib::subMode, g_match_used_run_deck[0], g_match_used_run_deck[1]);
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
