/* Each player's deck changes between matches, online: sent by the players' mods and shown on a
 * spectator's screen as card images under that player's side, green outlines for cards that came in and
 * red for cards that left.
 *
 * A player with a run in use sends its deck change as soon as it is made (peers.h delivers it to every
 * machine with the mod), so a spectator can show it on its loading screen while the players are still
 * choosing. A spectator also receives both players' decks in each GAME_MATCH, with the match's number,
 * and works the changes out from those. That comparison is the fallback, shown at the start of the
 * match only for a player whose change did not arrive: it works whether the players have the mod or not.
 * Everything here only draws or sends the mod's own packets, so it cannot change the match.
 *
 * A spectator is sometimes sent back to the last match to catch up on it, so the notice is tied to
 * the match the spectator is watching (the number in its replay requests, which counts the same way as
 * GAME_MATCH), not to the battle scene starting. */
#include <stdio.h>
#include <string.h>
#include "internal.h"
#include "peers.h"
#include "spectator.h"

/* How long the start-of-match notice stays, from when the match it belongs to is first shown, and how
 * long of that it takes to fade. Real time, as it only draws. The loading screen's stays all along. */
#define NOTICE_MS 8000
#define NOTICE_FADE_MS 1000
/* Cards per side: each copy that came in or left is a card of its own, at half size, under that
 * player's half of the screen. What came in comes first and what left after it, on one row of 8, as a
 * reward swaps at most 4 copies. More (owed rewards, or a different deck slot) carry on to further rows. */
#define NOTICE_CARDS 16
#define NOTICE_ROW_CARDS 8
#define NOTICE_TOP 61
#define NOTICE_GAP 4
#define NOTICE_MARGIN 11

typedef struct {
    int character;
    sts_deck deck;
} side_deck;

/* One side's change, for the notice. */
typedef struct {
    int has;
    int character;
    int match;
    sts_deck added, removed;
} side_change;

/* Packets arrive on another thread from the one that draws. */
static CRITICAL_SECTION g_lock;

/* The last GAME_MATCH with both decks (a spectator's), and its match number (-1 before any). */
static side_deck g_last[2];
static int g_last_match = -1;

/* What each side's deck gained and lost by the last GAME_MATCH, worked out from the decks. */
static side_change g_compared[2];
/* The newest change each side's player sent. */
static side_change g_received[2];
/* Counts up whenever either of those changes, so the notice is rebuilt. */
static int g_content_serial = 0;

/* The match the spectator is asking inputs for, which is the one on screen. */
static int g_watching_match = -1;

/* A player's own deck in the last GAME_MATCH it sent, the starting point of its next change. Between the
 * players GAME_MATCH's match byte does not count up, so matches are counted here: one sent more than
 * GAME_MATCH_RESEND_MS after the last is a new match, not a resend. */
#define GAME_MATCH_RESEND_MS 2000
static int g_own_known = 0;
static int g_own_was_run_deck = 0;
static int g_own_side = 0;
static int g_own_match = 0;
static DWORD g_own_sent_at = 0;
static sts_deck g_own_deck;

void spectator_install(void)
{
    InitializeCriticalSection(&g_lock);
}

/* One side of GAME_MATCH: u8 character, u8 skin, u8 deck slot, u8 card count, u16 cards[count], u8
 * buttons. Returns the offset after it, or -1 if it does not fit. */
static int read_side(const unsigned char *packet, int len, int offset, side_deck *side)
{
    int count, wau;

    if (offset + 4 > len)
        return -1;
    side->character = packet[offset];
    count = packet[offset + 3];
    offset += 4;
    if (offset + count * 2 + 1 > len)
        return -1;
    sts_deck_clear(&side->deck);
    for (wau = 0; wau < count; wau++)
        sts_deck_add(&side->deck, (uint16_t)(packet[offset + wau * 2] | (packet[offset + wau * 2 + 1] << 8)));
    return offset + count * 2 + 1;
}

/* Both sides and the match number of a GAME_MATCH. Returns the match number, or -1. */
static int read_game_match(const unsigned char *packet, int len, side_deck sides[2])
{
    int offset = read_side(packet, len, 2, &sides[0]);

    if (offset >= 0)
        offset = read_side(packet, len, offset, &sides[1]);
    if (offset < 0 || offset + 7 > len)
        return -1;
    return packet[offset + 6];
}

static void log_ids(const char *what, const sts_deck *deck)
{
    char text[STS_MAX_DECK * 6 + 1];
    size_t used = 0;
    int wau;

    text[0] = 0;
    for (wau = 0; wau < deck->count; wau++)
        used += (size_t)sprintf(text + used, " %u", (unsigned)deck->ids[wau]);
    slog("   %s:%s", what, deck->count ? text : " none");
}

void spectator_on_match(const unsigned char *packet, int len)
{
    side_deck sides[2];
    int match, side;

    match = read_game_match(packet, len, sides);
    /* Between the players, each GAME_MATCH holds only the sender's deck. A spectator's holds both. */
    if (match < 0 || sides[0].deck.count == 0 || sides[1].deck.count == 0)
        return;

    EnterCriticalSection(&g_lock);
    /* GAME_MATCH is sent again until acknowledged, and passed on to other spectators. */
    if (match != g_last_match) {
        /* Only the match straight after the last one has changes to show. Anything else is a new set of
         * players, or a spectator that joined late. */
        int follows = g_last_match >= 0 && match == ((g_last_match + 1) & 0xFF);

        for (side = 0; side < 2; side++) {
            side_change *compared = &g_compared[side];

            compared->has = 0;
            compared->match = match;
            compared->character = sides[side].character;
            if (!follows || g_last[side].character != sides[side].character)
                continue;
            sts_deck_changes(&g_last[side].deck, &sides[side].deck, &compared->added, &compared->removed);
            compared->has = compared->added.count > 0 || compared->removed.count > 0;
            if (compared->has) {
                slog("spectator: match %d, %s (character %d) deck changed", match, side ? "right" : "left",
                     sides[side].character);
                log_ids("in", &compared->added);
                log_ids("out", &compared->removed);
            }
        }
        g_last[0] = sides[0];
        g_last[1] = sides[1];
        g_last_match = match;
        g_content_serial++;
    }
    LeaveCriticalSection(&g_lock);
}

void spectator_on_sent_match(const unsigned char *packet, int len)
{
    side_deck sides[2];
    DWORD now = GetTickCount();
    int side;

    if (SokuLib::mainMode != SokuLib::BATTLE_MODE_VSCLIENT && SokuLib::mainMode != SokuLib::BATTLE_MODE_VSSERVER)
        return;
    /* mainMode 4 (VSCLIENT in SokuLib's names) is the host. Its deck is the first side. */
    side = SokuLib::mainMode == SokuLib::BATTLE_MODE_VSCLIENT ? 0 : 1;
    if (read_game_match(packet, len, sides) < 0 || sides[side].deck.count == 0)
        return;
    EnterCriticalSection(&g_lock);
    if (!g_own_known || now - g_own_sent_at > GAME_MATCH_RESEND_MS) {
        g_own_known = 1;
        g_own_side = side;
        g_own_match++;
        g_own_deck = sides[side].deck;
        /* The character byte of a client's own side can be junk, so the deck itself says whether the
         * match was played with the run deck. */
        g_own_was_run_deck = deck_is_active(sides[side].deck.ids, sides[side].deck.count);
    }
    g_own_sent_at = now;
    LeaveCriticalSection(&g_lock);
}

void spectator_on_replay_request(int match)
{
    g_watching_match = match;
}

void changes_on_received(const peer_deck_change *change)
{
    side_change *received = &g_received[change->side];

    EnterCriticalSection(&g_lock);
    received->has = change->added.count > 0 || change->removed.count > 0;
    received->character = change->character;
    /* It goes with the next match. A spectator numbers matches by its own GAME_MATCH, which counts (and
     * arrives just before each battle, long before a reward). -1 before any: shown on the next one. */
    received->match = g_last_match >= 0 ? (g_last_match + 1) & 0xFF : -1;
    received->added = change->added;
    received->removed = change->removed;
    g_content_serial++;
    LeaveCriticalSection(&g_lock);
    log_ids("in", &change->added);
    log_ids("out", &change->removed);
}

/* Every frame online: once a reward has changed the run deck, the change since the last match goes to
 * the other machines with the mod. Taking an owed reward as well sends a newer change covering both. */
void spectator_player_frame(void)
{
    static sts_deck announced;
    static int announced_match = -1;
    static int serial = 0;
    sts_deck own_deck, added, removed, since;
    peer_deck_change change;
    int own_match, own_side, was_run_deck;

    if (SokuLib::mainMode != SokuLib::BATTLE_MODE_VSCLIENT && SokuLib::mainMode != SokuLib::BATTLE_MODE_VSSERVER)
        return;
    if (!g_active || g_player != 0)
        return;
    EnterCriticalSection(&g_lock);
    if (!g_own_known) {
        LeaveCriticalSection(&g_lock);
        return;
    }
    own_deck = g_own_deck;
    own_match = g_own_match;
    own_side = g_own_side;
    was_run_deck = g_own_was_run_deck;
    LeaveCriticalSection(&g_lock);
    if (!was_run_deck)
        return;
    /* What has been sent since that match was played, or the deck it was played with. */
    if (announced_match != own_match) {
        announced = own_deck;
        announced_match = own_match;
    }
    sts_deck_changes(&announced, &g_active_deck, &added, &since);
    if (!added.count && !since.count)
        return;
    announced = g_active_deck;

    sts_deck_changes(&own_deck, &g_active_deck, &added, &removed);
    if (!added.count && !removed.count)
        return;
    memset(&change, 0, sizeof change);
    change.side = own_side;
    change.character = g_active_character;
    serial = (serial + 1) & 0xFF;
    change.serial = serial;
    change.opponent_may_see = 0;
    change.added = added;
    change.removed = removed;
    slog("changes: sending change %d of this player's deck, made after match %d of this connection", change.serial,
         own_match);
    log_ids("in", &added);
    log_ids("out", &removed);
    peers_send_change(&change);
}

/* The notice, made into sprites when it changes. */
typedef struct {
    Sprite image;
    int loaded;
    int added;   /* 1 came in, 0 left */
    int row, column;
} notice_card;

static notice_card g_cards[2][NOTICE_CARDS];
static int g_card_count[2];
static RectangleShape g_outline;
static int g_built_serial = -1;
static int g_built_loading = -1;
static int g_built_match = -1;
/* When the notice was first on screen (GetTickCount), 0 before it was. */
static DWORD g_shown_since = 0;

/* Adds the cards after the ones already there, at half size. */
static void add_cards(int side, int character, const sts_deck *cards, int added)
{
    int wau;

    for (wau = 0; wau < cards->count && g_card_count[side] < NOTICE_CARDS; wau++) {
        int index = g_card_count[side]++;
        notice_card *card = &g_cards[side][index];

        card->added = added;
        card->row = index / NOTICE_ROW_CARDS;
        card->column = index % NOTICE_ROW_CARDS;
        card->loaded = load_card_sprite(&card->image, character, cards->ids[wau], 0, 0, 1);
        if (card->loaded) {
            SokuLib::Vector2u size = card->image.texture.getSize();

            card->image.setSize({size.x / 2, size.y / 2});
        }
    }
}

/* What to show. On the loading screen: the changes the players sent for the coming match. At the start
 * of a match: the changes worked out from the decks, for each player whose own change did not arrive. */
static void build_notice(int loading)
{
    int next = g_last_match >= 0 ? (g_last_match + 1) & 0xFF : -1, side;

    g_built_match = loading ? next : g_last_match;
    for (side = 0; side < 2; side++) {
        const side_change *received = &g_received[side];
        const side_change *shown = NULL;

        g_card_count[side] = 0;
        if (loading) {
            if (received->has && (next < 0 || received->match < 0 || received->match == next))
                shown = received;
        } else if (g_compared[side].has && g_compared[side].match == g_last_match
                   && !(received->has && received->match == g_last_match)) {
            shown = &g_compared[side];
        }
        if (!shown)
            continue;
        add_cards(side, shown->character, &shown->added, 1);
        add_cards(side, shown->character, &shown->removed, 0);
    }
}

/* A colour with its alpha scaled by strength, out of 255. */
static unsigned faded(unsigned color, int strength)
{
    unsigned alpha = (color >> 24) * (unsigned)strength / 255u;

    return (alpha << 24) | (color & 0x00FFFFFFu);
}

static void draw_cards(int strength)
{
    int side, wau;

    for (side = 0; side < 2; side++) {
        for (wau = 0; wau < g_card_count[side]; wau++) {
            notice_card *card = &g_cards[side][wau];
            SokuLib::Vector2u size;
            int column = card->column, row = card->row, pos_x, pos_y;
            unsigned border = card->added ? COLOR_GREEN : COLOR_RED;

            if (!card->loaded)
                continue;
            size = card->image.texture.getSize();
            size.x /= 2;
            size.y /= 2;
            /* The left player's cards run in from the left edge, the right player's from the right, each
             * outline in line with the end of its HP bar (the last pixel is 639, so the mirror of x is
             * 639 - x). */
            pos_x = side ? 639 - NOTICE_MARGIN - (column + 1) * ((int)size.x + NOTICE_GAP) + NOTICE_GAP
                         : NOTICE_MARGIN + column * ((int)size.x + NOTICE_GAP);
            pos_y = NOTICE_TOP + row * ((int)size.y + NOTICE_GAP);
            g_outline.setPosition({pos_x - 1, pos_y - 1});
            g_outline.setSize({size.x + 2, size.y + 2});
            g_outline.setFillColor(faded(border & 0x60FFFFFFu, strength));
            g_outline.setBorderColor(faded(border, strength));
            g_outline.draw();
            card->image.setPosition({pos_x, pos_y});
            card->image.tint = faded(COLOR_WHITE, strength);
            card->image.draw();
        }
    }
}

void spectator_render(int loading)
{
    DWORD now = GetTickCount(), elapsed;
    int strength;

    EnterCriticalSection(&g_lock);
    if (g_built_serial != g_content_serial || g_built_loading != loading) {
        int match_before = g_built_match;

        build_notice(loading);
        if (g_built_loading != loading || g_built_match != match_before)
            g_shown_since = 0;
        g_built_serial = g_content_serial;
        g_built_loading = loading;
    }
    LeaveCriticalSection(&g_lock);

    if (!g_card_count[0] && !g_card_count[1])
        return;
    if (loading) {
        draw_cards(255);
        return;
    }
    /* Only over the match it belongs to, not a catch-up of the one before. */
    if (g_built_match < 0 || g_watching_match != g_built_match)
        return;
    if (!g_shown_since)
        g_shown_since = now ? now : 1;
    elapsed = now - g_shown_since;
    if (elapsed >= NOTICE_MS)
        return;
    strength = elapsed + NOTICE_FADE_MS > NOTICE_MS ? (int)((NOTICE_MS - elapsed) * 255 / NOTICE_FADE_MS) : 255;
    draw_cards(strength);
}
