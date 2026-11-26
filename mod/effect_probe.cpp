/* A probe for debug builds only: which sprites a card effect is drawn from, to draw two of them behind
 * rare offers (roadmap item 15): Remilia's Millennium Vampire and Tenshi's State of Enlightenment.
 *
 * In Practice mode it writes effects.log, beside the mod: each player's action as it changes, and each
 * distinct frame any of their objects shows (action, sequence, pose, the image's index in the
 * character's pattern file, the rectangle taken from it, blend and colour, and where it is from the
 * player). An image index is a line of data/character/<name>/<name>.pat's image list, which the
 * pattern file names; the log does not hold the names. Nothing about the player or the machine is
 * written. Release builds leave it out. */
#include <stdio.h>
#include <string.h>
#include "internal.h"
#include "effect_probe.h"

#ifdef NDEBUG

void effect_probe_frame(void)
{
}

#else

#define SEEN_MAX 8192
#define OBJECTS_MAX 512

static FILE *g_file = NULL;
static uint32_t g_seen[SEEN_MAX];     /* hashes of frames already written, 0 for none */
static int g_seen_count = 0;
static int g_full_noted = 0;
static unsigned g_last_frame_count = 0;
static unsigned g_player_action[2];
static unsigned g_player_render[2];
/* The objects seen on the previous frame: each one's action, the frame it appeared and its last pose. */
typedef struct {
    void *object;
    unsigned action;
    unsigned born;
    unsigned pose;   /* sequence << 16 | pose */
} seen_object;

static seen_object g_objects[2][OBJECTS_MAX];
static int g_object_count[2];

/* The objects of the card effects drawn behind offers (docs/findings.md): every pose they show is
 * written, repeats too, and when each one ends, to get their timing. */
static int watched(unsigned action)
{
    return action == 856 || action == 858;
}

static uint32_t mix(uint32_t hash, uint32_t value)
{
    hash ^= value;
    return hash * 16777619u;
}

/* 1 the first time this hash comes up. */
static int first_time(uint32_t hash)
{
    int slot;

    if (hash == 0)
        hash = 1;
    if (g_seen_count >= SEEN_MAX / 2) {
        if (!g_full_noted)
            fprintf(g_file, "-- too many distinct frames; no more are written\n");
        g_full_noted = 1;
        return 0;
    }
    for (slot = (int)(hash % SEEN_MAX); g_seen[slot]; slot = (slot + 1) % SEEN_MAX)
        if (g_seen[slot] == hash)
            return 0;
    g_seen[slot] = hash;
    g_seen_count++;
    return 1;
}

static void reset(void)
{
    memset(g_seen, 0, sizeof g_seen);
    g_seen_count = 0;
    g_full_noted = 0;
    memset(g_player_action, 0xFF, sizeof g_player_action);
    memset(g_player_render, 0xFF, sizeof g_player_render);
    memset(g_object_count, 0, sizeof g_object_count);
}

static int open_file(void)
{
    char path[MAX_PATH + 32];

    if (g_file)
        return 1;
    format_text(path, "%seffects.log", g_dir);
    g_file = fopen(path, "w");
    if (!g_file)
        return 0;
    fprintf(g_file, "Effect probe. Frames are game frames; positions are from the player, in pixels.\n");
    return 1;
}

static void write_object_frame(int side, unsigned frame, SokuLib::v2::Player *player, SokuLib::v2::GameObject *object,
                               int is_new)
{
    SokuLib::v2::FrameData *data = object->frameData;
    const SokuLib::v2::BlendOptions *blend = data ? data->blendOptionsPtr : NULL;
    uint32_t hash = 2166136261u;

    if (!data)
        return;
    hash = mix(hash, (uint32_t)side);
    hash = mix(hash, object->frameState.actionId);
    hash = mix(hash, object->frameState.sequenceId);
    hash = mix(hash, object->frameState.poseId);
    hash = mix(hash, (uint32_t)(unsigned short)data->texIndex);
    hash = mix(hash, ((uint32_t)(unsigned short)data->texOffset.x << 16) | (unsigned short)data->texOffset.y);
    hash = mix(hash, ((uint32_t)(unsigned short)data->texSize.x << 16) | (unsigned short)data->texSize.y);
    if (!is_new && !first_time(hash))
        return;
    if (is_new)
        first_time(hash);
    fprintf(g_file,
            "%u P%d %s object action %u seq %u pose %u: image %d rect %d,%d %dx%d offset %d,%d, %d frames, group %d, "
            "blend %u color %08X scale %.2f,%.2f rot %.0f, tint %08X, at %+.0f,%+.0f%s\n",
            frame, side + 1, is_new ? "new" : "   ", object->frameState.actionId, object->frameState.sequenceId,
            object->frameState.poseId, data->texIndex, data->texOffset.x, data->texOffset.y, data->texSize.x,
            data->texSize.y, data->offset.x, data->offset.y, data->duration, (int)data->renderGroup,
            blend ? (unsigned)blend->mode : 0u, blend ? blend->color : 0xFFFFFFFFu, blend ? blend->scale.x : 1.0f,
            blend ? blend->scale.y : 1.0f, blend ? blend->rotateZ : 0.0f, (unsigned)object->renderInfos.color.color,
            object->position.x - player->position.x, object->position.y - player->position.y,
            object->textures == player->textures ? "" : ", own images");
}

static void probe_side(int side, unsigned frame)
{
    SokuLib::BattleManager &manager = SokuLib::getBattleMgr();
    SokuLib::v2::Player *player = (SokuLib::v2::Player *)(side ? &manager.rightCharacterManager : &manager.leftCharacterManager);
    seen_object now[OBJECTS_MAX];
    int now_count = 0, wau, j;
    unsigned action = ((unsigned)player->frameState.actionId << 16) | player->frameState.sequenceId;
    unsigned render = (unsigned)player->renderInfos.color.color ^ ((unsigned)player->renderInfos.shaderType << 24)
                    ^ (unsigned)player->renderInfos.shaderColor.color;

    if (action != g_player_action[side]) {
        fprintf(g_file, "%u P%d %s action %u seq %u\n", frame, side + 1, SokuLib::getCharName(player->characterIndex),
                player->frameState.actionId, player->frameState.sequenceId);
        g_player_action[side] = action;
    }
    if (render != g_player_render[side]) {
        fprintf(g_file, "%u P%d player tint %08X shader %d color %08X\n", frame, side + 1,
                (unsigned)player->renderInfos.color.color, player->renderInfos.shaderType,
                (unsigned)player->renderInfos.shaderColor.color);
        g_player_render[side] = render;
    }
    if (!player->objectList)
        return;
    for (auto object : player->objectList->getList()) {
        const seen_object *before = NULL;
        unsigned pose;

        if (!object)
            continue;
        pose = ((unsigned)object->frameState.sequenceId << 16) | object->frameState.poseId;
        for (wau = 0; wau < g_object_count[side]; wau++)
            if (g_objects[side][wau].object == object && g_objects[side][wau].action == object->frameState.actionId)
                before = &g_objects[side][wau];
        if (now_count < OBJECTS_MAX) {
            now[now_count].object = object;
            now[now_count].action = object->frameState.actionId;
            now[now_count].born = before ? before->born : frame;
            now[now_count].pose = pose;
            now_count++;
        }
        write_object_frame(side, frame, player, object, before == NULL);
        if (before && before->pose != pose && watched(object->frameState.actionId))
            fprintf(g_file, "%u P%d  watch action %u seq %u pose %u, %u frames after it appeared\n", frame, side + 1,
                    object->frameState.actionId, object->frameState.sequenceId, object->frameState.poseId,
                    frame - before->born);
    }
    /* The watched objects that are gone. */
    for (wau = 0; wau < g_object_count[side]; wau++) {
        const seen_object *old = &g_objects[side][wau];
        int still = 0;

        for (j = 0; j < now_count; j++)
            if (now[j].object == old->object && now[j].action == old->action)
                still = 1;
        if (!still && watched(old->action))
            fprintf(g_file, "%u P%d  watch action %u seq %u ended after %u frames\n", frame, side + 1, old->action,
                    old->pose >> 16, frame - old->born);
    }
    memcpy(g_objects[side], now, sizeof(seen_object) * (size_t)now_count);
    g_object_count[side] = now_count;
}

static int probe_frame(void)
{
    unsigned frame = SokuLib::getBattleMgr().frameCount;

    if (frame < g_last_frame_count) {
        reset();
        fprintf(g_file, "-- new battle\n");
    }
    if (frame == g_last_frame_count && frame != 0)
        return 1;   /* paused, or called twice */
    g_last_frame_count = frame;
    probe_side(0, frame);
    probe_side(1, frame);
    fflush(g_file);
    return 1;
}

void effect_probe_frame(void)
{
    if (SokuLib::mainMode != SokuLib::BATTLE_MODE_PRACTICE)
        return;
    if (!g_file) {
        if (!open_file())
            return;
        reset();
    }
    __try {
        probe_frame();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        fprintf(g_file, "-- could not read the battle this frame\n");
    }
}

#endif
