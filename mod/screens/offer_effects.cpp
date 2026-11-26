/* The effects behind an offer's card, by rarity: Remilia's flames from Millennium Vampire behind
 * uncommon cards, and Tenshi's burst from State of Enlightenment behind rare ones. Remilia Scarlet
 * for the mod's name, and Tenshi for the weather of Scarlet Weather Rhapsody. Where the images are and
 * how the game draws them is in docs/findings.md (card effect sprites). They are 24-bit with black
 * for nothing, so they are drawn additive, as the game does. */
#include <d3d9.h>
#include <math.h>
#include "internal.h"

/* The timings are the game's, from the effect probe. Sizes are the game's times about 0.4, which makes
 * a character about the size of the card, held back where an image would reach well past the box. */

/* Remilia: 20 images of 2 frames each, looping, fading in over the first 26 frames. */
#define FLAME_IMAGES 20
#define FLAME_FRAMES_EACH 2
#define FLAME_FADE_IN 26
#define FLAME_SCALE 1.3f
#define FLAME_ALPHA 0xE0u
#define FLAME_RISE 8             /* pixels the flames sit above the card's centre */

/* Tenshi: on appearing, a glow pulsing between two sizes (25 frames), a ring (42 frames) and 12 sparks
 * (50 frames), each fading out; and lightning, 4 images of 2 frames each, once every 28 frames. */
#define LIGHTNING_IMAGES 4
#define LIGHTNING_FRAMES_EACH 2
#define LIGHTNING_CYCLE 28
#define LIGHTNING_SCALE 1.2f
#define GLOW_FRAMES 25
#define RING_FRAMES 42
#define SPARK_FRAMES 50
#define SPARKS 12

static Sprite g_flames[FLAME_IMAGES];
static Sprite g_lightning[LIGHTNING_IMAGES];
static Sprite g_glow, g_ring, g_spark;
static int g_loaded = -1;   /* -1 not tried yet, then 0 or 1 */
/* Picks each loop of the lightning's way round. Only for show, so local, and new each time offers appear. */
static uint32_t g_flip_seed = 0;

static int load_image(Sprite *sprite, const char *path)
{
    SokuLib::Vector2u size;

    if (!sprite->texture.loadFromGame(path)) {
        slog("offer effects: could not load %s", path);
        return 0;
    }
    size = sprite->texture.getSize();
    sprite->rect.left = 0;
    sprite->rect.top = 0;
    sprite->rect.width = (int)size.x;
    sprite->rect.height = (int)size.y;
    return 1;
}

static int load_images(void)
{
    char path[96];
    int wau, ok = 1;

    for (wau = 0; wau < FLAME_IMAGES; wau++) {
        format_text(path, "data/character/remilia/bulletHa%03d.bmp", wau);
        ok &= load_image(&g_flames[wau], path);
    }
    for (wau = 0; wau < LIGHTNING_IMAGES; wau++) {
        format_text(path, "data/character/tenshi/spellBulletDa%03d.bmp", wau);
        ok &= load_image(&g_lightning[wau], path);
    }
    ok &= load_image(&g_glow, "data/character/tenshi/objectDd000.bmp");
    ok &= load_image(&g_ring, "data/character/tenshi/objectDe000.bmp");
    ok &= load_image(&g_spark, "data/character/tenshi/bulletCc001.bmp");
    return ok;
}

/* Draws an image centred on a point, scaled, with this much of its light (0 to 255). */
static void draw_image(Sprite *sprite, int center_x, int center_y, float scale, unsigned alpha)
{
    SokuLib::Vector2u size = sprite->texture.getSize();
    unsigned width = (unsigned)(size.x * scale), height = (unsigned)(size.y * scale);

    if (alpha == 0 || width == 0 || height == 0)
        return;
    sprite->setSize({width, height});
    sprite->setPosition({center_x - (int)width / 2, center_y - (int)height / 2});
    sprite->tint = ((alpha > 255 ? 255 : alpha) << 24) | 0xFFFFFFu;
    sprite->draw();
}

/* As draw_image, mirrored left to right and flipped upside down as asked: the texture's rectangle
 * runs backwards, so no other images are needed. */
static void draw_image_turned(Sprite *sprite, int center_x, int center_y, float scale, unsigned alpha, int mirror,
                              int flip)
{
    SokuLib::Vector2u size = sprite->texture.getSize();

    sprite->rect.left = mirror ? (int)size.x : 0;
    sprite->rect.width = mirror ? -(int)size.x : (int)size.x;
    sprite->rect.top = flip ? (int)size.y : 0;
    sprite->rect.height = flip ? -(int)size.y : (int)size.y;
    draw_image(sprite, center_x, center_y, scale, alpha);
}

/* Something that lasts `length` frames, `time` frames in: from `full` down to nothing. */
static unsigned fade(unsigned full, unsigned time, unsigned length)
{
    return time >= length ? 0 : full * (length - time) / length;
}

static void draw_flames(int center_x, int center_y, unsigned shown)
{
    unsigned alpha = shown >= FLAME_FADE_IN ? FLAME_ALPHA : FLAME_ALPHA * shown / FLAME_FADE_IN;

    draw_image(&g_flames[(shown / FLAME_FRAMES_EACH) % FLAME_IMAGES], center_x, center_y - FLAME_RISE,
               FLAME_SCALE, alpha);
}

static void draw_burst(int center_x, int center_y, unsigned shown, unsigned offer)
{
    unsigned pulse = (shown / 2) % 2, lightning = shown % LIGHTNING_CYCLE;
    int wau;

    /* The glow pulses between 5 and 6 times its size in the game, the ring between full and three
     * quarters strength. */
    draw_image(&g_glow, center_x, center_y, pulse ? 1.4f : 1.2f, fade(0xF5u, shown, GLOW_FRAMES));
    draw_image(&g_ring, center_x, center_y, 0.8f, fade(pulse ? 0xC0u : 0xF9u, shown, RING_FRAMES));
    for (wau = 0; wau < SPARKS && shown < SPARK_FRAMES; wau++) {
        float angle = (float)wau * 6.2831853f / SPARKS;
        float radius = 12.0f + 1.2f * (float)shown;

        draw_image(&g_spark, center_x + (int)(cosf(angle) * radius), center_y + (int)(sinf(angle) * radius * 0.8f),
                   0.6f, fade(0xFAu, shown, SPARK_FRAMES));
    }
    /* Each loop of the lightning comes one of four ways round, at random: as drawn, mirrored, upside
     * down, or both, each offer its own way. The timing stays the same on all of them. */
    if (lightning < LIGHTNING_IMAGES * LIGHTNING_FRAMES_EACH) {
        uint32_t way = (shown / LIGHTNING_CYCLE + 1) * 2654435761u ^ (offer + 1) * 40503u ^ g_flip_seed;

        way = (way ^ (way >> 15)) * 2246822519u;
        way ^= way >> 13;
        draw_image_turned(&g_lightning[lightning / LIGHTNING_FRAMES_EACH], center_x, center_y, LIGHTNING_SCALE, 0xFFu,
                          (int)(way & 1), (int)((way >> 1) & 1));
    }
}

/* The spells the effects come from, for their sounds: each sound plays as its effect appears, as in the
 * game (Millennium Vampire's 17 frames in, with the flames; State of Enlightenment's 78 frames in,
 * with the burst). */
#define REMILIA 6
#define MILLENNIUM_VAMPIRE 208
#define TENSHI 14
#define STATE_OF_ENLIGHTENMENT 206

void offer_effect_sound(int tier)
{
    g_flip_seed = GetTickCount() * 2654435761u;
    if (!card_sounds_on())
        return;
    if (tier == STS_RARITY_UNCOMMON)
        card_sounds_start(REMILIA, MILLENNIUM_VAMPIRE, STS_CARD_SPELL, 0, 0);
    else if (tier == STS_RARITY_RARE)
        card_sounds_start(TENSHI, STATE_OF_ENLIGHTENMENT, STS_CARD_SPELL, 0, 0);
}

void draw_offer_effect(int tier, int center_x, int center_y, unsigned shown, unsigned offer)
{
    IDirect3DDevice9 *device = SokuLib::pd3dDev;
    DWORD blend_on = 0, source = 0, destination = 0;

    if (tier != STS_RARITY_UNCOMMON && tier != STS_RARITY_RARE)
        return;
    if (g_loaded < 0)
        g_loaded = load_images();
    if (!g_loaded || !device)
        return;
    device->GetRenderState(D3DRS_ALPHABLENDENABLE, &blend_on);
    device->GetRenderState(D3DRS_SRCBLEND, &source);
    device->GetRenderState(D3DRS_DESTBLEND, &destination);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
    if (tier == STS_RARITY_UNCOMMON)
        draw_flames(center_x, center_y, shown);
    else
        draw_burst(center_x, center_y, shown, offer);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, blend_on);
    device->SetRenderState(D3DRS_SRCBLEND, source);
    device->SetRenderState(D3DRS_DESTBLEND, destination);
}
