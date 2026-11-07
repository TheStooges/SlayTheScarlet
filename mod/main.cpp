/* Slay the Scarlet: DLL entry points and scene hooks. */
#include <SokuLib.hpp>
#include <windows.h>
#include <shlwapi.h>
#include <string.h>
#include "screen.h"

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
/* The reward is done and the Result menu is being told to close. */
static int g_result_closing = 0;

static int game_has_focus(void)
{
    DWORD pid = 0;

    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

static int __fastcall on_title_process(SokuLib::Title *This)
{
    int f7 = (GetAsyncKeyState(VK_F7) & 0x8000) != 0;
    int pressed = f7 && !g_f7_prev;

    g_f7_prev = f7;
    g_result_closing = 0;
    deck_on_title_frame();
    /* Only from the bare main menu, not with a submenu such as Options on top. */
    if (pressed && !screen_is_open() && game_has_focus() && SokuLib::menuManager.empty())
        screen_open();

    if (screen_is_open()) {
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

    if (screen_is_open())
        screen_render(0);
    else if (SokuLib::menuManager.empty())
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
        if (SokuLib::inputMgrs.input.a || SokuLib::inputMgrs.input.b)
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
    if (!g_result_closing) {
        deck_on_result_frame();
        if (deck_take_result_skip()) {
            g_result_closing = 1;
            g_wait_for_release = 1;
        }
    }

    if (screen_is_open()) {
        screen_update(SokuLib::inputMgrs.input);
        if (!screen_is_open()) {
            g_result_closing = 1;
            g_wait_for_release = 1;
        }
        return 1;   /* the menu stays */
    }
    if (g_result_closing) {
        /* Leave through the game's own exit path by pressing its confirm button for it. */
        SokuLib::KeyInput saved = SokuLib::inputMgrs.input;
        int result;

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

extern "C" __declspec(dllexport) bool CheckVersion(const BYTE hash[16])
{
    return memcmp(hash, SokuLib::targetHash, sizeof(SokuLib::targetHash)) == 0;
}

extern "C" __declspec(dllexport) bool Initialize(HMODULE module, HMODULE parent)
{
    char dir[MAX_PATH];
    DWORD old;

    (void)parent;
    GetModuleFileNameA(module, dir, MAX_PATH);
    PathRemoveFileSpecA(dir);
    PathAddBackslashA(dir);
    screen_set_dir(dir);

    VirtualProtect((PVOID)RDATA_SECTION_OFFSET, RDATA_SECTION_SIZE, PAGE_EXECUTE_WRITECOPY, &old);
    g_og_title_process = SokuLib::TamperDword(&SokuLib::VTable_Title.onProcess, on_title_process);
    g_og_title_render = SokuLib::TamperDword(&SokuLib::VTable_Title.onRender, on_title_render);
    g_og_select_process = SokuLib::TamperDword(&SokuLib::VTable_Select.onProcess, on_select_process);
    g_og_select_render = SokuLib::TamperDword(&SokuLib::VTable_Select.onRender, on_select_render);
    g_og_battle_process = SokuLib::TamperDword(&SokuLib::VTable_Battle.onProcess, on_battle_process);
    g_og_result_process = SokuLib::TamperDword(&g_vtable_match_end.onProcess, on_result_process);
    g_og_result_render = SokuLib::TamperDword(&g_vtable_match_end.onRender, on_result_render);
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
