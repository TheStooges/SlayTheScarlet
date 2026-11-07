/* Slay the Scarlet: DLL entry points and scene hooks. */
#include <SokuLib.hpp>
#include <windows.h>
#include <shlwapi.h>
#include <string.h>
#include "screen.h"

static int (SokuLib::Title::*g_og_title_process)();
static int (SokuLib::Title::*g_og_title_render)();
static int (SokuLib::Select::*g_og_select_process)();
static int (SokuLib::Battle::*g_og_battle_process)();
static int g_f7_prev = 0;

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
        screen_render();
    return result;
}

static int __fastcall on_select_process(SokuLib::Select *This)
{
    deck_on_select_frame();
    return (This->*g_og_select_process)();
}

static int __fastcall on_battle_process(SokuLib::Battle *This)
{
    deck_on_battle_frame();
    return (This->*g_og_battle_process)();
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
    g_og_battle_process = SokuLib::TamperDword(&SokuLib::VTable_Battle.onProcess, on_battle_process);
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
