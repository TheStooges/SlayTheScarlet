/* Drawing the mod's screens. Text comes from the game's own card files and is passed through
 * to the game's font untouched. */
#include <stdio.h>
#include <string.h>
#include "internal.h"

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
            sprintf(text, "Disable %s to play runs", mod_in_the_way);
        else if (blocked)
            sprintf(text, "%s: rules changed", sts_character_name(g_rules_changed_character));
        else if (g_active)
            sprintf(text, "In use: %s", sts_character_name(g_active_character));
        else
            sprintf(text, "No run in use");
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
        _snprintf(out, 47, "%s or Pad%d", key, pad + 1);
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

static int load_card_sprite(Sprite *sprite, uint16_t id, int pos_x, int pos_y, unsigned scale)
{
    char path[128];
    SokuLib::Vector2u size;

    if (id < 100)
        sprintf(path, "data/card/common/card%03u.bmp", (unsigned)id);
    else
        sprintf(path, "data/card/%.64s/card%03u.bmp", SokuLib::getCharName((unsigned)g_run.character), (unsigned)id);
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
    g_has_art[index] = load_card_sprite(&g_art[index], id, pos_x, pos_y, 2);
}

/* Reading card files takes a moment, so the deck images load once per deck rather than
 * on every cursor move. */
static void load_deck_art(void)
{
    int wau;

    for (wau = 0; wau < MAX_DECK_ART; wau++)
        g_has_deck_art[wau] = wau < g_run.deck.count
            && load_card_sprite(&g_deck_art[wau], g_run.deck.ids[wau],
                                DECK_X + (wau % DECK_COLS) * DECK_PITCH_X, DECK_Y + (wau / DECK_COLS) * DECK_PITCH_Y, 1);
    g_deck_art_dirty = 0;
}

/* The card's type line. Skills also show their input and button, as the game's deck screen does. */
static void add_card_kind(const sts_card *card, int pos_x, int pos_y, int width)
{
    char text[48], skill[STS_SKILL_LABEL_SIZE];

    switch (card->type) {
    case STS_CARD_SYSTEM:
        sprintf(text, "System, cost %d", card->cost_or_slot);
        break;
    case STS_CARD_SPELL:
        sprintf(text, "Spell, cost %d", card->cost_or_slot);
        break;
    case STS_CARD_SKILL:
        if (sts_skill_label(&g_db, g_run.character, card->id, skill)) {
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

static void rebuild_choose(void)
{
    int character;
    char line[128];

    add_label("Slay the Scarlet: choose a character", g_font_big, 20, 16, 600, 30, COLOR_WHITE);
    if (g_settings.starter_enabled)
        add_label("Starter decks are on: each draft begins with the [starter] cards from settings.ini.",
                  g_font_small, 20, 414, 600, 20, COLOR_GOLD);
    if (blocking_mod()) {
        sprintf(line, "%s can overwrite your real decks with a run deck, so runs are off.", blocking_mod());
        add_label(line, g_font_small, 20, 372, 600, 20, COLOR_RED);
        sprintf(line, "Disable it in SWRSToys.ini and restart the game to play runs.");
        add_label(line, g_font_small, 20, 392, 600, 20, COLOR_RED);
    }
    for (character = 0; character < STS_NUM_CHARACTERS; character++) {
        int pos_x = 24 + (character % COLS) * 120;
        int pos_y = 80 + (character / COLS) * 70;
        unsigned color = COLOR_WHITE;

        add_label(sts_character_name(character), g_font_big, pos_x + 6, pos_y + 6, 110, 26, COLOR_WHITE);
        line[0] = 0;
        if (g_has_save[character]) {
            if (g_saved_rules_changed[character]) {
                sprintf(line, "Rules changed");
                color = COLOR_RED;
            } else if (g_saved_phase[character] == STS_PHASE_DRAFT) {
                sprintf(line, "Draft %d/%d", g_saved_picks[character],
                        sts_settings_draft_length(&g_settings, character));
                color = COLOR_GOLD;
            } else if (g_saved_phase[character] == STS_PHASE_REWARD) {
                sprintf(line, "Reward waiting");
                color = COLOR_GOLD;
            } else if (g_active && g_active_character == character) {
                sprintf(line, "In use");
                color = COLOR_GREEN;
            } else {
                sprintf(line, "Deck ready");
                color = COLOR_GREY;
            }
        }
        add_label(line, g_font_small, pos_x + 6, pos_y + 32, 110, 20, color);
    }
    if (g_alt_armed)
        sprintf(line, "Press %s again to discard this character's run and start over.", BUTTON_C);
    else
        sprintf(line, "%s: open   %s: close   %s twice: start over", BUTTON_A, BUTTON_B, BUTTON_C);
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

static void rebuild_draft(void)
{
    char line[192];
    int wau;

    if (draft_deck_shown()) {
        if (g_deck_art_dirty)
            load_deck_art();
        add_label("Your deck so far", g_font_small, DRAFT_DECK_X, DRAFT_DECK_Y - 24, 320, 20, COLOR_GREY);
    }

    if (g_state == ST_REWARD) {
        const char *verb = g_settings.reward.mode == STS_REWARD_ADD ? "add"
                         : g_settings.reward.mode == STS_REWARD_REMOVE ? "remove" : "swap in";
        sprintf(line, "%s: choose a card to %s", sts_character_name(g_run.character), verb);
        add_label(line, g_font_big, 20, 16, 470, 30, COLOR_WHITE);
        sprintf(line, "Match %d", g_run.matches_played);
        add_label(line, g_font_small, 500, 22, 130, 20, COLOR_GREY);
    } else {
        sprintf(line, "%s: pick %d of %d", sts_character_name(g_run.character),
                g_run.picks_done + 1, sts_settings_draft_length(&g_settings, g_run.character));
        add_label(line, g_font_big, 20, 16, 400, 30, COLOR_WHITE);
        sprintf(line, "Deck %d/%d", g_run.deck.count, g_settings.rules.deck_min);
        add_label(line, g_font_small, 500, 22, 130, 20, COLOR_GREY);
    }

    for (wau = 0; wau < g_run.offer_count && wau < STS_OFFER_MAX; wau++) {
        const sts_offer *offer = &g_run.offers[wau];
        const card_text *entry = find_text(offer->id);
        const sts_card *card = sts_card_db_find(&g_db, g_run.character, offer->id);
        int pos_x = offer_x(wau, g_run.offer_count);

        load_art(wau, offer->id, pos_x + 8, OFFER_Y + 8);
        add_wrapped(entry ? entry->name : "?", g_font_small, pos_x + 8, OFFER_Y + 146, OFFER_W - 16, 17, 22, 1, 3,
                    COLOR_WHITE);
        if (card)
            add_card_kind(card, pos_x + 8, OFFER_Y + 202, OFFER_W - 16);
        if (offer->copies > 1) {
            sprintf(line, "x%d", offer->copies);
            add_label(line, g_font_big, pos_x + OFFER_W - 40, OFFER_Y + 8, 36, 28, COLOR_GOLD);
        }
    }
    if (g_cursor < g_run.offer_count) {
        const card_text *entry = find_text(g_run.offers[g_cursor].id);
        /* The game's own descriptions are laid out in rows of exactly this many bytes. */
        if (entry)
            add_wrapped(entry->desc, g_font_small, 24, OFFER_Y + 236, 592, 17, 36, 0, 6, COLOR_WHITE);
    }
    if (g_state == ST_REWARD)
        sprintf(line, "Left/Right: choose   %s: take", BUTTON_A);
    else
        sprintf(line, "Left/Right: choose   %s: take   %s: back (progress is saved)", BUTTON_A, BUTTON_B);
    add_label(line, g_font_small, 20, 440, 600, 20, COLOR_GREY);
}

/* Name, type and description of one card, starting at y. */
static void add_card_details(uint16_t id, int pos_y)
{
    const card_text *entry = find_text(id);
    const sts_card *card = sts_card_db_find(&g_db, g_run.character, id);

    /* A skill's arrows sit above its type line, so they need room under the name. */
    int kind_y = pos_y + (card && card->type == STS_CARD_SKILL ? 46 : 28);

    add_label(entry ? entry->name : "?", g_font_big, 24, pos_y, 592, 28, COLOR_WHITE);
    if (card)
        add_card_kind(card, 24, kind_y, 592);
    if (entry)
        add_wrapped(entry->desc, g_font_small, 24, kind_y + 20, 592, 17, 36, 0, 6, COLOR_WHITE);
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
        const sts_card *card = sts_card_db_find(&g_db, g_run.character, list[wau].id);
        char prefix[STS_SKILL_LABEL_SIZE + 8] = "";
        char text[NAME_SIZE + 32];

        /* "a22 Taste of Death", "4-cost Death Sign ...": what matters most about each type. */
        if (card && card->type == STS_CARD_SKILL) {
            char skill[STS_SKILL_LABEL_SIZE];
            if (sts_skill_label(&g_db, g_run.character, card->id, skill))
                sprintf(prefix, "%s ", skill);
        } else if (card && card->type == STS_CARD_SPELL) {
            sprintf(prefix, "%d-cost ", card->cost_or_slot);
        }
        if (list[wau].copies > 1)
            _snprintf(text, sizeof text - 1, "%s%s x%d", prefix, entry ? entry->name : "?", list[wau].copies);
        else
            _snprintf(text, sizeof text - 1, "%s%s", prefix, entry ? entry->name : "?");
        text[sizeof text - 1] = 0;
        add_label(text, g_font_small, pos_x + 8, pos_y, 340, 20, COLOR_WHITE);
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
        add_label("Choose what leaves your deck", g_font_big, 20, 16, 600, 30, COLOR_WHITE);
        add_label("Confirm?", g_font_big, 520, 432, 110, 30, COLOR_GOLD);
        if (g_cursor < g_run.deck.count)
            add_card_details(g_run.deck.ids[g_cursor], DECK_Y + 2 * DECK_PITCH_Y + 8);
        add_change_list(DECK_Y + 2 * DECK_PITCH_Y + 8, "Removing", "Adding");
        sprintf(line, "%s: confirm   %s: change the last pick", BUTTON_A, BUTTON_B);
        add_label(line, g_font_small, 20, 440, 600, 20, COLOR_GREY);
        return;
    }
    add_label("Choose what leaves your deck", g_font_big, 20, 16, 600, 30, COLOR_WHITE);
    if (g_cursor < g_run.deck.count)
        add_card_details(g_run.deck.ids[g_cursor], DECK_Y + 2 * DECK_PITCH_Y + 8);
    if (g_cursor < g_run.deck.count && g_run.deck.ids[g_cursor] == offer->id)
        add_label("This is the card you are taking, so it cannot leave.", g_font_small, 20, 396, 600, 20, COLOR_RED);
    sprintf(line, "Replacing with %dx %.90s. Choose %d more.", offer->copies, entry ? entry->name : "?", remaining);
    add_label(line, g_font_small, 20, 416, 600, 20, COLOR_GOLD);
    sprintf(line, "Arrows: choose   %s: select   %s: back to the offers", BUTTON_A, BUTTON_B);
    add_label(line, g_font_small, 20, 440, 600, 20, COLOR_GREY);
}

static void rebuild_rules(void)
{
    char line[192];
    int wau;

    sprintf(line, "%s: the rules have changed", sts_character_name(g_run.character));
    add_label(line, g_font_big, 20, 16, 600, 30, COLOR_WHITE);
    add_label("This run was made under different rules from the current settings.",
              g_font_small, 40, 80, 580, 20, COLOR_WHITE);
    add_label(g_rules_problem, g_font_small, 40, 106, 580, 20, COLOR_RED);

    for (wau = 0; wau < rules_option_count(); wau++) {
        int pos_y = RULES_ROW_Y + wau * RULES_ROW_PITCH;
        const char *note = NULL;

        switch (rules_option_at(wau)) {
        case RULES_CARRY_ON:
            add_label("Carry on under the current rules", g_font_big, 56, pos_y, 500, 28, COLOR_WHITE);
            note = "Your deck and progress stay as they are.";
            break;
        case RULES_BACK:
            add_label("Back", g_font_big, 56, pos_y, 500, 28, COLOR_WHITE);
            note = "Leaves the run as it is.";
            break;
        case RULES_START_OVER:
            add_label("Start over", g_font_big, 56, pos_y, 500, 28, g_rules_start_over_armed ? COLOR_RED : COLOR_WHITE);
            note = "Discards this run's deck and progress.";
            break;
        }
        add_label(note, g_font_small, 72, pos_y + 26, 500, 20, COLOR_GREY);
    }
    if (g_rules_start_over_armed)
        sprintf(line, "Press %s again to discard this run.   %s: cancel", BUTTON_A, BUTTON_B);
    else
        sprintf(line, "Up/Down: choose   %s: select   %s: back", BUTTON_A, BUTTON_B);
    add_label(line, g_font_small, 20, 440, 600, 20, g_rules_start_over_armed ? COLOR_RED : COLOR_GREY);
}

static void rebuild_reward_type(void)
{
    static const char *const names[3] = { "System cards", "Skill cards", "Spell cards" };
    char line[192];
    const sts_offer *offer = &g_run.offers[g_pending_offer];
    const card_text *entry = find_text(offer->id);
    unsigned mask = sts_replace_types_available(&g_db, g_run.character, &g_run.deck, offer->id, offer->copies);
    int type;

    add_label("Choose what type leaves your deck", g_font_big, 20, 16, 600, 30, COLOR_WHITE);
    for (type = 0; type < 3; type++) {
        /* Copies of the incoming card are not counted, as they cannot leave. */
        int count = sts_replace_count_of_type(&g_db, g_run.character, &g_run.deck, offer->id, type);

        sprintf(line, "%s (%d can leave)", names[type], count);
        add_label(line, g_font_big, 56, 110 + type * 50, 500, 28,
                  (mask & STS_TYPE_BIT(type)) ? COLOR_WHITE : COLOR_GREY);
    }
    sprintf(line, "Replacing with %dx %.90s. Random cards of that type leave.",
            offer->copies, entry ? entry->name : "?");
    add_label(line, g_font_small, 20, 416, 600, 20, COLOR_GOLD);
    sprintf(line, "Up/Down: choose   %s: confirm   %s: back to the offers", BUTTON_A, BUTTON_B);
    add_label(line, g_font_small, 20, 440, 600, 20, COLOR_GREY);
}

static void rebuild_deck(void)
{
    char line[192];
    int rows = (g_run.deck.count + DECK_COLS - 1) / DECK_COLS;
    int pos_y;

    if (g_deck_art_dirty)
        load_deck_art();

    sprintf(line, "%s: your deck", sts_character_name(g_run.character));
    add_label(line, g_font_big, 20, 16, 400, 30, COLOR_WHITE);
    sprintf(line, "%d cards", g_run.deck.count);
    add_label(line, g_font_small, 540, 22, 90, 20, COLOR_GREY);

    /* Details of the card under the cursor, below however many rows the deck needs. */
    if (rows > 2)
        rows = 2;
    pos_y = DECK_Y + rows * DECK_PITCH_Y + 8;
    if (g_cursor < g_run.deck.count)
        add_card_details(g_run.deck.ids[g_cursor], pos_y);
    add_change_list(pos_y, "Removed", "Added");
    add_label(g_status, g_font_small, 20, 396, 600, 20, run_deck_in_use() ? COLOR_GREEN : COLOR_GOLD);
    sprintf(line, "%s: %s", BUTTON_D, run_deck_in_use() ? "stop using this run" : "use this run");
    add_label(line, g_font_small, 20, 416, 600, 20, COLOR_WHITE);
    if (g_alt_armed)
        sprintf(line, "Press %s again to discard this run and start over.", BUTTON_C);
    else
        sprintf(line, "Arrows: inspect   %s: back   %s twice: start over", BUTTON_B, BUTTON_C);
    add_label(line, g_font_small, 20, 440, 600, 20, g_alt_armed ? COLOR_RED : COLOR_GREY);
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
        rebuild_draft();
        break;
    case ST_DECK:          rebuild_deck(); break;
    case ST_REWARD_REMOVE:
    case ST_REWARD_CONFIRM:
        rebuild_reward_remove();
        break;
    case ST_REWARD_TYPE:   rebuild_reward_type(); break;
    case ST_RULES:         rebuild_rules(); break;
    case ST_CLOSED:        break;
    }
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

void screen_render(int opaque)
{
    int wau;

    if (g_dirty)
        rebuild();

    draw_box(0, 0, SCREEN_W, SCREEN_H, opaque ? 0xFF000000u : 0xE0000000u, opaque ? 0xFF000000u : 0xE0000000u);
    if (g_state == ST_CHOOSE) {
        int pos_x = 24 + (g_cursor % COLS) * 120;
        int pos_y = 80 + (g_cursor / COLS) * 70;
        draw_box(pos_x, pos_y, 116, 60, 0x60FFE080u, 0xFFFFE080u);
    } else if (g_state == ST_REWARD_TYPE) {
        draw_box(44, 104 + g_cursor * 50, 552, 38, 0x60FFE080u, 0xFFFFE080u);
    } else if (g_state == ST_RULES) {
        draw_box(44, RULES_ROW_Y - 6 + g_cursor * RULES_ROW_PITCH, 552, 58,
                 g_rules_start_over_armed ? 0x60FF6060u : 0x60FFE080u,
                 g_rules_start_over_armed ? 0xFFFF6060u : 0xFFFFE080u);
    } else if (g_state == ST_DRAFT || g_state == ST_REWARD) {
        for (wau = 0; wau < g_run.offer_count && wau < STS_OFFER_MAX; wau++) {
            int selected = wau == g_cursor;
            draw_box(offer_x(wau, g_run.offer_count), OFFER_Y, OFFER_W, 226,
                     selected ? 0x60FFE080u : 0x40FFFFFFu, selected ? 0xFFFFE080u : 0x80FFFFFFu);
            if (g_has_art[wau])
                g_art[wau].draw();
        }
        if (draft_deck_shown())
            draw_draft_deck();
    } else if (g_state == ST_DECK || g_state == ST_REWARD_REMOVE || g_state == ST_REWARD_CONFIRM) {
        /* Two rows fit above the card details. A larger deck scrolls with the cursor. */
        int top = g_cursor / DECK_COLS > 0 ? g_cursor / DECK_COLS - 1 : 0;

        for (wau = top * DECK_COLS; wau < g_run.deck.count && wau < MAX_DECK_ART && wau < (top + 2) * DECK_COLS; wau++) {
            int pos_x = DECK_X + (wau % DECK_COLS) * DECK_PITCH_X;
            int pos_y = DECK_Y + (wau / DECK_COLS - top) * DECK_PITCH_Y;

            int picked = (g_state == ST_REWARD_REMOVE || g_state == ST_REWARD_CONFIRM) && g_selected[wau];

            /* The draft screen draws these same images smaller. */
            if (g_has_deck_art[wau])
                g_deck_art[wau].setSize(g_deck_art[wau].texture.getSize());

            if (wau == g_cursor || picked) {
                /* An even margin around the image, whatever size the game's art is. */
                SokuLib::Vector2u size = g_has_deck_art[wau] ? g_deck_art[wau].getSize()
                                                           : SokuLib::Vector2u{DECK_PITCH_X - 8, DECK_PITCH_Y - 8};

                draw_box(pos_x - 3, pos_y - 3, (int)size.x + 6, (int)size.y + 6,
                         picked ? 0x80FF6060u : 0x60FFE080u, picked ? 0xFFFF6060u : 0xFFFFE080u);
            }
            if (g_has_deck_art[wau]) {
                /* Copies of the incoming card are greyed out on the removal screens. */
                int blocked = (g_state == ST_REWARD_REMOVE || g_state == ST_REWARD_CONFIRM)
                    && g_run.deck.ids[wau] == g_run.offers[g_pending_offer].id;

                g_deck_art[wau].tint = blocked ? 0xFF505050u : COLOR_WHITE;
                g_deck_art[wau].setPosition({pos_x, pos_y});
                g_deck_art[wau].draw();
            }
        }
    }
    for (wau = 0; wau < g_label_count; wau++)
        g_labels[wau].draw();
    for (wau = 0; wau < g_input_draw_count; wau++) {
        Sprite *sprite = &g_input_sprites[g_input_draws[wau].image];

        sprite->setPosition({g_input_draws[wau].pos_x, g_input_draws[wau].pos_y});
        sprite->draw();
    }
}
