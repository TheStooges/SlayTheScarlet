/* Slay the Scarlet: DLL entry points and scene hooks. */
#include <SokuLib.hpp>
#include <windows.h>
#include <shlwapi.h>
#include <string.h>
#include "netlog.h"
#include "log.h"
#include "match.h"
#include "run_deck.h"
#include "screen.h"

/* The reward is done and the Result menu is being told to close. */
static int g_result_closing = 0;

/* Set when only logging: see Initialize. */
static int g_observer = 0;

/* Records each change in how far the players are through character select: when a character gets
 * picked, and whether the confirm button was down at that moment. */
static void log_select_stage(const SokuLib::Select &select)
{
    static int last_left = -1;
    static int last_right = -1;

    if (select.leftSelectionStage == last_left && select.rightSelectionStage == last_right)
        return;
    last_left = select.leftSelectionStage;
    last_right = select.rightSelectionStage;
    slog("select stage: left %d, right %d (confirm button %d)", last_left, last_right,
         (int)SokuLib::inputMgrs.input.a);
}

/* Netplay scenes keep running exactly as normal. Pausing one, as offline character select does
 * for a late reward, could stall the connection. */
#define NET_SELECT_HOOK(Type, name) \
    static int (SokuLib::Type::*g_og_##Type)(); \
    static int __fastcall on_##Type(SokuLib::Type *This) \
    { \
        int result; \
        netlog_scene(name); \
        g_result_closing = 0; \
        if (!g_observer) \
            deck_on_net_select_frame(); \
        result = (This->*g_og_##Type)(); \
        log_select_stage(This->base); \
        return result; \
    }
#define NET_LOADING_HOOK(Type, name) \
    static int (SokuLib::Type::*g_og_##Type)(); \
    static int __fastcall on_##Type(SokuLib::Type *This) \
    { \
        netlog_scene(name); \
        return (This->*g_og_##Type)(); \
    }
#define NET_BATTLE_HOOK(Type, name) \
    static int (SokuLib::Type::*g_og_##Type)(); \
    static int __fastcall on_##Type(SokuLib::Type *This) \
    { \
        netlog_scene(name); \
        deck_on_battle_frame(); \
        if (SokuLib::menuManager.empty()) \
            g_result_closing = 0; \
        return (This->*g_og_##Type)(); \
    }

NET_SELECT_HOOK(SelectServer, "character select, as host")
NET_SELECT_HOOK(SelectClient, "character select, as client")
NET_LOADING_HOOK(LoadingServer, "loading, as host")
NET_LOADING_HOOK(LoadingClient, "loading, as client")
/* A spectator's scenes: only logged for now, to learn when it waits and when it watches. A spectator
 * waits on its loading screen while the players are on the screen after a match and character
 * select, and is sometimes sent back to the battle to catch up on the last one. */
NET_LOADING_HOOK(LoadingWatch, "loading, as spectator")
NET_LOADING_HOOK(BattleWatch, "battle, as spectator")
NET_BATTLE_HOOK(BattleServer, "battle, as host")
NET_BATTLE_HOOK(BattleClient, "battle, as client")

static int (SokuLib::Title::*g_og_title_process)();
static int (SokuLib::Title::*g_og_title_render)();
static int (SokuLib::Select::*g_og_select_process)();
static int (SokuLib::Select::*g_og_select_render)();
static int (SokuLib::Battle::*g_og_battle_process)();
static int (SokuLib::MenuResult::*g_og_result_process)();
static int (SokuLib::MenuResult::*g_og_result_render)();
static int g_f7_prev = 0;

/* The screen after a match that lists the game's card unlocks: class CMenuEnd, found from
 * the game's RTTI and its use of data/menu/result/result.dat. SokuLib's VTable_Result is a
 * different screen, the Result item on the title menu. */
static auto &g_vtable_match_end = *reinterpret_cast<SokuLib::Menu_VTABLE<SokuLib::MenuResult> *>(0x00859528);

/* Character select stays paused until the button that closed the screen is released, or
 * the same press would also pick a character. */
static int g_wait_for_release = 0;
/* What character select returns to stay in its scene. Its return value is the next scene, so
 * only a value seen on two frames running is safe: the frame that starts a match returns
 * the loading scene once. */
static int g_select_stay = SokuLib::SCENE_SELECT;
static int g_select_previous = -1;
static int game_has_focus(void)
{
    DWORD pid = 0;

    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

static void install_connect_hooks(void);

static int __fastcall on_title_process(SokuLib::Title *This)
{
    int f7 = (GetAsyncKeyState(VK_F7) & 0x8000) != 0;
    int pressed = f7 && !g_f7_prev;

    g_f7_prev = f7;
    install_connect_hooks();
    g_result_closing = 0;
    deck_on_title_frame();
    /* Only from the bare main menu, not with a submenu such as Options on top. */
    if (pressed && !screen_is_open() && game_has_focus() && SokuLib::menuManager.empty())
        screen_open();

    /* A game menu over the title is drawn on top of the screen and would hide it. The network
     * menu, which the game opens after a disconnect during an offer, shows the screen in its
     * place (on_connect_process). Under any other menu the screen waits, reading no buttons,
     * so presses meant for the menu cannot take an offer unseen. */
    if (screen_is_open() && SokuLib::menuManager.empty()) {
        /* The menu keeps running but sees no input. The real values go back afterwards,
         * because the game counts how long buttons are held in these same fields. */
        SokuLib::KeyInput saved = SokuLib::inputMgrs.input;
        int result;

        screen_update(saved);
        memset(&SokuLib::inputMgrs.input, 0, sizeof SokuLib::inputMgrs.input);
        result = (This->*g_og_title_process)();
        SokuLib::inputMgrs.input = saved;
        return result;
    }
    return (This->*g_og_title_process)();
}

static int __fastcall on_title_render(SokuLib::Title *This)
{
    int result = (This->*g_og_title_render)();

    if (!SokuLib::menuManager.empty())
        return result;
    if (screen_is_open())
        screen_render(0);
    else
        screen_render_title_hint();
    return result;
}

static int __fastcall on_select_process(SokuLib::Select *This)
{
    int result;

    g_result_closing = 0;
    deck_on_select_frame();

    /* Character select updates its keys inside its own frame function, so its input cannot
     * be blanked from out here. It is paused instead, and the screen reads the input the
     * game's menus use, which is updated outside the scene. */
    if (screen_is_open()) {
        screen_update(SokuLib::inputMgrs.input);
        if (!screen_is_open())
            g_wait_for_release = 1;
        return g_select_stay;
    }
    if (g_wait_for_release) {
        /* Esc too, which closes the screen and on character select would also leave it. */
        if (SokuLib::inputMgrs.input.a || SokuLib::inputMgrs.input.b || (GetAsyncKeyState(VK_ESCAPE) & 0x8000))
            return g_select_stay;
        g_wait_for_release = 0;
    }
    result = (This->*g_og_select_process)();
    if (result == g_select_previous)
        g_select_stay = result;
    g_select_previous = result;
    return result;
}

static int __fastcall on_select_render(SokuLib::Select *This)
{
    int result = (This->*g_og_select_render)();

    if (screen_is_open())
        screen_render(0);
    return result;
}

static int __fastcall on_battle_process(SokuLib::Battle *This)
{
    deck_on_battle_frame();
    if (SokuLib::menuManager.empty())
        g_result_closing = 0;
    return (This->*g_og_battle_process)();
}

/* The Result menu lists the game's own card unlocks. When a card offer is due, the offer
 * takes its place. */
static int __fastcall on_result_process(SokuLib::MenuResult *This)
{
    netlog_scene("the screen after a match");
    if (!g_result_closing) {
        deck_on_result_frame();
        if (deck_take_result_skip())
            g_result_closing = 1;
    }

    if (screen_is_open()) {
        screen_update(SokuLib::inputMgrs.input);
        if (!screen_is_open())
            g_result_closing = 1;
        return 1;   /* the menu stays */
    }
    if (g_result_closing) {
        /* Leave through the game's own exit path by pressing its confirm button for it. That
         * waits for the player to let go first, or the same press carries on into the next
         * screen. Online character select cannot be paused to wait for it there. */
        SokuLib::KeyInput saved = SokuLib::inputMgrs.input;
        int result;

        if (saved.a || saved.b)
            return 1;

        SokuLib::inputMgrs.input.a = 1;
        result = (This->*g_og_result_process)();
        SokuLib::inputMgrs.input = saved;
        deck_log_result_close(result);
        return result;
    }
    return (This->*g_og_result_process)();
}

static int __fastcall on_result_render(SokuLib::MenuResult *This)
{
    if (screen_is_open()) {
        screen_render(1);
        return 1;
    }
    if (g_result_closing)
        return 1;   /* keeps the unlock list from flashing up on the way out */
    return (This->*g_og_result_render)();
}

/* A disconnect during an offer sends the game to the title with its network menu open. The
 * offer is shown in the menu's place, and the menu waits, frozen, until the screen closes.
 *
 * InGameHostlist draws its host list and its own menu items from Direct3D's EndScene, after
 * everything else, whenever the top menu's vtable is the network menu's. So while the offer is
 * open the menu object points at a copy of that vtable instead: the game still runs it, through
 * the copy, but other mods no longer see a network menu. The real vtable goes back when the
 * screen closes, and those mods start again as if the menu had just opened. */
static int (SokuLib::MenuConnect::*g_og_connect_process)();
static int (SokuLib::MenuConnect::*g_og_connect_render)();
static int g_connect_wait_for_release = 0;
static void **const g_connect_vtable = reinterpret_cast<void **>(&SokuLib::VTable_ConnectMenu);
/* More entries than the menu's 4 virtual functions, in case the class has others unknown. The
 * copy matches the original at every index it holds. */
static void *g_connect_hidden_vtable[32];

static void set_connect_vtable(SokuLib::MenuConnect *menu, void **vtable)
{
    *reinterpret_cast<void ***>(menu) = vtable;
}

static int __fastcall on_connect_process(SokuLib::MenuConnect *This)
{
    if (screen_is_open()) {
        set_connect_vtable(This, g_connect_hidden_vtable);
        screen_update(SokuLib::inputMgrs.input);
        if (!screen_is_open()) {
            set_connect_vtable(This, g_connect_vtable);
            g_connect_wait_for_release = 1;
        }
        return 1;   /* the menu stays */
    }
    set_connect_vtable(This, g_connect_vtable);
    /* Or the press that closed the screen would also choose an item in the menu. */
    if (g_connect_wait_for_release) {
        if (SokuLib::inputMgrs.input.a || SokuLib::inputMgrs.input.b)
            return 1;
        g_connect_wait_for_release = 0;
    }
    return (This->*g_og_connect_process)();
}

static int __fastcall on_connect_render(SokuLib::MenuConnect *This)
{
    if (screen_is_open()) {
        screen_render(1);
        return 1;
    }
    return (This->*g_og_connect_render)();
}

/* Hooked on the first title frame, after every mod has loaded, so this hook is the outermost
 * and an open offer stops all of the others' network menu code, not just the game's. */
static void install_connect_hooks(void)
{
    static int installed = 0;
    DWORD old;

    if (installed)
        return;
    installed = 1;
    VirtualProtect((PVOID)RDATA_SECTION_OFFSET, RDATA_SECTION_SIZE, PAGE_EXECUTE_WRITECOPY, &old);
    g_og_connect_process = SokuLib::TamperDword(&SokuLib::VTable_ConnectMenu.onProcess, on_connect_process);
    g_og_connect_render = SokuLib::TamperDword(&SokuLib::VTable_ConnectMenu.onRender, on_connect_render);
    VirtualProtect((PVOID)RDATA_SECTION_OFFSET, RDATA_SECTION_SIZE, old, &old);
    memcpy(g_connect_hidden_vtable, g_connect_vtable, sizeof g_connect_hidden_vtable);
    slog("network menu hooked over %p and %p", *(void **)&g_og_connect_process, *(void **)&g_og_connect_render);
}

extern "C" __declspec(dllexport) bool CheckVersion(const BYTE hash[16])
{
    return memcmp(hash, SokuLib::targetHash, sizeof(SokuLib::targetHash)) == 0;
}

extern "C" __declspec(dllexport) bool Initialize(HMODULE module, HMODULE parent)
{
    char dir[MAX_PATH];
    char observer_path[MAX_PATH + 16];
    int observer;
    DWORD old;

    (void)parent;
    GetModuleFileNameA(module, dir, MAX_PATH);
    PathRemoveFileSpecA(dir);
    PathAddBackslashA(dir);
    screen_set_dir(dir);
    log_init();

    /* Development aid: with observer.txt beside the DLL, only netplay is logged. No screens, no
     * deck writes, so an install can stand in for a vanilla opponent and still show its side. */
    strcpy(observer_path, dir);
    strcat(observer_path, "observer.txt");
    observer = PathFileExistsA(observer_path);
    g_observer = observer;
    slog("started%s", observer ? " as a log-only observer" : "");

    VirtualProtect((PVOID)RDATA_SECTION_OFFSET, RDATA_SECTION_SIZE, PAGE_EXECUTE_WRITECOPY, &old);
    if (!observer) {
        g_og_title_process = SokuLib::TamperDword(&SokuLib::VTable_Title.onProcess, on_title_process);
        g_og_title_render = SokuLib::TamperDword(&SokuLib::VTable_Title.onRender, on_title_render);
        g_og_select_process = SokuLib::TamperDword(&SokuLib::VTable_Select.onProcess, on_select_process);
        g_og_select_render = SokuLib::TamperDword(&SokuLib::VTable_Select.onRender, on_select_render);
        g_og_battle_process = SokuLib::TamperDword(&SokuLib::VTable_Battle.onProcess, on_battle_process);
        g_og_result_process = SokuLib::TamperDword(&g_vtable_match_end.onProcess, on_result_process);
        g_og_result_render = SokuLib::TamperDword(&g_vtable_match_end.onRender, on_result_render);
    }
    g_og_SelectServer = SokuLib::TamperDword(&SokuLib::VTable_SelectServer.onProcess, on_SelectServer);
    g_og_SelectClient = SokuLib::TamperDword(&SokuLib::VTable_SelectClient.onProcess, on_SelectClient);
    g_og_LoadingServer = SokuLib::TamperDword(&SokuLib::VTable_LoadingServer.onProcess, on_LoadingServer);
    g_og_LoadingClient = SokuLib::TamperDword(&SokuLib::VTable_LoadingClient.onProcess, on_LoadingClient);
    g_og_BattleServer = SokuLib::TamperDword(&SokuLib::VTable_BattleServer.onProcess, on_BattleServer);
    g_og_BattleClient = SokuLib::TamperDword(&SokuLib::VTable_BattleClient.onProcess, on_BattleClient);
    g_og_LoadingWatch = SokuLib::TamperDword(&SokuLib::VTable_LoadingWatch.onProcess, on_LoadingWatch);
    g_og_BattleWatch = SokuLib::TamperDword(&SokuLib::VTable_BattleWatch.onProcess, on_BattleWatch);
    netlog_install();
    VirtualProtect((PVOID)RDATA_SECTION_OFFSET, RDATA_SECTION_SIZE, old, &old);
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    return true;
}

extern "C" int APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved)
{
    (void)module;
    (void)reason;
    (void)reserved;
    return TRUE;
}

extern "C" __declspec(dllexport) int getPriority()
{
    return 0;
}
