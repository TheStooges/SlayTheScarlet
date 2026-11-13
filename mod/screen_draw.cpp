/* Drawing the mod's screens. Text comes from the game's own card files and is passed through
 * to the game's font untouched. */
#include <stdio.h>
#include <string.h>
#include "internal.h"

/* The box under the cursor, and the bar beside a list that scrolls. */
#define CURSOR_FILL     0x60FFE080u
#define CURSOR_BORDER   0xFFFFE080u
#define WARNING_FILL    0x60FF6060u
#define WARNING_BORDER  0xFFFF6060u
#define SCROLL_TRACK    0x30FFFFFFu
#define SCROLL_THUMB    0xC0FFE080u
#define SCROLLBAR_X     616

/* The character grid, on the main menu and the gallery: a cell of 116 by 60 for each character. */
#define CELL_X(index) (24 + ((index) % COLS) * 120)
#define CELL_Y(index) (80 + ((index) / COLS) * 70)
#define CELL_W 116
#define CELL_H 60

static SokuLib::SWRFont g_font_big, g_font_small;
static int g_fonts_ready = 0;
static Sprite g_labels[MAX_TEXTS];
static int g_label_count = 0;
static Sprite g_art[STS_OFFER_MAX];
static int g_has_art[STS_OFFER_MAX];
static RectangleShape g_box;

static Sprite g_deck_art[MAX_DECK_ART];
static int g_has_deck_art[MAX_DECK_ART];
int g_deck_art_dirty = 1;

static void init_font(SokuLib::SWRFont &font, int height, int weight)
{
    SokuLib::FontDescription desc;

    memset(&desc, 0, sizeof desc);
    desc.r1 = desc.g1 = desc.b1 = 255;
    desc.r2 = desc.g2 = desc.b2 = 255;
    desc.height = height;
    desc.weight = weight;
    desc.italic = 0;
    desc.shadow = 2;
    desc.bufferSize = 1000000;
    desc.useOffset = 0;
    strcpy(desc.faceName, SokuLib::defaultFontName);
    font.create();
    font.setIndirect(desc);
}

static void ensure_fonts(void)
{
    if (g_fonts_ready)
        return;
    init_font(g_font_big, 20, FW_BOLD);
    init_font(g_font_small, 14, FW_NORMAL);
    g_fonts_ready = 1;
}

/* One line of the main menu's hint, with its right edge at HINT_RIGHT. */
#define HINT_RIGHT 626
static void make_hint_line(Sprite *sprite, const char *text, int pos_y, unsigned color)
{
    SokuLib::Vector2i real;

    if (!sprite->texture.createFromText(text, g_font_small, {300, 20}, &real))
        return;
    sprite->setSize(real.to<unsigned>());
    sprite->rect.left = 0;
    sprite->rect.top = 0;
    sprite->rect.width = real.x;
    sprite->rect.height = real.y;
    sprite->setPosition({HINT_RIGHT - real.x, pos_y});
    sprite->tint = color;
}

void screen_render_title_hint(void)
{
    static Sprite lines[2];
    static int shown_for = -2;
    const char *mod_in_the_way = blocking_mod();
    int blocked = mod_in_the_way || (!g_active && g_rules_changed_character >= 0);
    int now = mod_in_the_way ? 200 : blocked ? 100 + g_rules_changed_character : g_active ? g_active_character : -1;
    int wau;

    /* The text only needs making again when the run in use changes. */
    if (now != shown_for) {
        char text[64];

        ensure_fonts();
        make_hint_line(&lines[0], "F7: Slay the Scarlet", 422, COLOR_WHITE);
        if (mod_in_the_way)
            sprintf(text, tr("Disable %s to play runs"), mod_in_the_way);
        else if (blocked)
            sprintf(text, tr("%s: rules changed"), character_label(g_rules_changed_character));
        else if (g_active)
            sprintf(text, tr("In use: %s"), character_label(g_active_character));
        else
            sprintf(text, tr("No run in use"));
        make_hint_line(&lines[1], text, 438, blocked ? COLOR_RED : g_active ? COLOR_GREEN : COLOR_GREY);
        shown_for = now;
    }
    for (wau = 0; wau < 2; wau++)
        lines[wau].draw();
}

static void add_label(const char *text, SokuLib::SWRFont &font, int pos_x, int pos_y, int width, int height,
                      unsigned color)
{
    Sprite *sprite;
    SokuLib::Vector2i real;

    if (!text || !*text || g_label_count >= MAX_TEXTS)
        return;
    sprite = &g_labels[g_label_count];
    if (!sprite->texture.createFromText(text, font, {(unsigned)width, (unsigned)height}, &real))
        return;
    sprite->setSize(real.to<unsigned>());
    sprite->rect.left = 0;
    sprite->rect.top = 0;
    sprite->rect.width = real.x;
    sprite->rect.height = real.y;
    sprite->setRotation(0.0f);
    sprite->setPosition({pos_x, pos_y});
    sprite->tint = color;
    g_label_count++;
}

/* The player's real button, as the game's key config shows it: "Z or Pad2". Takes one of the
 * fields of KeyBindings. Keeps a few buffers so one line of text can name several buttons. */
static const char *button_name(int SokuLib::KeyBindings::*field)
{
    static char buffers[4][48];
    static int next = 0;
    char *out = buffers[next];
    char key[32] = "";
    int dik = SokuLib::profile1.keyboardBindings.*field;
    int pad = SokuLib::profile1.controllerBindings.*field;
    /* The scan code goes in bits 16-23, with bit 24 marking DirectInput's extended keys. */
    LONG param = (LONG)((dik & 0x7F) << 16) | ((dik & 0x80) ? (1L << 24) : 0);

    next = (next + 1) % 4;
    if (!GetKeyNameTextA(param, key, sizeof key))
        sprintf(key, "Key%d", dik);
    /* The key config screen counts pad buttons from 1. */
    if (pad >= 0)
        _snprintf(out, 47, tr("%s or Pad%d"), key, pad + 1);
    else
        _snprintf(out, 47, "%s", key);
    out[47] = 0;
    return out;
}

#define BUTTON_A button_name(&SokuLib::KeyBindings::a)
#define BUTTON_B button_name(&SokuLib::KeyBindings::b)
#define BUTTON_C button_name(&SokuLib::KeyBindings::c)
#define BUTTON_D button_name(&SokuLib::KeyBindings::d)

/* Skill inputs use the game's own deck screen images, one per input, in the skill's
 * cost_or_slot order. Each has "Skill Card" on its top half and the arrows and button name
 * on its bottom half; only the arrows are drawn. The positions were measured from the images:
 * three arrows for most inputs, two for 22. */
#define SKILL_INPUT_IMAGES 5
#define SKILL_INPUT_TOP 16
#define SKILL_INPUT_H 14
static const int g_input_left[SKILL_INPUT_IMAGES] = { 5, 5, 5, 5, 6 };
static const int g_input_width[SKILL_INPUT_IMAGES] = { 31, 31, 31, 31, 18 };

typedef struct {
    int image;
    int pos_x, pos_y;
} input_draw;

static Sprite g_input_sprites[SKILL_INPUT_IMAGES];
static int g_input_loaded[SKILL_INPUT_IMAGES];
static int g_inputs_ready = 0;
static input_draw g_input_draws[STS_OFFER_MAX + 1];
static int g_input_draw_count = 0;

static void load_skill_input_images(void)
{
    int wau;

    if (g_inputs_ready)
        return;
    g_inputs_ready = 1;
    for (wau = 0; wau < SKILL_INPUT_IMAGES; wau++) {
        char path[64];
        Sprite *sprite = &g_input_sprites[wau];

        sprintf(path, "data/profile/deck2/022_SkillCard%03dCost.bmp", wau);
        g_input_loaded[wau] = sprite->texture.loadFromGame(path);
        if (!g_input_loaded[wau]) {
            slog("could not load %s", path);
            continue;
        }
        sprite->setSize({(unsigned)g_input_width[wau], SKILL_INPUT_H});
        sprite->rect.left = g_input_left[wau];
        sprite->rect.top = SKILL_INPUT_TOP;
        sprite->rect.width = g_input_width[wau];
        sprite->rect.height = SKILL_INPUT_H;
        sprite->tint = COLOR_GOLD;
    }
}

/* Queues a skill's input image for drawing. */
static void add_skill_input(int slot, int pos_x, int pos_y)
{
    load_skill_input_images();
    if (slot >= 0 && slot < SKILL_INPUT_IMAGES && g_input_loaded[slot]
        && g_input_draw_count < (int)(sizeof g_input_draws / sizeof g_input_draws[0])) {
        input_draw *draw = &g_input_draws[g_input_draw_count++];
        draw->image = slot;
        draw->pos_x = pos_x;
        draw->pos_y = pos_y;
    }
}

/* Copies up to cols bytes of s into out, never splitting a two-byte Shift-JIS character.
 * With at_spaces set, a line that does not reach the end of s breaks at its last space.
 * Returns how many bytes of s were used. */
static size_t take_line(const char *text, int cols, int at_spaces, char *out)
{
    size_t length = 0, space = 0;
    size_t len;

    while (text[length] && length < (size_t)cols) {
        size_t step = IsDBCSLeadByteEx(932, (BYTE)text[length]) && text[length + 1] ? 2 : 1;
        if (length + step > (size_t)cols)
            break;
        if (text[length] == ' ')
            space = length;
        length += step;
    }
    if (at_spaces && text[length] && text[length] != ' ' && space > 0)
        length = space;
    memcpy(out, text, length);
    len = length;
    while (len > 0 && out[len - 1] == ' ')
        len--;
    out[len] = 0;
    return length;
}

/* The game stores card descriptions in rows of a fixed number of bytes, padded with spaces where a
 * sentence ends early: 36 in the English translation, 30 (15 full-width characters) in Japanese.
 * Splitting at the same width puts each sentence where the game's deck editor shows it. */
static int description_row_bytes(void)
{
    return game_is_japanese() ? 30 : 36;
}

static void add_wrapped(const char *text, SokuLib::SWRFont &font, int pos_x, int pos_y, int width, int line_h,
                        int cols, int at_spaces, int max_lines, unsigned color)
{
    char line[DESC_SIZE];
    int row;

    if (!text)
        return;
    for (row = 0; row < max_lines && *text; row++) {
        text += take_line(text, cols, at_spaces, line);
        while (at_spaces && *text == ' ')
            text++;
        add_label(line, font, pos_x, pos_y + row * line_h, width, line_h + 4, color);
    }
}

/* Help text wraps at this many bytes: 40 full-width characters in Japanese, which has no spaces
 * to break at, or about 84 letters in English, broken at spaces. */
#define HELP_COLUMNS (game_is_japanese() ? 80 : 78)

/* How many lines add_wrapped takes for this text. */
static int count_wrapped_lines(const char *text, int cols, int at_spaces)
{
    char line[DESC_SIZE];
    int lines = 0;

    while (*text) {
        size_t used = take_line(text, cols, at_spaces, line);

        if (used == 0)
            break;
        text += used;
        while (at_spaces && *text == ' ')
            text++;
        lines++;
    }
    return lines;
}

/* A paragraph of help below the settings, from pos_y. Returns the y after it. */
static int add_help(const char *text, int pos_y, int max_lines, unsigned color)
{
    int at_spaces = !game_is_japanese();
    int lines;

    if (!text || !*text || max_lines <= 0)
        return pos_y;
    lines = count_wrapped_lines(text, HELP_COLUMNS, at_spaces);
    if (lines > max_lines)
        lines = max_lines;
    add_wrapped(text, g_font_small, 28, pos_y, 590, 17, HELP_COLUMNS, at_spaces, lines, color);
    return pos_y + lines * 17;
}

/* A line about the reward in progress, its last row at last_y. Long card names can take it onto a
 * second row, which goes above the first so the key prompts stay clear. */
#define REWARD_NOTICE_Y 416
static void add_reward_notice(const char *text, int last_y, unsigned color)
{
    int at_spaces = !game_is_japanese();
    int lines = count_wrapped_lines(text, HELP_COLUMNS, at_spaces);

    if (lines > 2)
        lines = 2;
    add_wrapped(text, g_font_small, 20, last_y - (lines - 1) * 17, 600, 17, HELP_COLUMNS, at_spaces, lines, color);
}

/* A card's image. The character is whose card it is: the same ID is a different card for each. */
static int load_card_sprite(Sprite *sprite, int character, uint16_t id, int pos_x, int pos_y, unsigned scale)
{
    char path[128];
    SokuLib::Vector2u size;

    if (id < 100)
        sprintf(path, "data/card/common/card%03u.bmp", (unsigned)id);
    else
        sprintf(path, "data/card/%.64s/card%03u.bmp", SokuLib::getCharName((unsigned)character), (unsigned)id);
    if (!sprite->texture.loadFromGame(path))
        return 0;
    size = sprite->texture.getSize();
    sprite->setSize({size.x * scale, size.y * scale});
    sprite->rect.left = 0;
    sprite->rect.top = 0;
    sprite->rect.width = (int)size.x;
    sprite->rect.height = (int)size.y;
    sprite->setPosition({pos_x, pos_y});
    return 1;
}

static void load_art(int index, uint16_t id, int pos_x, int pos_y)
{
    g_has_art[index] = load_card_sprite(&g_art[index], g_run.character, id, pos_x, pos_y, 2);
}

/* Reading card files takes a moment, so the deck images load once per deck rather than
 * on every cursor move. */
static void load_deck_art(void)
{
    int wau;

    for (wau = 0; wau < MAX_DECK_ART; wau++)
        g_has_deck_art[wau] = wau < g_run.deck.count
            && load_card_sprite(&g_deck_art[wau], g_run.character, g_run.deck.ids[wau],
                                DECK_X + (wau % DECK_COLS) * DECK_PITCH_X, DECK_Y + (wau / DECK_COLS) * DECK_PITCH_Y, 1);
    g_deck_art_dirty = 0;
}

/* The card's type line. Skills also show their input and button, as the game's deck screen does. */
static void add_card_kind(const sts_card *card, int character, int pos_x, int pos_y, int width)
{
    char text[48], skill[STS_SKILL_LABEL_SIZE];

    switch (card->type) {
    case STS_CARD_SYSTEM:
        sprintf(text, "System, Cost %d", card->cost_or_slot);
        break;
    case STS_CARD_SPELL:
        sprintf(text, "Spell, Cost %d", card->cost_or_slot);
        break;
    case STS_CARD_SKILL:
        if (sts_skill_label(&g_db, character, card->id, skill)) {
            int start = pos_x, before = g_label_count, number_w = 0, slot = card->cost_or_slot;

            /* "Skill, a" and "214" are separate so the arrows can centre over the digits. */
            sprintf(text, "Skill, %c", skill[0]);
            add_label(text, g_font_small, pos_x, pos_y, width, 20, COLOR_GOLD);
            if (g_label_count > before)
                pos_x += (int)g_labels[before].getSize().x;
            before = g_label_count;
            add_label(skill + 1, g_font_small, pos_x, pos_y, 40, 20, COLOR_GOLD);
            if (g_label_count > before)
                number_w = (int)g_labels[before].getSize().x;
            if (slot < SKILL_INPUT_IMAGES) {
                int arrows_x = pos_x + (number_w - g_input_width[slot]) / 2;
                add_skill_input(slot, arrows_x > start ? arrows_x : start, pos_y - SKILL_INPUT_H);
            }
            return;
        }
        sprintf(text, "Skill");
        break;
    default:
        text[0] = 0;
        break;
    }
    add_label(text, g_font_small, pos_x, pos_y, width, 20, COLOR_GOLD);
}

#define OFFER_W 180
#define OFFER_GAP 16
#define OFFER_Y 70

static int offer_x(int wau, int count)
{
    int total = count * OFFER_W + (count - 1) * OFFER_GAP;
    return (SCREEN_W - total) / 2 + wau * (OFFER_W + OFFER_GAP);
}

/* What a character's run is doing, under its name on the main menu, and the colour to show it in.
 * Empty for a character with no run. */
static void run_status(int character, char *out, unsigned *color)
{
    out[0] = 0;
    *color = COLOR_WHITE;
    if (!g_has_save[character])
        return;
    if (g_saved_rules_changed[character]) {
        sprintf(out, tr("Rules changed"));
        *color = COLOR_RED;
    } else if (g_saved_phase[character] == STS_PHASE_DRAFT) {
        sprintf(out, tr("Draft %d/%d"), g_saved_picks[character], sts_settings_draft_length(&g_settings, character));
        *color = COLOR_GOLD;
    } else if (g_saved_phase[character] == STS_PHASE_REWARD) {
        sprintf(out, tr("Reward waiting"));
        *color = COLOR_GOLD;
    } else if (g_saved_phase[character] == STS_PHASE_FINISHED) {
        sprintf(out, tr("Finished"));
        *color = COLOR_GREY;
    } else if (g_active && g_active_character == character) {
        sprintf(out, tr("In use"));
        *color = COLOR_GREEN;
    } else {
        sprintf(out, tr("Deck ready"));
        *color = COLOR_GREY;
    }
}

static void rebuild_choose(void)
{
    int character;
    char line[128];

    add_label(tr("Slay the Scarlet: choose a character"), g_font_big, 20, 16, 600, 30, COLOR_WHITE);
    if (g_settings.seed[0] && !blocking_mod()) {
        _snprintf(line, sizeof line - 1, tr("Currently set seed: %s."), g_settings.seed);
        line[sizeof line - 1] = 0;
        add_label(line, g_font_small, 20, 396, 600, 20, COLOR_GOLD);
    }
    if (g_settings.starter_enabled)
        add_label(tr("Starting decks are on: each draft begins with set cards for the character."),
                  g_font_small, 20, 414, 600, 20, COLOR_GOLD);
    if (blocking_mod()) {
        sprintf(line, tr("%s can overwrite your real decks with a run deck, so runs are off."), blocking_mod());
        add_label(line, g_font_small, 20, 392, 600, 20, COLOR_RED);
        sprintf(line, tr("Disable it in SWRSToys.ini and restart the game to play runs."));
        add_label(line, g_font_small, 20, 410, 600, 20, COLOR_RED);
    }
    add_label(tr("Settings"), g_font_big, CHOOSE_SETTINGS_X + 6, CHOOSE_SETTINGS_Y + 4, 200, 26, COLOR_WHITE);
    add_label(tr("Gallery"), g_font_big, CHOOSE_GALLERY_X + 6, CHOOSE_GALLERY_Y + 4, 110, 26, COLOR_WHITE);
    for (character = 0; character < STS_NUM_CHARACTERS; character++) {
        int pos_x = CELL_X(character);
        int pos_y = CELL_Y(character);
        unsigned color;

        add_label(character_label(character), g_font_big, pos_x + 6, pos_y + 6, 110, 26, COLOR_WHITE);
        run_status(character, line, &color);
        add_label(line, g_font_small, pos_x + 6, pos_y + 32, 110, 20, color);
    }
    switch (choose_toggle_for(g_cursor)) {
    case TOGGLE_USE:
        sprintf(line, tr("%s: use %s's run"), BUTTON_D, character_label(g_cursor));
        add_label(line, g_font_small, 20, 458, 600, 20, COLOR_GREY);
        break;
    case TOGGLE_STOP:
        sprintf(line, tr("%s: stop using %s's run"), BUTTON_D, character_label(g_cursor));
        add_label(line, g_font_small, 20, 458, 600, 20, COLOR_GREY);
        break;
    case TOGGLE_NONE:
        break;
    }
    if (g_alt_armed)
        sprintf(line, tr("Press %s again to delete this character's run."), BUTTON_C);
    else if (g_cursor == CHOOSE_SETTINGS || g_cursor == CHOOSE_GALLERY || !run_file_exists(g_cursor))
        sprintf(line, tr("%s: open   %s: close"), BUTTON_A, BUTTON_B);
    else
        sprintf(line, tr("%s: open   %s: close   %s twice: delete run"), BUTTON_A, BUTTON_B, BUTTON_C);
    add_label(line, g_font_small, 20, 440, 600, 20, g_alt_armed ? COLOR_RED : COLOR_GREY);
}

/* The deck so far, beside the description on the draft screen. Cards are drawn small, with
 * outlines of the same size for the picks still to come. Larger decks show their most recent cards. */
#define DRAFT_DECK_X 296
#define DRAFT_DECK_Y 332
#define DRAFT_DECK_COLS 10
#define DRAFT_DECK_SLOTS 20
#define DRAFT_DECK_PITCH_X 32
#define DRAFT_DECK_PITCH_Y 49
#define DRAFT_DECK_CARD_W 28
#define DRAFT_DECK_CARD_H 46
/* The game's card images have an empty margin, so the image is drawn larger than the box for the
 * visible card to fill it. */
#define DRAFT_DECK_IMAGE_W 31
#define DRAFT_DECK_IMAGE_H 47
#define DRAFT_DECK_IMAGE_OFFSET_X (-1)

static int draft_deck_shown(void)
{
    return g_state == ST_DRAFT && g_settings.draft_show_deck;
}

/* The offers on screen: the run's, or the ones the roulette runs through. */
static const sts_offer *shown_offers(int *count)
{
    if (g_state == ST_ROULETTE) {
        *count = g_roulette_count;
        return g_roulette_offers;
    }
    *count = g_run.offer_count;
    return g_run.offers;
}

/* What the offers are for, and where the run is: the match on the reward screen, the pick and the deck
 * on the draft. */
static void add_offers_heading(void)
{
    char line[192];

    if (g_state == ST_REWARD || g_state == ST_ROULETTE) {
        const char *format = g_state == ST_ROULETTE ? tr("%s: a card is picked at random")
                           : g_settings.reward.mode == STS_REWARD_ADD ? tr("%s: choose a card to add")
                           : g_settings.reward.mode == STS_REWARD_REMOVE ? tr("%s: choose a card to remove")
                           : g_settings.offer_pick == STS_OFFER_PICK_RANDOM ? tr("%s: this card swaps in")
                           : tr("%s: choose a card to swap in");
        sprintf(line, format, character_label(g_run.character));
        add_label(line, g_font_big, 20, 16, 470, 30, COLOR_WHITE);
        /* Owed rewards come from the latest matches, so the offer on screen is from an earlier one. */
        if (g_settings.match_limit > 0)
            sprintf(line, tr("Match %d of %d"), g_run.matches_played - g_run.owed_count, g_settings.match_limit);
        else
            sprintf(line, tr("Match %d"), g_run.matches_played - g_run.owed_count);
        add_label(line, g_font_small, 500, 22, 130, 20, COLOR_GREY);
    } else {
        sprintf(line, tr("%s: pick %d of %d"), character_label(g_run.character),
                g_run.picks_done + 1, sts_settings_draft_length(&g_settings, g_run.character));
        add_label(line, g_font_big, 20, 16, 400, 30, COLOR_WHITE);
        sprintf(line, tr("Deck %d/%d"), g_run.deck.count, g_settings.rules.deck_min);
        add_label(line, g_font_small, 500, 22, 130, 20, COLOR_GREY);
    }
}

/* One offer in its box: the card's image, name, type, and copies when there is more than one. */
static void add_offer_card(int index, const sts_offer *offer, int offer_count)
{
    char copies[16];
    const card_text *entry = find_text(offer->id);
    const sts_card *card = sts_card_db_find(&g_db, g_run.character, offer->id);
    int pos_x = offer_x(index, offer_count);

    load_art(index, offer->id, pos_x + 8, OFFER_Y + 8);
    add_wrapped(entry ? entry->name : "?", g_font_small, pos_x + 8, OFFER_Y + 146, OFFER_W - 16, 17, 22, 1, 3,
                COLOR_WHITE);
    if (card)
        add_card_kind(card, g_run.character, pos_x + 8, OFFER_Y + 202, OFFER_W - 16);
    if (offer->copies > 1) {
        sprintf(copies, "x%d", offer->copies);
        add_label(copies, g_font_big, pos_x + OFFER_W - 40, OFFER_Y + 8, 36, 28, COLOR_GOLD);
    }
}

static void rebuild_draft(void)
{
    char line[192];
    int wau, offer_count;
    const sts_offer *offers = shown_offers(&offer_count);

    if (draft_deck_shown()) {
        if (g_deck_art_dirty)
            load_deck_art();
        add_label(tr("Your deck so far"), g_font_small, DRAFT_DECK_X, DRAFT_DECK_Y - 24, 320, 20, COLOR_GREY);
    }
    add_offers_heading();
    for (wau = 0; wau < offer_count && wau < STS_OFFER_MAX; wau++)
        add_offer_card(wau, &offers[wau], offer_count);
    if (g_cursor < offer_count) {
        const card_text *entry = find_text(offers[g_cursor].id);
        if (entry)
            add_wrapped(entry->desc, g_font_small, 24, OFFER_Y + 236, 592, 17, description_row_bytes(), 0, 6,
                        COLOR_WHITE);
    }
    if (g_state == ST_ROULETTE)
        sprintf(line, tr("%s: skip"), BUTTON_A);
    else if (g_state == ST_REWARD && g_run.offer_count == 1)
        sprintf(line, tr("%s: take"), BUTTON_A);
    else if (g_state == ST_REWARD)
        sprintf(line, tr("Left/Right: choose   %s: take"), BUTTON_A);
    else
        sprintf(line, tr("Left/Right: choose   %s: take   %s: back (progress is saved)"), BUTTON_A, BUTTON_B);
    add_label(line, g_font_small, 20, 440, 600, 20, COLOR_GREY);
}

/* Name, type and description of one of the character's cards, starting at y. */
static void add_card_details(int character, uint16_t id, int pos_y)
{
    const card_text *entry = find_text(id);
    const sts_card *card = sts_card_db_find(&g_db, character, id);

    /* A skill's arrows sit above its type line, so they need room under the name. Every type gets
     * that room, so the type line and description sit at the same height for any card. Six rows of
     * description still end above the status line at y 396. */
    int kind_y = pos_y + 46;

    add_label(entry ? entry->name : "?", g_font_big, 24, pos_y, 592, 28, COLOR_WHITE);
    if (card)
        add_card_kind(card, character, 24, kind_y, 592);
    if (entry)
        add_wrapped(entry->desc, g_font_small, 24, kind_y + 20, 592, 17, description_row_bytes(), 0, 6,
                    COLOR_WHITE);
}

/* One heading and its cards. Returns the y position after them. */
static int add_change_section(const char *heading, const deck_change *list, int count, int pos_y, unsigned color)
{
    const int pos_x = 288;
    int wau;

    if (count == 0)
        return pos_y;
    add_label(heading, g_font_small, pos_x, pos_y, 340, 20, color);
    pos_y += 18;
    for (wau = 0; wau < count; wau++) {
        const card_text *entry = find_text(list[wau].id);
        char text[NAME_SIZE + 32];

        /* Names only: the card details beside the list show the type, cost or skill input. */
        if (list[wau].copies > 1)
            _snprintf(text, sizeof text - 1, "%s x%d", entry ? entry->name : "?", list[wau].copies);
        else
            _snprintf(text, sizeof text - 1, "%s", entry ? entry->name : "?");
        text[sizeof text - 1] = 0;
        add_label(text, g_font_small, pos_x, pos_y, 340, 20, COLOR_WHITE);
        pos_y += 17;
    }
    return pos_y + 6;
}

/* What a reward changed, or is about to, in the space beside the card details. y is the top of
 * those details; the list starts below their first line, which a long card name fills. */
static void add_change_list(int pos_y, const char *removed_heading, const char *added_heading)
{
    pos_y += 30;
    pos_y = add_change_section(removed_heading, g_changes_removed, g_changes_removed_count, pos_y, COLOR_RED);
    add_change_section(added_heading, g_changes_added, g_changes_added_count, pos_y, COLOR_GREEN);
}

static void rebuild_reward_remove(void)
{
    char line[192];
    const sts_offer *offer = &g_run.offers[g_pending_offer];
    const card_text *entry = find_text(offer->id);
    int remaining = offer->copies - g_selected_count;

    if (g_deck_art_dirty)
        load_deck_art();
    if (g_state == ST_REWARD_CONFIRM) {
        add_label(tr("Choose what leaves your deck"), g_font_big, 20, 16, 600, 30, COLOR_WHITE);
        add_label(tr("Confirm?"), g_font_big, 455, 432, 175, 30, COLOR_GOLD);
        if (g_cursor < g_run.deck.count)
            add_card_details(g_run.character, g_run.deck.ids[g_cursor], DECK_Y + 2 * DECK_PITCH_Y + 8);
        add_change_list(DECK_Y + 2 * DECK_PITCH_Y + 8, tr("Removing"), tr("Adding"));
        sprintf(line, tr("%s: confirm   %s: change the last pick"), BUTTON_A, BUTTON_B);
        add_label(line, g_font_small, 20, 440, 600, 20, COLOR_GREY);
        return;
    }
    add_label(tr("Choose what leaves your deck"), g_font_big, 20, 16, 600, 30, COLOR_WHITE);
    if (g_cursor < g_run.deck.count)
        add_card_details(g_run.character, g_run.deck.ids[g_cursor], DECK_Y + 2 * DECK_PITCH_Y + 8);
    if (g_cursor < g_run.deck.count && g_run.deck.ids[g_cursor] == offer->id) {
        add_reward_notice(tr("This is the card you are taking, so it cannot leave."), REWARD_NOTICE_Y, COLOR_RED);
    } else {
        sprintf(line, tr("Replacing with %dx %.90s."), offer->copies, entry ? entry->name : "?");
        add_reward_notice(line, REWARD_NOTICE_Y - 17, COLOR_GOLD);
        sprintf(line, tr("Choose %d more."), remaining);
        add_reward_notice(line, REWARD_NOTICE_Y, COLOR_GOLD);
    }
    sprintf(line, tr("Arrows: choose   %s: select   %s: back to the offers"), BUTTON_A, BUTTON_B);
    add_label(line, g_font_small, 20, 440, 600, 20, COLOR_GREY);
}

/* What going back to the run's rules would change in settings.ini, before it does. */
#define RULE_TABLE_NOW_X 266
#define RULE_TABLE_RUN_X 446
static void rebuild_rules_restore(void)
{
    char line[256];
    int pos_y = RULE_TABLE_Y - 21, wau, menu = -1, line_number = 0, lines = rule_table_line_count();

    sprintf(line, tr("%s: go back to this run's rules"), character_label(g_run.character));
    add_label(line, g_font_big, 20, 16, 600, 30, COLOR_WHITE);
    add_label(tr("These settings in settings.ini would change:"), g_font_small, 40, 58, 580, 20, COLOR_WHITE);

    /* A table: the setting, its value now, and its value under this run's rules, under each menu. */
    add_label(tr("Current settings"), g_font_small, RULE_TABLE_NOW_X, pos_y, 170, 20, COLOR_GREY);
    add_label(tr("This run's rules"), g_font_small, RULE_TABLE_RUN_X, pos_y, 190, 20, COLOR_GREY);
    /* Only the lines in view, when there are more than fit. */
    for (wau = 0; wau < g_rule_change_count; wau++) {
        const rule_change *change = &g_rule_changes[wau];
        int row;

        if (change->menu != menu) {
            menu = change->menu;
            row = line_number++ - g_rule_table_top;
            if (row >= 0 && row < rule_table_rows())
                add_label(settings_menu_name(menu), g_font_small, 40, RULE_TABLE_Y + row * RULE_TABLE_ROW_H, 200, 20,
                          COLOR_GOLD);
        }
        row = line_number++ - g_rule_table_top;
        if (row < 0 || row >= rule_table_rows())
            continue;
        pos_y = RULE_TABLE_Y + row * RULE_TABLE_ROW_H;
        add_label(change->label, g_font_small, 56, pos_y, RULE_TABLE_NOW_X - 64, 20, COLOR_WHITE);
        add_label(change->now, g_font_small, RULE_TABLE_NOW_X, pos_y, RULE_TABLE_RUN_X - RULE_TABLE_NOW_X - 8, 20, COLOR_WHITE);
        add_label(change->restored, g_font_small, RULE_TABLE_RUN_X, pos_y, 610 - RULE_TABLE_RUN_X, 20, COLOR_GREEN);
    }
    pos_y = RULE_TABLE_Y + (lines < rule_table_rows() ? lines : rule_table_rows()) * RULE_TABLE_ROW_H;
    if (g_rule_changes_hidden > 0) {
        add_label(tr("Settings that are only in settings.ini change too."), g_font_small, 40, pos_y + 4, 580, 20, COLOR_GREY);
        pos_y += 23;
    }
    if (g_rules_restore_others > 0) {
        _snprintf(line, sizeof line - 1, tr("Then these runs would show Rules changed: %s"), g_settings_affected);
        line[sizeof line - 1] = 0;
        add_wrapped(line, g_font_small, 40, pos_y + 6, 580, 17, HELP_COLUMNS, 1, 2, COLOR_RED);
    }
    if (lines > rule_table_rows())
        sprintf(line, tr("Up/Down: scroll   %s: change the rules   %s: back"), BUTTON_A, BUTTON_B);
    else
        sprintf(line, tr("%s: change the rules   %s: back"), BUTTON_A, BUTTON_B);
    add_label(line, g_font_small, 20, 440, 600, 20, COLOR_GREY);
}

static void rebuild_rules(void)
{
    char line[192];
    int wau;

    sprintf(line, tr("%s: the rules have changed"), character_label(g_run.character));
    add_label(line, g_font_big, 20, 16, 600, 30, COLOR_WHITE);
    add_label(tr("This run was made under different rules from the current settings."),
              g_font_small, 40, 80, 580, 20, COLOR_WHITE);
    add_label(g_rules_problem, g_font_small, 40, 106, 580, 20, COLOR_RED);

    for (wau = 0; wau < rules_option_count(); wau++) {
        int pos_y = RULES_ROW_Y + wau * RULES_ROW_PITCH;
        const char *note = NULL;

        switch (rules_option_at(wau)) {
        case RULES_CARRY_ON:
            add_label(tr("Carry on under the current rules"), g_font_big, 56, pos_y, 500, 28, COLOR_WHITE);
            note = tr("Your deck and progress stay as they are.");
            break;
        case RULES_RESTORE:
            add_label(tr("Go back to this run's rules"), g_font_big, 56, pos_y, 500, 28, COLOR_WHITE);
            note = tr("Changes settings.ini back to the rules this run was made under.");
            break;
        case RULES_OPTION_COUNT:
            break;
        case RULES_BACK:
            add_label(tr("Back"), g_font_big, 56, pos_y, 500, 28, COLOR_WHITE);
            note = tr("Leaves the run as it is.");
            break;
        case RULES_START_OVER:
            add_label(tr("Start over"), g_font_big, 56, pos_y, 500, 28, g_rules_start_over_armed ? COLOR_RED : COLOR_WHITE);
            note = tr("Discards this run's deck and progress.");
            break;
        }
        if (note)
            add_label(note, g_font_small, 72, pos_y + 26, 540, 20, COLOR_GREY);
    }
    if (g_rules_start_over_armed)
        sprintf(line, tr("Press %s again to discard this run.   %s: cancel"), BUTTON_A, BUTTON_B);
    else
        sprintf(line, tr("Up/Down: choose   %s: select   %s: back"), BUTTON_A, BUTTON_B);
    add_label(line, g_font_small, 20, 440, 600, 20, g_rules_start_over_armed ? COLOR_RED : COLOR_GREY);
}

/* Cards leaving at random: the deck as it was, the cursor on it. */
static void rebuild_sweep(void)
{
    char line[192];
    const card_text *entry = g_changes_added_count > 0 ? find_text(g_changes_added[0].id) : NULL;

    if (g_deck_art_dirty)
        load_deck_art();
    sprintf(line, tr("%s: cards leave at random"), character_label(g_run.character));
    add_label(line, g_font_big, 20, 16, 600, 30, COLOR_WHITE);
    if (g_cursor < g_run.deck.count)
        add_card_details(g_run.character, g_run.deck.ids[g_cursor], DECK_Y + 2 * DECK_PITCH_Y + 8);
    if (g_changes_added_count > 0) {
        sprintf(line, tr("Replacing with %dx %.90s."), g_changes_added[0].copies, entry ? entry->name : "?");
        add_reward_notice(line, REWARD_NOTICE_Y, COLOR_GOLD);
    }
    sprintf(line, tr("%s: skip"), BUTTON_A);
    add_label(line, g_font_small, 20, 440, 600, 20, COLOR_GREY);
}

/* The type choices, below the two rows of the deck. */
#define REWARD_TYPE_Y (DECK_Y + 2 * DECK_PITCH_Y + 14)
#define REWARD_TYPE_PITCH 42

static void rebuild_reward_type(void)
{
    char line[192];
    const sts_offer *offer = &g_run.offers[g_pending_offer];
    const card_text *entry = find_text(offer->id);
    unsigned mask = sts_replace_types_available(&g_db, g_run.character, &g_run.deck, offer->id, offer->copies);
    int type;

    if (g_deck_art_dirty)
        load_deck_art();
    add_label(tr("Choose what type leaves your deck"), g_font_big, 20, 16, 600, 30, COLOR_WHITE);
    for (type = 0; type < 3; type++) {
        /* Copies of the incoming card are not counted, as they cannot leave. */
        int count = sts_replace_count_of_type(&g_db, g_run.character, &g_run.deck, offer->id, type);
        const char *name = type == STS_CARD_SYSTEM ? tr("System cards")
                         : type == STS_CARD_SKILL ? tr("Skill cards") : tr("Spell cards");

        sprintf(line, tr("%s (%d can leave)"), name, count);
        add_label(line, g_font_big, 56, REWARD_TYPE_Y + type * REWARD_TYPE_PITCH, 500, 28,
                  (mask & STS_TYPE_BIT(type)) ? COLOR_WHITE : COLOR_GREY);
    }
    sprintf(line, tr("Replacing with %dx %.90s."), offer->copies, entry ? entry->name : "?");
    add_reward_notice(line, REWARD_NOTICE_Y - 17, COLOR_GOLD);
    add_reward_notice(tr("Random cards of that type leave."), REWARD_NOTICE_Y, COLOR_GOLD);
    sprintf(line, tr("Up/Down: choose   %s: confirm   %s: back to the offers"), BUTTON_A, BUTTON_B);
    add_label(line, g_font_small, 20, 440, 600, 20, COLOR_GREY);
}

static void rebuild_deck(void)
{
    char line[192];
    int rows = (g_run.deck.count + DECK_COLS - 1) / DECK_COLS;
    int pos_y;

    if (g_deck_art_dirty)
        load_deck_art();

    if (g_run.phase == STS_PHASE_FINISHED && g_viewed_match == -1)
        sprintf(line, tr("%s: the deck after the draft"), character_label(g_run.character));
    else if (g_run.phase == STS_PHASE_FINISHED && g_viewed_match == g_run.history_count - 1)
        sprintf(line, tr("%s: the final deck"), character_label(g_run.character));
    else if (g_run.phase == STS_PHASE_FINISHED && g_viewed_match >= 0)
        sprintf(line, tr("%s: the deck after match %d"), character_label(g_run.character),
                g_run.history_forgotten + g_viewed_match + 1);
    else
        sprintf(line, tr("%s: your deck"), character_label(g_run.character));
    add_label(line, g_font_big, 20, 16, 500, 30, COLOR_WHITE);
    sprintf(line, tr("%d cards"), g_run.deck.count);
    add_label(line, g_font_small, 540, 22, 90, 20, COLOR_GREY);

    /* Details of the card under the cursor, below however many rows the deck needs. */
    if (rows > 2)
        rows = 2;
    pos_y = DECK_Y + rows * DECK_PITCH_Y + 8;
    if (g_cursor < g_run.deck.count)
        add_card_details(g_run.character, g_run.deck.ids[g_cursor], pos_y);
    add_change_list(pos_y, tr("Removed"), tr("Added"));
    if (g_run.phase == STS_PHASE_FINISHED) {
        add_label(tr("This run is over, so its deck is no longer used."), g_font_small, 20, 396, 600, 20, COLOR_GREY);
        sprintf(line, tr("Arrows: inspect   %s: back to the score"), BUTTON_B);
        add_label(line, g_font_small, 20, 440, 600, 20, COLOR_GREY);
        return;
    }
    add_label(g_status, g_font_small, 20, 396, 600, 20, run_deck_in_use() ? COLOR_GREEN : COLOR_GOLD);
    /* With the other key prompts at the bottom, not the status line above. */
    sprintf(line, run_deck_in_use() ? tr("%s: stop using this run") : tr("%s: use this run"), BUTTON_D);
    add_label(line, g_font_small, 20, 458, 600, 20, COLOR_GREY);
    if (g_run.phase == STS_PHASE_REWARD)
        sprintf(line, tr("Arrows: inspect   %s: next offer"), BUTTON_A);
    else if (g_alt_armed)
        sprintf(line, tr("Press %s again to discard this run and start over."), BUTTON_C);
    else
        sprintf(line, tr("Arrows: inspect   %s: back   %s twice: start over"), BUTTON_B, BUTTON_C);
    add_label(line, g_font_small, 20, 440, 600, 20, g_alt_armed ? COLOR_RED : COLOR_GREY);
}

/* A finished run: its results, then the starting deck and each match, with what its reward changed.
 * The deck at the chosen point is drawn above the list (draw_score_deck). */
/* The run's results, and for events the seed and rules it was played under and how often it was
 * started over, so a screenshot of this screen shows them. */
static void add_score_summary(void)
{
    char line[192];
    int won, lost, rounds_won, rounds_lost;

    count_results(&g_run, &won, &lost, &rounds_won, &rounds_lost);
    sprintf(line, tr("%s: the run is over"), character_label(g_run.character));
    add_label(line, g_font_big, 20, 16, 600, 30, COLOR_WHITE);
    sprintf(line, tr("Won %d of %d matches."), won, g_run.matches_played);
    add_label(line, g_font_big, 40, 52, 560, 28, COLOR_GOLD);
    sprintf(line, tr("Rounds won %d, lost %d."), rounds_won, rounds_lost);
    add_label(line, g_font_small, 40, 82, 240, 20, COLOR_WHITE);
    if (g_run.has_rules && g_run.rules.seed[0])
        _snprintf(line, sizeof line - 1, tr("Seed %s   Rules %08x   Started over: %d"), g_run.rules.seed,
                  (unsigned)g_run.settings_hash, g_run.restarts);
    else
        _snprintf(line, sizeof line - 1, tr("Rules %08x   Started over: %d"), (unsigned)g_run.settings_hash, g_run.restarts);
    line[sizeof line - 1] = 0;
    add_label(line, g_font_small, 270, 82, 360, 20, COLOR_GREY);
}

/* The draft and each match, in two lines each, from the first one shown: the result, and what the
 * match's reward changed. */
static void add_score_entries(void)
{
    char result[192], change[640];
    int entry, match;

    for (entry = g_score_top; entry < score_entry_count() && entry < g_score_top + SCORE_VISIBLE; entry++) {
        int pos_y = SCORE_LIST_Y + (entry - g_score_top) * SCORE_ENTRY_H;

        match = score_entry_match(entry);
        if (match < 0) {
            add_label(tr("The draft"), g_font_small, 48, pos_y, 552, 20, COLOR_WHITE);
            continue;
        }
        describe_match(&g_run, match, result, sizeof result, change, sizeof change);
        add_label(result, g_font_small, 48, pos_y, 552, 20, g_run.history[match].won ? COLOR_GREEN : COLOR_RED);
        if (change[0])
            add_label(change, g_font_small, 68, pos_y + 17, 532, 20, COLOR_WHITE);
    }
}

static void rebuild_score(void)
{
    char line[192];
    int match;
    sts_deck deck;

    add_score_summary();
    /* What the deck beside it is. */
    match = score_entry_match(g_cursor);
    if (match < 0)
        sprintf(line, "%s", tr("The draft"));
    else if (match == g_run.history_count - 1)
        sprintf(line, "%s", tr("The final match"));
    else
        sprintf(line, tr("After match %d"), g_run.history_forgotten + match + 1);
    add_label(line, g_font_big, SCORE_DECK_X + 340, SCORE_DECK_Y, 240, 28, COLOR_WHITE);
    /* Above the cards that left, drawn below it. */
    if (sts_run_deck_after(&g_run, match, &deck) && match >= 0 && g_run.history[match].removed_count > 0)
        add_label(tr("Replaced"), g_font_small, SCORE_DECK_X + 340, SCORE_DECK_Y + 30, 240, 20, COLOR_RED);
    add_score_entries();

    sprintf(line, tr("Up/Down: choose   %s: see this deck   %s: back"), BUTTON_A, BUTTON_B);
    add_label(line, g_font_small, 20, 440, 600, 20, COLOR_GREY);
    if (g_alt_armed)
        sprintf(line, tr("Press %s again to discard this run and start over."), BUTTON_C);
    else
        sprintf(line, tr("%s twice: start over"), BUTTON_C);
    add_label(line, g_font_small, 20, 458, 600, 20, g_alt_armed ? COLOR_RED : COLOR_GREY);
}

/* Where each entry of the list of menus sits. Save changes stands a little apart from the menus. */
static int settings_menu_y(int index)
{
    return 70 + index * 44 + (index == SETTINGS_MENU_COUNT ? 16 : 0);
}

/* The list of menus, or one menu's settings, with help for the entry under the cursor below. */
/* The seed's text box, over the help below the settings. */
#define SEED_BOX_Y 344

/* The help runs from SETTINGS_HELP_Y down to four lines below it, or fewer above a longer error. */
#define SETTINGS_HELP_Y 349

/* The title, and the list of menus or the open menu's settings, with what changed marked in gold. */
static void add_settings_list(void)
{
    char line[256], value[128];
    int wau, unsaved = 0;

    for (wau = 0; wau < SETTINGS_MENU_COUNT; wau++)
        if (settings_menu_changed(wau))
            unsaved = 1;
    if (unsaved)
        add_label(tr("Not saved yet"), g_font_small, 470, 22, 160, 20, COLOR_GOLD);

    if (g_settings_menu < 0) {
        add_label(tr("Slay the Scarlet: settings"), g_font_big, 20, 16, 440, 30, COLOR_WHITE);
        for (wau = 0; wau <= SETTINGS_MENU_COUNT; wau++) {
            int pos_y = settings_menu_y(wau);

            add_label(settings_menu_name(wau), g_font_big, 56, pos_y, 400, 28, COLOR_WHITE);
            if (wau < SETTINGS_MENU_COUNT && settings_menu_changed(wau))
                add_label(tr("Changed"), g_font_small, 470, pos_y + 6, 140, 20, COLOR_GOLD);
        }
        return;
    }
    sprintf(line, tr("Settings: %s"), settings_menu_name(g_settings_menu));
    add_label(line, g_font_big, 20, 16, 440, 30, COLOR_WHITE);
    for (wau = 0; wau < SETTINGS_VISIBLE_ROWS && g_settings_top + wau < g_settings_row_count; wau++) {
        const settings_row *row = &g_settings_rows[g_settings_top + wau];
        int pos_y = SETTINGS_LIST_Y + wau * SETTINGS_ROW_H;

        add_label(row->label, g_font_small, 48, pos_y, 320, 20, COLOR_WHITE);
        settings_value_text(row, value, sizeof value);
        add_label(value, g_font_small, 380, pos_y, 226, 20, settings_row_changed(row) ? COLOR_GOLD : COLOR_WHITE);
    }
}

/* Saving would change the rules of saved runs: which ones, and the choice. */
static void add_settings_confirm(void)
{
    char line[256];

    _snprintf(line, sizeof line - 1, tr("Saving changes the rules of: %s."), g_settings_affected);
    line[sizeof line - 1] = 0;
    add_help(line, 348, 2, COLOR_GOLD);
    add_help(tr("Those runs will need to start over, or carry on if that is allowed."), 383, 2, COLOR_WHITE);
    sprintf(line, tr("%s: save anyway   %s: keep editing"), BUTTON_A, BUTTON_B);
    add_label(line, g_font_small, 20, 440, 600, 20, COLOR_GREY);
}

/* The error (in red, up to two lines, ending above the key prompts) or the status line. Returns where
 * the help must end: an error of two lines takes a line from it. */
static int add_settings_error(int help_bottom)
{
    int error_lines;

    if (!g_error[0]) {
        add_label(g_status, g_font_small, 28, 416, 590, 20, COLOR_GREEN);
        return help_bottom;
    }
    error_lines = count_wrapped_lines(g_error, HELP_COLUMNS, !game_is_japanese());
    if (error_lines > 2)
        error_lines = 2;
    add_wrapped(g_error, g_font_small, 28, 416 - (error_lines - 1) * 17, 590, 17, HELP_COLUMNS, !game_is_japanese(), 2,
                COLOR_RED);
    return help_bottom - (error_lines - 1) * 17;
}

/* The seed's text box, in place of the help while it is open. */
static void add_seed_entry(void)
{
    char line[256];

    _snprintf(line, sizeof line - 1, "%s_", g_seed_entry);
    line[sizeof line - 1] = 0;
    add_label(tr("Type a seed:"), g_font_small, 36, SEED_BOX_Y + 8, 140, 20, COLOR_WHITE);
    add_label(line, g_font_big, 176, SEED_BOX_Y + 4, 420, 28, COLOR_GOLD);
    add_label(tr("Letters, digits, - and _, up to 24, and capitals count. Empty turns the seed off."), g_font_small, 36,
              SEED_BOX_Y + 38, 580, 20, COLOR_GREY);
    add_label(tr("Enter: done   Esc: cancel   Backspace: delete"), g_font_small, 20, 440, 600, 20, COLOR_GREY);
}

/* The setting under the cursor: where it is in settings.ini, its help (each value's line starting on
 * a line of its own), and the keys. */
static void add_setting_help(int help_bottom)
{
    char line[256];
    int help_y = SETTINGS_HELP_Y, wau;

    if (g_cursor < g_settings_row_count) {
        const settings_row *current = &g_settings_rows[g_cursor];

        sprintf(line, "settings.ini: [%s] %s", current->section, current->key);
        add_label(line, g_font_small, 28, 331, 590, 20, COLOR_GREY);
        for (wau = 0; wau < current->help_count; wau++)
            if (settings_help_shown(current, wau))
                help_y = add_help(current->help[wau], help_y, (help_bottom - help_y) / 17, COLOR_WHITE);
    }
    if (settings_on_seed_row())
        sprintf(line, tr("Up/Down: choose   %s: type a seed   %s: back"), BUTTON_A, BUTTON_B);
    else
        sprintf(line, tr("Up/Down: choose   Left/Right: change   %s: save   %s: back"), BUTTON_A, BUTTON_B);
    add_label(line, g_font_small, 20, 440, 600, 20, COLOR_GREY);
    sprintf(line, tr("%s: default value"), BUTTON_C);
    add_label(line, g_font_small, 20, 458, 600, 20, COLOR_GREY);
}

static void rebuild_settings(void)
{
    char line[256];
    int help_bottom = SETTINGS_HELP_Y + 4 * 17;

    add_settings_list();
    if (g_state == ST_SETTINGS_CONFIRM) {
        add_settings_confirm();
        return;
    }
    help_bottom = add_settings_error(help_bottom);
    if (g_settings_menu < 0) {
        add_help(settings_menu_help(g_cursor), 366, (help_bottom - 366) / 17, COLOR_WHITE);
        if (g_settings_leave_armed)
            sprintf(line, tr("Press %s again to leave without saving."), BUTTON_B);
        else
            sprintf(line, tr("Up/Down: choose   %s: open   %s: back"), BUTTON_A, BUTTON_B);
        add_label(line, g_font_small, 20, 440, 600, 20, g_settings_leave_armed ? COLOR_RED : COLOR_GREY);
        return;
    }
    if (seed_entry_open())
        add_seed_entry();
    else
        add_setting_help(help_bottom);
}

/* The gallery's card images, loaded once for each list of cards. A card not found in a run yet shows
 * the game's face-down card, at the size of the others. */
#define CARD_FACE_DOWN_PATH "data/infoEffect/cardFaceDown.bmp"
static Sprite g_gallery_art[STS_POOL_MAX_CARDS];
static int g_has_gallery_art[STS_POOL_MAX_CARDS];

static void load_gallery_art(void)
{
    int character = g_gallery_character < 0 ? 0 : g_gallery_character, wau;
    SokuLib::Vector2u card_size = {0, 0};

    for (wau = 0; wau < STS_POOL_MAX_CARDS; wau++) {
        g_has_gallery_art[wau] = 0;
        if (wau >= g_gallery_count)
            continue;
        if (gallery_card_shown(wau)) {
            g_has_gallery_art[wau] = load_card_sprite(&g_gallery_art[wau], character, g_gallery_cards[wau].id, 0, 0, 1);
            if (g_has_gallery_art[wau] && card_size.x == 0)
                card_size = g_gallery_art[wau].getSize();
        } else if (g_gallery_art[wau].texture.loadFromGame(CARD_FACE_DOWN_PATH)) {
            SokuLib::Vector2u size = g_gallery_art[wau].texture.getSize();

            g_gallery_art[wau].setSize(size);
            g_gallery_art[wau].rect.left = 0;
            g_gallery_art[wau].rect.top = 0;
            g_gallery_art[wau].rect.width = (int)size.x;
            g_gallery_art[wau].rect.height = (int)size.y;
            g_has_gallery_art[wau] = 1;
        } else {
            slog("could not load %s", CARD_FACE_DOWN_PATH);
        }
    }
    /* Face-down cards take the size of the cards beside them, or a card image's usual size. */
    if (card_size.x == 0) {
        Sprite probe;

        if (load_card_sprite(&probe, character, g_gallery_count > 0 ? g_gallery_cards[0].id : 0, 0, 0, 1))
            card_size = probe.getSize();
    }
    for (wau = 0; wau < g_gallery_count && card_size.x > 0; wau++)
        if (g_has_gallery_art[wau] && !gallery_card_shown(wau))
            g_gallery_art[wau].setSize(card_size);
    g_gallery_art_dirty = 0;
}

/* How many cards of a kind a character has found, or the system cards for -1. */
static int found_of_kind(int character, int kind)
{
    const sts_card_list *list = character < 0 ? &g_db.system : &g_db.characters[character];
    int count = 0, wau;

    for (wau = 0; wau < list->count; wau++)
        count += list->cards[wau].type == kind && card_found(character, list->cards[wau].id);
    return count;
}

static const char *gallery_kind_name(int kind)
{
    switch (kind) {
    case STS_CARD_SYSTEM: return tr("System cards");
    case STS_CARD_SKILL:  return tr("Skill cards");
    default:              return tr("Spell cards");
    }
}

/* How many cards of a kind a character has, or the system cards for -1. */
static int count_of_kind(int character, int kind)
{
    const sts_card_list *list = character < 0 ? &g_db.system : &g_db.characters[character];
    int count = 0, wau;

    for (wau = 0; wau < list->count; wau++)
        count += list->cards[wau].type == kind;
    return count;
}

/* The rows of the gallery's first screen. Unlock the gallery sits apart from the kinds of card. */
#define GALLERY_KIND_Y(index) (70 + (index) * 44 + ((index) == GALLERY_UNLOCK_ROW ? 16 : 0))


static void rebuild_gallery_kinds(void)
{
    char line[128];
    int kind, character;

    add_label(tr("Slay the Scarlet: gallery"), g_font_big, 20, 16, 600, 30, COLOR_WHITE);
    for (kind = 0; kind < GALLERY_KINDS; kind++) {
        int count = kind == STS_CARD_SYSTEM ? count_of_kind(-1, kind) : 0;
        int found = kind == STS_CARD_SYSTEM ? found_of_kind(-1, kind) : 0;

        for (character = 0; kind != STS_CARD_SYSTEM && character < STS_NUM_CHARACTERS; character++) {
            count += count_of_kind(character, kind);
            found += found_of_kind(character, kind);
        }
        add_label(gallery_kind_name(kind), g_font_big, 56, GALLERY_KIND_Y(kind), 400, 28, COLOR_WHITE);
        sprintf(line, tr("%d of %d found"), found, count);
        add_label(line, g_font_small, 470, GALLERY_KIND_Y(kind) + 6, 140, 20, COLOR_GREY);
    }
    /* Once every card shows, the row says why, greyed out: found in runs, or unlocked. */
    if (gallery_all_found()) {
        add_label(tr("All cards obtained. Thank you for playing!"), g_font_big, 56, GALLERY_KIND_Y(GALLERY_UNLOCK_ROW),
                  560, 28, COLOR_GREY);
    } else if (gallery_unlocked()) {
        add_label(tr("Gallery unlocked"), g_font_big, 56, GALLERY_KIND_Y(GALLERY_UNLOCK_ROW), 560, 28, COLOR_GREY);
    } else {
        add_label(tr("Unlock the gallery"), g_font_big, 56, GALLERY_KIND_Y(GALLERY_UNLOCK_ROW), 400, 28, COLOR_WHITE);
        if (g_cursor == GALLERY_UNLOCK_ROW && g_gallery_unlock_armed) {
            sprintf(line, tr("Press %s again to show every card. This cannot be undone."), BUTTON_A);
            add_label(line, g_font_small, 20, 396, 600, 20, COLOR_RED);
        } else if (g_cursor == GALLERY_UNLOCK_ROW) {
            add_label(tr("Shows every card, as if each had been found in a run."), g_font_small, 20, 396, 600, 20,
                      COLOR_GREY);
        }
    }
    sprintf(line, tr("Up/Down: choose   %s: open   %s: back"), BUTTON_A, BUTTON_B);
    add_label(line, g_font_small, 20, 440, 600, 20, COLOR_GREY);
}

static void rebuild_gallery_characters(void)
{
    char line[128];
    int character;

    add_label(g_gallery_kind == STS_CARD_SKILL ? tr("Gallery: skill cards") : tr("Gallery: spell cards"),
              g_font_big, 20, 16, 600, 30, COLOR_WHITE);
    for (character = 0; character < STS_NUM_CHARACTERS; character++) {
        int pos_x = CELL_X(character);
        int pos_y = CELL_Y(character);

        add_label(character_label(character), g_font_big, pos_x + 6, pos_y + 6, 110, 26, COLOR_WHITE);
        sprintf(line, tr("%d of %d found"), found_of_kind(character, g_gallery_kind),
                count_of_kind(character, g_gallery_kind));
        add_label(line, g_font_small, pos_x + 6, pos_y + 32, 110, 20, COLOR_GREY);
    }
    sprintf(line, tr("Arrows: choose   %s: open   %s: back"), BUTTON_A, BUTTON_B);
    add_label(line, g_font_small, 20, 440, 600, 20, COLOR_GREY);
}

/* The card under the cursor, as on the deck screen. */
static void rebuild_gallery_cards(void)
{
    char line[192];
    int character = g_gallery_character < 0 ? 0 : g_gallery_character;
    int rows = (g_gallery_count + DECK_COLS - 1) / DECK_COLS;

    if (g_gallery_art_dirty)
        load_gallery_art();
    if (g_gallery_kind == STS_CARD_SYSTEM)
        sprintf(line, "%s", tr("System cards"));
    else
        sprintf(line, g_gallery_kind == STS_CARD_SKILL ? tr("%s: skill cards") : tr("%s: spell cards"),
                character_label(g_gallery_character));
    add_label(line, g_font_big, 20, 16, 500, 30, COLOR_WHITE);
    sprintf(line, tr("%d of %d found"), found_of_kind(g_gallery_character, g_gallery_kind), g_gallery_count);
    add_label(line, g_font_small, 500, 22, 130, 20, COLOR_GREY);

    if (rows > 2)
        rows = 2;
    if (g_cursor < g_gallery_count && !gallery_card_shown(g_cursor)) {
        add_label(tr("Not found yet: this card shows once it has been offered in a run, or starts a run's deck."), g_font_small, 24,
                  DECK_Y + rows * DECK_PITCH_Y + 8, 592, 20, COLOR_GREY);
    } else if (g_cursor < g_gallery_count) {
        add_card_details(character, g_gallery_cards[g_cursor].id, DECK_Y + rows * DECK_PITCH_Y + 8);
    }
    /* Nothing to play again on a face-down card, or with card sounds off. */
    if ((g_cursor < g_gallery_count && !gallery_card_shown(g_cursor)) || !g_settings.card_sounds)
        sprintf(line, tr("Arrows: inspect   %s: back"), BUTTON_B);
    else
        sprintf(line, tr("Arrows: inspect   %s: play again   %s: back"), BUTTON_A, BUTTON_B);
    add_label(line, g_font_small, 20, 440, 600, 20, COLOR_GREY);
}

static void rebuild(void)
{
    int wau;

    ensure_fonts();
    g_label_count = 0;
    g_input_draw_count = 0;
    for (wau = 0; wau < STS_OFFER_MAX; wau++)
        g_has_art[wau] = 0;

    switch (g_state) {
    case ST_CHOOSE: rebuild_choose(); break;
    case ST_DRAFT:
    case ST_REWARD:
    case ST_ROULETTE:
        rebuild_draft();
        break;
    case ST_DECK:          rebuild_deck(); break;
    case ST_REWARD_REMOVE:
    case ST_REWARD_CONFIRM:
        rebuild_reward_remove();
        break;
    case ST_REWARD_TYPE:   rebuild_reward_type(); break;
    case ST_SWEEP:         rebuild_sweep(); break;
    case ST_RULES:         rebuild_rules(); break;
    case ST_RULES_RESTORE: rebuild_rules_restore(); break;
    case ST_SCORE:         rebuild_score(); break;
    case ST_SETTINGS:
    case ST_SETTINGS_CONFIRM:
        rebuild_settings();
        break;
    case ST_GALLERY_KINDS:      rebuild_gallery_kinds(); break;
    case ST_GALLERY_CHARACTERS: rebuild_gallery_characters(); break;
    case ST_GALLERY_CARDS:      rebuild_gallery_cards(); break;
    case ST_CLOSED:        break;
    }
    /* The clocks, under the match number or the card count. */
    {
        char text[64];
        int seconds = reward_seconds_left();

        if (seconds >= 0) {
            sprintf(text, tr("Time left: %d"), seconds);
            add_label(text, g_font_small, 500, 40, 130, 20, seconds <= 10 ? COLOR_RED : COLOR_GOLD);
        } else if ((seconds = deck_seconds_left()) >= 0) {
            sprintf(text, g_run.phase == STS_PHASE_REWARD ? tr("Next offer in %d") : tr("Closes in %d"), seconds);
            add_label(text, g_font_small, 500, 40, 130, 20, COLOR_GREY);
        }
    }
    /* The settings screen draws its own errors, which can be long. */
    if (g_state != ST_SETTINGS && g_state != ST_SETTINGS_CONFIRM)
        add_label(g_error, g_font_small, 20, 416, 600, 20, COLOR_RED);
    g_dirty = 0;
}

static void draw_box(int pos_x, int pos_y, int width, int height, unsigned fill, unsigned border)
{
    g_box.setPosition({pos_x, pos_y});
    g_box.setSize({(unsigned)width, (unsigned)height});
    g_box.setFillColor(fill);
    g_box.setBorderColor(border);
    g_box.draw();
}

/* Behind every screen: the game's deck editor background (the dark gears of character select), the
 * run character's portrait faded into it, and a shade over both so text stays easy to read. Without
 * them, the screens would show the title menu through their text, or plain black after a match. */
#define BACKGROUND_PATH "data/scene/select/bg.bmp"
#define PORTRAIT_TINT 0x48FFFFFFu      /* the portrait at about a quarter strength, a backdrop */
#define BACKGROUND_SHADE 0x60000000u

static Sprite g_background;
static Sprite g_portrait;
static int g_background_loaded = -1;   /* -1 not tried yet, then 0 or 1 */
static int g_portrait_character = -1;
static int g_has_portrait = 0;

static int load_full_screen(Sprite *sprite, const char *path)
{
    SokuLib::Vector2u size;

    if (!sprite->texture.loadFromGame(path))
        return 0;
    size = sprite->texture.getSize();
    sprite->setSize({SCREEN_W, SCREEN_H});
    sprite->rect.left = 0;
    sprite->rect.top = 0;
    sprite->rect.width = (int)size.x;
    sprite->rect.height = (int)size.y;
    sprite->setPosition({0, 0});
    return 1;
}

static void draw_background(int opaque)
{
    if (g_background_loaded < 0) {
        g_background_loaded = load_full_screen(&g_background, BACKGROUND_PATH);
        if (!g_background_loaded)
            slog("could not load %s, using a plain background", BACKGROUND_PATH);
    }
    if (!g_background_loaded) {
        draw_box(0, 0, SCREEN_W, SCREEN_H, opaque ? 0xFF000000u : 0xE0000000u, opaque ? 0xFF000000u : 0xE0000000u);
        return;
    }
    g_background.draw();

    /* The character menu and the settings are not about one character, so they have no portrait,
     * and nor does most of the gallery: only a character's own cards. */
    if (g_state != ST_CHOOSE && g_state != ST_SETTINGS && g_state != ST_SETTINGS_CONFIRM
        && g_state != ST_GALLERY_KINDS && g_state != ST_GALLERY_CHARACTERS
        && (g_state != ST_GALLERY_CARDS || g_gallery_character >= 0)) {
        int character = g_state == ST_GALLERY_CARDS ? g_gallery_character : g_run.character;

        if (g_portrait_character != character) {
            char path[96];

            sprintf(path, "data/stand/%.64s.bmp", SokuLib::getCharName((unsigned)character));
            g_has_portrait = load_full_screen(&g_portrait, path);
            g_portrait.tint = PORTRAIT_TINT;
            g_portrait_character = character;
        }
        if (g_has_portrait)
            g_portrait.draw();
    }
    draw_box(0, 0, SCREEN_W, SCREEN_H, BACKGROUND_SHADE, BACKGROUND_SHADE);
}

/* The game's card images have an empty edge: measured in game, 1 pixel on the left, 2 on the right, 1
 * at the bottom and none at the top. */
#define CARD_ART_EDGE_LEFT 1
#define CARD_ART_EDGE_RIGHT 2
#define CARD_ART_EDGE_BOTTOM 1
#define CARD_BOX_MARGIN 3

/* A box around a card in the deck grid, with an even margin around the card that can be seen. The
 * image must be at its full size. */
static void draw_box_around(Sprite *sprite, int has_art, int pos_x, int pos_y, unsigned fill, unsigned border)
{
    int width = DECK_PITCH_X - 8 - 2 * CARD_BOX_MARGIN, height = DECK_PITCH_Y - 8 - 2 * CARD_BOX_MARGIN;

    if (has_art) {
        SokuLib::Vector2u size = sprite->getSize();

        pos_x += CARD_ART_EDGE_LEFT;
        width = (int)size.x - CARD_ART_EDGE_LEFT - CARD_ART_EDGE_RIGHT;
        height = (int)size.y - CARD_ART_EDGE_BOTTOM;
    }
    draw_box(pos_x - CARD_BOX_MARGIN, pos_y - CARD_BOX_MARGIN, width + 2 * CARD_BOX_MARGIN,
             height + 2 * CARD_BOX_MARGIN, fill, border);
}

static void draw_card_box(int index, int pos_x, int pos_y, unsigned fill, unsigned border)
{
    draw_box_around(&g_deck_art[index], g_has_deck_art[index], pos_x, pos_y, fill, border);
}

/* The gallery's cards, two rows at a time as on the deck screen, scrolling with the cursor. */
static void draw_gallery_cards(void)
{
    int top = g_cursor / DECK_COLS > 0 ? g_cursor / DECK_COLS - 1 : 0;
    int wau;

    for (wau = top * DECK_COLS; wau < g_gallery_count && wau < (top + 2) * DECK_COLS; wau++) {
        int pos_x = DECK_X + (wau % DECK_COLS) * DECK_PITCH_X;
        int pos_y = DECK_Y + (wau / DECK_COLS - top) * DECK_PITCH_Y;

        if (wau == g_cursor)
            draw_box_around(&g_gallery_art[wau], g_has_gallery_art[wau], pos_x, pos_y, CURSOR_FILL, CURSOR_BORDER);
        if (g_has_gallery_art[wau]) {
            g_gallery_art[wau].tint = COLOR_WHITE;
            g_gallery_art[wau].setPosition({pos_x, pos_y});
            g_gallery_art[wau].draw();
        }
    }
}

static void draw_draft_deck(void)
{
    int first = g_run.deck.count > DRAFT_DECK_SLOTS ? g_run.deck.count - DRAFT_DECK_SLOTS : 0;
    int slots = g_settings.rules.deck_min < DRAFT_DECK_SLOTS ? g_settings.rules.deck_min : DRAFT_DECK_SLOTS;
    int wau;

    for (wau = 0; wau < DRAFT_DECK_SLOTS; wau++) {
        int index = first + wau;
        int pos_x = DRAFT_DECK_X + (wau % DRAFT_DECK_COLS) * DRAFT_DECK_PITCH_X;
        int pos_y = DRAFT_DECK_Y + (wau / DRAFT_DECK_COLS) * DRAFT_DECK_PITCH_Y;

        if (index < g_run.deck.count && index < MAX_DECK_ART && g_has_deck_art[index]) {
            g_deck_art[index].setSize({DRAFT_DECK_IMAGE_W, DRAFT_DECK_IMAGE_H});
            g_deck_art[index].tint = COLOR_WHITE;
            g_deck_art[index].setPosition({pos_x + DRAFT_DECK_IMAGE_OFFSET_X, pos_y});
            g_deck_art[index].draw();
        } else if (wau < slots) {
            draw_box(pos_x, pos_y, DRAFT_DECK_CARD_W, DRAFT_DECK_CARD_H, 0x18FFFFFFu, 0x50FFFFFFu);
        }
    }
}

/* The score screen's deck changes with the cursor, so its card images are kept by ID: the decks of
 * one run share most of their cards. */
#define SCORE_ART_MAX 64
static Sprite g_score_art[SCORE_ART_MAX];
static uint16_t g_score_art_id[SCORE_ART_MAX];
static int g_score_art_loaded[SCORE_ART_MAX];
static int g_score_art_count = 0;
static int g_score_art_character = -1;

static Sprite *score_art(uint16_t id)
{
    int wau;

    if (g_score_art_character != g_run.character) {
        g_score_art_count = 0;
        g_score_art_character = g_run.character;
    }
    for (wau = 0; wau < g_score_art_count; wau++)
        if (g_score_art_id[wau] == id)
            return g_score_art_loaded[wau] ? &g_score_art[wau] : NULL;
    if (g_score_art_count == SCORE_ART_MAX)
        return NULL;
    g_score_art_id[g_score_art_count] = id;
    g_score_art_loaded[g_score_art_count] = load_card_sprite(&g_score_art[g_score_art_count], g_run.character, id, 0, 0, 1);
    g_score_art_count++;
    return g_score_art_loaded[g_score_art_count - 1] ? &g_score_art[g_score_art_count - 1] : NULL;
}

/* The deck after the chosen match, small, with the cards that match brought in outlined. */
static void draw_score_deck(void)
{
    int match = score_entry_match(g_cursor);
    uint16_t brought_in[STS_RUN_CHANGE_MAX];
    int brought_in_count = 0, wau, card;
    sts_deck deck;

    if (!sts_run_deck_after(&g_run, match, &deck))
        return;
    if (match >= 0) {
        brought_in_count = g_run.history[match].added_count;
        memcpy(brought_in, g_run.history[match].added, sizeof brought_in);
    }
    for (wau = 0; wau < deck.count && wau < DRAFT_DECK_SLOTS; wau++) {
        int pos_x = SCORE_DECK_X + (wau % DRAFT_DECK_COLS) * DRAFT_DECK_PITCH_X;
        int pos_y = SCORE_DECK_Y + (wau / DRAFT_DECK_COLS) * DRAFT_DECK_PITCH_Y;
        Sprite *sprite = score_art(deck.ids[wau]);

        /* Each copy brought in is outlined once. */
        for (card = 0; card < brought_in_count; card++) {
            if (brought_in[card] == deck.ids[wau]) {
                brought_in[card] = brought_in[--brought_in_count];
                draw_box(pos_x - 2, pos_y - 2, DRAFT_DECK_CARD_W + 4, DRAFT_DECK_CARD_H + 4, 0x6090FF90u, 0xFF90FF90u);
                break;
            }
        }
        if (sprite) {
            sprite->setSize({DRAFT_DECK_IMAGE_W, DRAFT_DECK_IMAGE_H});
            sprite->tint = COLOR_WHITE;
            sprite->setPosition({pos_x + DRAFT_DECK_IMAGE_OFFSET_X, pos_y});
            sprite->draw();
        }
    }

    /* The cards that match took out are no longer in the deck, so they get a row of their own beside
     * it, under the key. */
    if (match < 0)
        return;
    for (card = 0; card < g_run.history[match].removed_count; card++) {
        int pos_x = SCORE_DECK_X + 340 + card * DRAFT_DECK_PITCH_X;
        int pos_y = SCORE_DECK_Y + DRAFT_DECK_PITCH_Y + 2;
        Sprite *sprite = score_art(g_run.history[match].removed[card]);

        draw_box(pos_x - 2, pos_y - 2, DRAFT_DECK_CARD_W + 4, DRAFT_DECK_CARD_H + 4, 0x60FF9090u, 0xFFFF9090u);
        if (sprite) {
            sprite->setSize({DRAFT_DECK_IMAGE_W, DRAFT_DECK_IMAGE_H});
            sprite->tint = COLOR_WHITE;
            sprite->setPosition({pos_x + DRAFT_DECK_IMAGE_OFFSET_X, pos_y});
            sprite->draw();
        }
    }
}

static void draw_cursor_box(int pos_x, int pos_y, int width, int height)
{
    draw_box(pos_x, pos_y, width, height, CURSOR_FILL, CURSOR_BORDER);
}

/* A thin bar beside a list that shows `shown` of its `total` entries from `top`: the track, and the
 * part of it the visible entries cover. Nothing when everything fits. */
static void draw_scrollbar(int pos_y, int height, int shown, int total, int top)
{
    if (total <= shown)
        return;
    draw_box(SCROLLBAR_X, pos_y, 4, height, SCROLL_TRACK, SCROLL_TRACK);
    draw_box(SCROLLBAR_X, pos_y + height * top / total, 4, height * shown / total, SCROLL_THUMB, SCROLL_THUMB);
}

static void draw_offers(void)
{
    int offer_count, wau;

    shown_offers(&offer_count);
    for (wau = 0; wau < offer_count && wau < STS_OFFER_MAX; wau++) {
        int selected = wau == g_cursor;

        draw_box(offer_x(wau, offer_count), OFFER_Y, OFFER_W, 226, selected ? CURSOR_FILL : 0x40FFFFFFu,
                 selected ? CURSOR_BORDER : 0x80FFFFFFu);
        if (g_has_art[wau])
            g_art[wau].draw();
    }
    if (draft_deck_shown())
        draw_draft_deck();
}

/* The run deck as a grid, two rows at a time (the details of the card under the cursor go below), on
 * the deck screen and the screens that take cards out of it. */
static void draw_deck_grid(void)
{
    /* The type screen's cursor is on the types, not the cards, so its grid does not scroll. */
    int on_cards = g_state != ST_REWARD_TYPE;
    int removing = g_state == ST_REWARD_REMOVE || g_state == ST_REWARD_CONFIRM || g_state == ST_SWEEP;
    int incoming = g_run.offers[g_pending_offer].id;
    int top = on_cards && g_cursor / DECK_COLS > 0 ? g_cursor / DECK_COLS - 1 : 0;
    int wau;

    for (wau = top * DECK_COLS; wau < g_run.deck.count && wau < MAX_DECK_ART && wau < (top + 2) * DECK_COLS; wau++) {
        int pos_x = DECK_X + (wau % DECK_COLS) * DECK_PITCH_X;
        int pos_y = DECK_Y + (wau / DECK_COLS - top) * DECK_PITCH_Y;
        int picked = removing && g_selected[wau];

        /* The draft screen draws these same images smaller. */
        if (g_has_deck_art[wau])
            g_deck_art[wau].setSize(g_deck_art[wau].texture.getSize());

        /* On the type screen, a soft outline on the cards of the type under the cursor that can leave,
         * so it does not compete with the cursor box. */
        if (!on_cards && g_run.deck.ids[wau] != incoming) {
            const sts_card *card = sts_card_db_find(&g_db, g_run.character, g_run.deck.ids[wau]);

            if (card && (int)card->type == g_cursor)
                draw_card_box(wau, pos_x, pos_y, 0x28FFE080u, 0x90FFE080u);
        }
        if (picked)
            draw_card_box(wau, pos_x, pos_y, 0x80FF6060u, WARNING_BORDER);
        else if (on_cards && wau == g_cursor)
            draw_card_box(wau, pos_x, pos_y, CURSOR_FILL, CURSOR_BORDER);
        if (g_has_deck_art[wau]) {
            /* Cards that cannot leave are greyed out: copies of the incoming card, or on the sweep, the
             * cards that were never in the running. */
            int blocked = g_state == ST_SWEEP ? !g_sweep_candidate[wau]
                : (removing || g_state == ST_REWARD_TYPE) && g_run.deck.ids[wau] == incoming;

            g_deck_art[wau].tint = blocked ? 0xFF505050u : COLOR_WHITE;
            g_deck_art[wau].setPosition({pos_x, pos_y});
            g_deck_art[wau].draw();
        }
    }
}

static void draw_settings_cursor(void)
{
    int row = g_cursor - g_settings_top;

    if (g_settings_menu < 0) {
        draw_cursor_box(44, settings_menu_y(g_cursor) - 6, 552, 38);
        return;
    }
    if (row >= 0 && row < SETTINGS_VISIBLE_ROWS)
        draw_cursor_box(40, SETTINGS_LIST_Y + row * SETTINGS_ROW_H - 1, 568, SETTINGS_ROW_H + 1);
    draw_scrollbar(SETTINGS_LIST_Y, SETTINGS_VISIBLE_ROWS * SETTINGS_ROW_H, SETTINGS_VISIBLE_ROWS,
                   g_settings_row_count, g_settings_top);
}

/* What each screen draws under its text: the cursor, card images, and scrollbars. */
static void draw_screen_parts(void)
{
    switch (g_state) {
    case ST_CHOOSE:
        if (g_cursor == CHOOSE_SETTINGS)
            draw_cursor_box(CHOOSE_SETTINGS_X, CHOOSE_SETTINGS_Y, 116, 32);
        else if (g_cursor == CHOOSE_GALLERY)
            draw_cursor_box(CHOOSE_GALLERY_X, CHOOSE_GALLERY_Y, 116, 32);
        else
            draw_cursor_box(CELL_X(g_cursor), CELL_Y(g_cursor), CELL_W, CELL_H);
        break;
    case ST_GALLERY_KINDS:
        draw_cursor_box(44, GALLERY_KIND_Y(g_cursor) - 6, 552, 38);
        break;
    case ST_GALLERY_CHARACTERS:
        draw_cursor_box(CELL_X(g_cursor), CELL_Y(g_cursor), CELL_W, CELL_H);
        break;
    case ST_GALLERY_CARDS:
        draw_gallery_cards();
        break;
    case ST_SETTINGS:
    case ST_SETTINGS_CONFIRM:
        draw_settings_cursor();
        if (g_state == ST_SETTINGS && seed_entry_open())
            draw_box(24, SEED_BOX_Y, 592, 64, 0xC0101010u, CURSOR_BORDER);
        break;
    case ST_RULES_RESTORE:
        draw_scrollbar(RULE_TABLE_Y, rule_table_rows() * RULE_TABLE_ROW_H, rule_table_rows(), rule_table_line_count(),
                       g_rule_table_top);
        break;
    case ST_RULES:
        draw_box(44, RULES_ROW_Y - 6 + g_cursor * RULES_ROW_PITCH, 552, 58,
                 g_rules_start_over_armed ? WARNING_FILL : CURSOR_FILL,
                 g_rules_start_over_armed ? WARNING_BORDER : CURSOR_BORDER);
        break;
    case ST_SCORE:
        draw_cursor_box(36, SCORE_LIST_Y - 3 + (g_cursor - g_score_top) * SCORE_ENTRY_H, 568, SCORE_ENTRY_H - 2);
        draw_scrollbar(SCORE_LIST_Y, SCORE_VISIBLE * SCORE_ENTRY_H, SCORE_VISIBLE, score_entry_count(), g_score_top);
        draw_score_deck();
        break;
    case ST_DRAFT:
    case ST_REWARD:
    case ST_ROULETTE:
        draw_offers();
        break;
    case ST_REWARD_TYPE:
        draw_cursor_box(44, REWARD_TYPE_Y - 6 + g_cursor * REWARD_TYPE_PITCH, 552, 38);
        draw_deck_grid();
        break;
    case ST_DECK:
    case ST_REWARD_REMOVE:
    case ST_REWARD_CONFIRM:
    case ST_SWEEP:
        draw_deck_grid();
        break;
    case ST_CLOSED:
        break;
    }
}

void screen_render(int opaque)
{
    int wau;

    if (g_dirty)
        rebuild();
    draw_background(opaque);
    draw_screen_parts();
    for (wau = 0; wau < g_label_count; wau++)
        g_labels[wau].draw();
    for (wau = 0; wau < g_input_draw_count; wau++) {
        Sprite *sprite = &g_input_sprites[g_input_draws[wau].image];

        sprite->setPosition({g_input_draws[wau].pos_x, g_input_draws[wau].pos_y});
        sprite->draw();
    }
}
