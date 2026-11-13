/* The gallery: every card in the game, chosen by type and then by character, laid out as on the
 * deck screen. The card under the cursor plays the sounds it makes in a match (card_sounds.cpp), so
 * the sound table can be heard and checked card by card. Only cards that have been offered in a run are
 * shown (card_found); the rest are face down and play nothing, unless the player chose Unlock the
 * gallery (a second press confirms it, and the screens say so afterwards). It only shows cards: runs
 * are untouched.
 *
 * Kinds (system, skill, spell) -> characters (skill and spell cards only) -> cards. B goes back one
 * step, and from the kinds to the main menu. */
#include <stdlib.h>
#include "internal.h"

/* How long the cursor rests on a card before its sounds play, so moving across the grid does not
 * start a sound on every card passed: the same as on the offer screens. */
#define SOUND_SETTLE_FRAMES CARD_SOUNDS_SETTLE_FRAMES

int g_gallery_kind = STS_CARD_SYSTEM;
int g_gallery_character = -1;
sts_card g_gallery_cards[STS_POOL_MAX_CARDS];
int g_gallery_count = 0;
int g_gallery_art_dirty = 1;

/* Where the cursor was on the kinds and the characters, for coming back to them. */
static int g_kind_cursor = 0;
static int g_character_cursor = 0;

static const sts_card *card_under_cursor(void)
{
    return g_cursor >= 0 && g_cursor < g_gallery_count ? &g_gallery_cards[g_cursor] : NULL;
}

int g_gallery_unlock_armed = 0;

int gallery_all_found(void)
{
    int character, wau;

    for (wau = 0; wau < g_db.system.count; wau++)
        if (!card_found(-1, g_db.system.cards[wau].id))
            return 0;
    for (character = 0; character < STS_NUM_CHARACTERS; character++)
        for (wau = 0; wau < g_db.characters[character].count; wau++)
            if (!card_found(character, g_db.characters[character].cards[wau].id))
                return 0;
    return 1;
}

int gallery_unlock_offered(void)
{
    return !gallery_unlocked() && !gallery_all_found();
}

int gallery_card_shown(int index)
{
    return index >= 0 && index < g_gallery_count
        && (gallery_unlocked() || card_found(g_gallery_character, g_gallery_cards[index].id));
}

static void play_card_under_cursor(int delay)
{
    const sts_card *card = card_under_cursor();

    if (card && gallery_card_shown(g_cursor) && g_settings.card_sounds)
        card_sounds_start(g_gallery_kind == STS_CARD_SYSTEM ? STS_SOUND_SYSTEM : g_gallery_character,
                          card->id, card->type, delay, 1);
    else
        card_sounds_stop();
}

/* Skills in the order players name their inputs: 236, 623, 214, 421, then 22. A skill's
 * cost_or_slot is its input, 0 to 4 as 236, 214, 623, 421, 22. */
static int input_order(int slot)
{
    static const int order[5] = { 0, 2, 1, 3, 4 };

    return slot >= 0 && slot < 5 ? order[slot] : slot;
}

/* Skills by input, then the default before its alternates; spells by cost; system cards as the card
 * table lists them. Card ID settles the rest. */
static int compare_gallery_cards(const void *first_card, const void *second_card)
{
    const sts_card *first = (const sts_card *)first_card, *second = (const sts_card *)second_card;
    int first_key = 0, second_key = 0;

    if (first->type == STS_CARD_SKILL) {
        first_key = input_order(first->cost_or_slot);
        second_key = input_order(second->cost_or_slot);
    } else if (first->type == STS_CARD_SPELL) {
        first_key = first->cost_or_slot;
        second_key = second->cost_or_slot;
    }
    if (first_key != second_key)
        return first_key - second_key;
    return (int)first->id - (int)second->id;
}

/* The cards of the chosen kind, in the gallery's order. */
static void open_cards(int character)
{
    const sts_card_list *list = character < 0 ? &g_db.system : &g_db.characters[character];
    int wau;

    g_gallery_character = character;
    g_gallery_count = 0;
    for (wau = 0; wau < list->count; wau++)
        if (list->cards[wau].type == g_gallery_kind)
            g_gallery_cards[g_gallery_count++] = list->cards[wau];
    qsort(g_gallery_cards, (size_t)g_gallery_count, sizeof g_gallery_cards[0], compare_gallery_cards);
    /* Card names come from the game's files for one character at a time, the system cards always
     * among them. */
    load_card_text(character < 0 ? 0 : character);
    g_state = ST_GALLERY_CARDS;
    g_cursor = 0;
    g_gallery_art_dirty = 1;
    g_dirty = 1;
    play_card_under_cursor(SOUND_SETTLE_FRAMES);
}

static void open_kinds(void)
{
    card_sounds_release();
    g_state = ST_GALLERY_KINDS;
    g_cursor = g_kind_cursor;
    g_gallery_unlock_armed = 0;
    g_dirty = 1;
}

void open_gallery(void)
{
    /* Only the card table is needed, so a problem in settings.ini does not keep it shut. Without the
     * table, load_data has already put the reason on screen. */
    if (g_db.system.count == 0) {
        g_dirty = 1;
        return;
    }
    SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CONFIRM);
    g_error[0] = 0;
    /* Runs saved before cards were recorded count as well. */
    note_found_from_saves();
    open_kinds();
}

static void on_kinds_event(screen_event event)
{
    /* Unlock the gallery is a row of its own until it has been used, or every card has been found. */
    int rows = GALLERY_KINDS + (gallery_unlock_offered() ? 1 : 0);

    switch (event) {
    case EV_UP:
    case EV_DOWN:
        g_cursor = (g_cursor + (event == EV_UP ? rows - 1 : 1)) % rows;
        g_gallery_unlock_armed = 0;
        SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_MOVE);
        g_dirty = 1;
        break;
    case EV_CONFIRM:
        if (g_cursor == GALLERY_UNLOCK_ROW) {
            /* The first press only asks, with a sound of its own (048), as it cannot be undone. */
            if (!g_gallery_unlock_armed) {
                play_common_sound(48);
                g_gallery_unlock_armed = 1;
            } else {
                SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CONFIRM);
                unlock_gallery();
                g_gallery_unlock_armed = 0;
                g_cursor = 0;
            }
            g_dirty = 1;
            break;
        }
        SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CONFIRM);
        g_kind_cursor = g_cursor;
        /* The cursor's rows are in type order: system, skill, spell. */
        g_gallery_kind = g_cursor;
        if (g_gallery_kind == STS_CARD_SYSTEM) {
            open_cards(-1);
        } else {
            g_state = ST_GALLERY_CHARACTERS;
            g_cursor = g_character_cursor;
            g_dirty = 1;
        }
        break;
    case EV_CANCEL:
        SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CANCEL);
        card_sounds_release();
        go_to_chooser_at(CHOOSE_GALLERY);
        break;
    default:
        break;
    }
}

/* The characters, in the same grid as the main menu. Every direction wraps round. */
static void on_characters_event(screen_event event)
{
    int rows = STS_NUM_CHARACTERS / COLS;
    int column = g_cursor % COLS, row = g_cursor / COLS;

    switch (event) {
    case EV_LEFT:  column = (column + COLS - 1) % COLS; break;
    case EV_RIGHT: column = (column + 1) % COLS; break;
    case EV_UP:    row = (row + rows - 1) % rows; break;
    case EV_DOWN:  row = (row + 1) % rows; break;
    case EV_CONFIRM:
        SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CONFIRM);
        g_character_cursor = g_cursor;
        open_cards(g_cursor);
        return;
    case EV_CANCEL:
        SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CANCEL);
        g_character_cursor = g_cursor;
        open_kinds();
        return;
    default:
        return;
    }
    g_cursor = row * COLS + column;
    SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_MOVE);
    g_dirty = 1;
}

static void on_cards_event(screen_event event)
{
    switch (event) {
    case EV_LEFT:
    case EV_RIGHT:
    case EV_UP:
    case EV_DOWN:
        move_card_grid(event, g_gallery_count);
        play_card_under_cursor(SOUND_SETTLE_FRAMES);
        break;
    case EV_CONFIRM:
        /* Again, at once. A card whose sounds vary may come out another way, which the screen names. */
        play_card_under_cursor(0);
        g_dirty = 1;
        break;
    case EV_CANCEL:
        SokuLib::playSEWaveBuffer(SokuLib::SFX_MENU_CANCEL);
        card_sounds_stop();
        if (g_gallery_kind == STS_CARD_SYSTEM) {
            open_kinds();
        } else {
            g_state = ST_GALLERY_CHARACTERS;
            g_cursor = g_character_cursor;
            g_dirty = 1;
        }
        break;
    default:
        break;
    }
}

void on_gallery_event(screen_event event)
{
    switch (g_state) {
    case ST_GALLERY_KINDS:      on_kinds_event(event); break;
    case ST_GALLERY_CHARACTERS: on_characters_event(event); break;
    case ST_GALLERY_CARDS:      on_cards_event(event); break;
    default: break;
    }
}
