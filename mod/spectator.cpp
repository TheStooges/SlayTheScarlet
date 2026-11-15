/* What a spectator sees of the runs: at the start of each match, the cards each player's deck gained
 * and lost since the last one, as card images under their side of the screen: green outlines for
 * cards that came in, red for cards that left.
 *
 * Nothing is sent and no packet changes. A spectator already receives both players' decks in each
 * GAME_MATCH, with the match's number, so it works the changes out itself, and it works whether the
 * players have the mod or not. It also only draws, so it cannot change the match.
 *
 * A spectator is sometimes sent back to the last match to catch up on it, so the notice is tied to
 * the match the spectator is watching (the number in its replay requests, which counts the same way as
 * GAME_MATCH), not to the battle scene starting. */
#include <stdio.h>
#include <string.h>
#include "internal.h"
#include "spectator.h"

/* How long the notice stays, from when the match it belongs to is first shown, and how long of that
 * it takes to fade. Real time, as it only draws. */
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

/* Packets arrive on another thread from the one that draws. */
static CRITICAL_SECTION g_lock;

/* The last GAME_MATCH seen, and its match number (-1 before any). */
static side_deck g_last[2];
static int g_last_match = -1;

/* The notice for one match: what each side's deck gained and lost. */
static int g_notice_match = -1;
static int g_notice_serial = 0;
static int g_notice_has[2];
static int g_notice_character[2];
static sts_deck g_notice_added[2], g_notice_removed[2];

/* The match the spectator is asking inputs for, which is the one on screen. */
static int g_watching_match = -1;

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
    int offset = 2, match, side;

    offset = read_side(packet, len, offset, &sides[0]);
    if (offset >= 0)
        offset = read_side(packet, len, offset, &sides[1]);
    if (offset < 0 || offset + 7 > len)
        return;
    /* Between the players, each GAME_MATCH holds only the sender's deck. A spectator's holds both. */
    if (sides[0].deck.count == 0 || sides[1].deck.count == 0)
        return;
    match = packet[offset + 6];

    EnterCriticalSection(&g_lock);
    /* GAME_MATCH is sent again until acknowledged, and passed on to other spectators. */
    if (match != g_last_match) {
        /* Only the match straight after the last one has changes to show. Anything else is a new set of
         * players, or a spectator that joined late. */
        int follows = g_last_match >= 0 && match == ((g_last_match + 1) & 0xFF);

        g_notice_match = match;
        g_notice_serial++;
        for (side = 0; side < 2; side++) {
            g_notice_has[side] = 0;
            g_notice_character[side] = sides[side].character;
            if (!follows || g_last[side].character != sides[side].character)
                continue;
            sts_deck_changes(&g_last[side].deck, &sides[side].deck, &g_notice_added[side], &g_notice_removed[side]);
            g_notice_has[side] = g_notice_added[side].count > 0 || g_notice_removed[side].count > 0;
            if (g_notice_has[side]) {
                slog("spectator: match %d, %s (character %d) deck changed", match, side ? "right" : "left",
                     sides[side].character);
                log_ids("in", &g_notice_added[side]);
                log_ids("out", &g_notice_removed[side]);
            }
        }
        g_last[0] = sides[0];
        g_last[1] = sides[1];
        g_last_match = match;
    }
    LeaveCriticalSection(&g_lock);
}

void spectator_on_replay_request(int match)
{
    g_watching_match = match;
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
static int g_built_serial = 0;
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

static void build_notice(void)
{
    int side;

    for (side = 0; side < 2; side++) {
        g_card_count[side] = 0;
        if (!g_notice_has[side])
            continue;
        add_cards(side, g_notice_character[side], &g_notice_added[side], 1);
        add_cards(side, g_notice_character[side], &g_notice_removed[side], 0);
    }
}

/* A colour with its alpha scaled by strength, out of 255. */
static unsigned faded(unsigned color, int strength)
{
    unsigned alpha = (color >> 24) * (unsigned)strength / 255u;

    return (alpha << 24) | (color & 0x00FFFFFFu);
}

void spectator_render(void)
{
    DWORD now = GetTickCount(), elapsed;
    int strength, side, wau;

    EnterCriticalSection(&g_lock);
    if (g_built_serial != g_notice_serial) {
        build_notice();
        g_built_serial = g_notice_serial;
        g_built_match = g_notice_match;
        g_shown_since = 0;
    }
    LeaveCriticalSection(&g_lock);

    /* Only over the match it belongs to, not a catch-up of the one before. */
    if ((!g_card_count[0] && !g_card_count[1]) || g_built_match < 0 || g_watching_match != g_built_match)
        return;
    if (!g_shown_since)
        g_shown_since = now ? now : 1;
    elapsed = now - g_shown_since;
    if (elapsed >= NOTICE_MS)
        return;
    strength = elapsed + NOTICE_FADE_MS > NOTICE_MS ? (int)((NOTICE_MS - elapsed) * 255 / NOTICE_FADE_MS) : 255;

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
