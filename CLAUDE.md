# Slay the Scarlet — Touhou Hisoutensoku (12.3) mod

A deck-building "run" mod for Touhou Hisoutensoku, inspired by Slay the Spire. The community has
previously run this format manually as an event called "Slay the Scarlet". This mod automates it.

## Concept

- A run is locked to one character. Changing character means starting a separate run.
- **Starting deck:** built with a draft tool — 20 picks, each a choice between 3 cards (1 copy each).
- **After each match:** the vanilla card-reward screen (which only lists unlocks and is usually
  skipped) is replaced with a card-offer screen: choose 1 of 3 offers, which changes the deck.
- Players then return to character select as normal and continue the set with the modified deck.
- The card unlock system and `score.dat` are **not** touched. Players are assumed to have the
  community's fully unlocked save. All run state lives in the mod's own files.
- **Later phase:** double-edged items ("relics") that give a buff and a matching debuff, e.g.
  −1000 max HP / +10% card meter gain, or +10% spirit regen delay / +10% spirit regen rate.

## Design principle: rules are settings

The community has not settled the rules, so every rule variant is a setting (mod `.ini`), with
defaults to be chosen after playtesting. Do not hardcode any of these:

- Deck size (default 20; HandicapMod proves the engine accepts other sizes) and copy limit (default 4)
- Draft length and number of offers per pick
- Reward mode: replace / add / skip / remove (deck size min/max as bounds)
- Who picks after a match: both / winner only / loser only / nobody (pure RNG)
- Replacement control: player chooses specific cards / chooses by type (system, skill, spell) / random
- Offer copy counts: fixed 1 or random (e.g. 2x A / 4x B / 1x C)
- Opponent visibility of picks: hidden / reveal after you confirm (suggested default) / live
- Opponent deck display: change only / full deck
- Items on/off; item definitions loaded from a data file, not compiled in

Offer generation must filter the card pool *before* rolling (never offer a card that would break
the copy limit or deck bounds), rather than rerolling on failure.

## Target and toolchain

- Game version: **Hisoutensoku v1.10a only.** Mods export `CheckVersion` and compare the game hash
  against `SokuLib::targetHash`. All hardcoded addresses assume 1.10a.
- English translation is the `th123e` SWRSToys module on the standard exe, so addresses are shared.
- Mod loader: **SWRSToys** (mods registered as lines in `SWRSToys.ini`).
- Rollback netcode: **Giuroll** (Giuroll-hagb fork is the maintained one). SokuRoll is outdated.
- Compiler: **MSVC, 32-bit (x86) only.** MinGW/Cygwin are not supported because `__thiscall` is
  required to call game functions. Build from the "x86 Native Tools Command Prompt".
- Build system: CMake. Project is based on SokuDev/ModTemplate, with SokuLib as a git submodule.
- The developer is comfortable in C; prefer C-style code. C++ only where SokuLib requires it.

## Build and run (Windows)

Verified working: ModTemplate builds and loads in the game through SWRSToys (it opens a "Hello
world" console window). Run everything from the **x86 Native Tools Command Prompt for VS 20XX**; a
normal terminal or the x64 prompt fails (`nmake`/`cl` not found). Check with `where cl`.

```
git submodule init && git submodule update      (once, pulls in SokuLib)
mkdir build && cd build
cmake .. -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build . --target <TargetName>
```

- Release builds: `-DCMAKE_BUILD_TYPE=Release -DFORCE_PDB=ON` (keeps a PDB for crash dumps).
- If CMake was configured in the wrong prompt, delete `build/` and reconfigure; the compiler
  choice is cached.
- Register the DLL in the game's `SWRSToys.ini` with an absolute, forward-slash path:
  `<Name>=C:/path/to/repo/build/<TargetName>.dll`. Rebuilds are picked up on the next launch.
- If the DLL does not load, check the `;` comment mark, the path, and that the exe is v1.10a
  (`CheckVersion` declines silently on any other hash).

## Repository layout (intended)

```
core/    Portable C. NO Windows or SokuLib headers. Rules engine, deck model, validation,
         offer generation, draft logic, settings parsing, run save format. Builds and is
         unit-tested on any OS (initial development is on macOS with clang).
tests/   Console tests for core/.
mod/     Windows-only DLL glue: SokuLib hooks, UI scenes, netplay, deck injection.
         Built only when MSVC is available.
```

Keep `core/` free of game dependencies so the logic can be developed and tested without the game.
Use integer maths for anything that could affect a match (determinism across machines).

## Netplay rules (critical)

Netplay is a primary requirement. Each machine simulates both players from shared inputs.

- **Deck changes happen between matches.** Decks are exchanged in the `GAME_MATCH` packet at
  character select, along with a shared random seed, so a deck change made *before* that packet is
  built reaches the opponent automatically and Giuroll is unaffected. Verify early in the debugger
  which data the packet is built from.
- **Card offers may use local RNG** (only the local player sees them). Anything affecting the
  match itself must be derived from shared data only — never local `rand()` or time.
- **Selection UI must run inside the game's frame loop** (scene/overlay), never a blocking loop, or
  network processing stops and the connection can time out. Test how long vanilla tolerates a
  player sitting on the results screen.
- **Items affect the in-match simulation**, so both machines must know both players' items before
  the first frame, effects must be per-player, and effects should be stateless or live in game
  memory so Giuroll's save/restore covers them. Avoid mod-side state that rollback can't restore.
- Mismatched mod versions or item/config files must be detected (version handshake) — they would
  otherwise desync silently.

### Modes

- **Full mode** (both players modded): all features, custom messages for picks/visibility/version.
- **Compatible mode** (only one player modded, opponent vanilla): deck rewards only. Must force:
  fixed 20 cards, max 4 copies, legal cards, no items, no in-match effects, **no packet changes or
  custom messages at all**. Inject the run deck by **writing it into a real deck slot** (dedicated
  slot, original contents backed up) before character select — NOT via a load-time hook, or the
  local game and the sent packet could disagree and desync. Opponent just sees a normal deck and
  the usual black screen while waiting.

### Spectators

- Build the selection screen as an event-driven state machine (driven by local input for players,
  by network events for spectators, local input ignored). Same screen, different driver.
- Spectators connect to the host; the host's mod must relay the client's events. Spectators must
  announce they are modded before receiving custom packets. Late joiners need a state snapshot.
- Fallback that works in every mode: diff each player's deck against the previous `GAME_MATCH`
  packet and show "P1 swapped X for Y" at match start.

## Reference material

- SokuDev org: https://github.com/SokuDev — SokuLib (reverse-engineered structures/addresses),
  ModTemplate (starting point), SokuMods (loader + many open-source mods to learn from).
- Relevant existing mods: EasyDecks (edits decks during netplay sessions), InfiniteDecks (deck
  storage), InGameHostlist / LabTool (in-game UI), FixMeiling236 (example of a determinism fix).
- A friend's netplay Practice Mode mod (shared interactive menu for both players, read-only for
  modded spectators) may be added to the repo as a reference for networking and menus.

### Findings from SokuDev/HandicapMod (`src/main.cpp`)

- Loader exports: `CheckVersion(const BYTE hash[16])`, `Initialize(HMODULE, HMODULE)` (install
  hooks, locate ini next to the DLL), `getPriority()`.
- Hooking helpers from SokuLib (no MinHook needed): `TamperDword` for vtables/imports
  (e.g. `VTable_BattleManager.onProcess` for per-frame battle logic), `TamperNearCall` /
  `TamperNearJmpOpr` for call sites, raw byte patches inside `VirtualProtect`, then
  `FlushInstructionCache`.
- **Max HP:** per-player max stored in `CharacterManager.objectBase.offset_0x186` (short).
  Patches at `0x46BCA4` (HP init), `0x481F36` (round reset → custom hook), `0x488F86` (regen cap).
  Set in a hook on player init at `0x46DE31`.
- **Card meter gain:** scaled per player (`val * mul[playerIndex] / 100`, integer maths) via
  naked asm stubs at call sites `0x468B1E` and `0x487873`.
- **Deck load:** `loadDeckData` hooked via `0x437D23` can rewrite the in-match deck (including
  size). 13-byte NOP at `0x435DA4` is likely the 20-card size check. Its use of time-seeded
  `rand()` is an example of what NOT to do for match-affecting data.
- **Extra netplay data:** hooks `ws2_32` `sendto`/`recvfrom`; appends extra values to each deck
  array in the `GAME_MATCH` packet and strips them on receive. Changes packet size, so it breaks
  vanilla peers (fine for full mode only). Needs testing alongside Giuroll.
- Profile name in netplay: `SokuLib::getNetObject().profile1name + index * 32`.

### Findings from a real install (SOKU -REWIRED- v1.5, OCE lab ver)

**Profile `.pf` deck format** (verified by parsing a real profile end to end):

- 0x6A-byte header: controller config (leading `0xFF`, 11 int32 DirectInput keyboard scancodes,
  joystick button numbers). No deck data.
- Then 80 decks: 20 characters × 4 slots, in SokuLib character order (Reimu, Marisa, Sakuya,
  Alice, Patchouli, Youmu, Remilia, Yuyuko, Yukari, Suika, Reisen, Aya, Komachi, Iku, Tenshi,
  Sanae, Cirno, Meiling, Utsuho, Suwako).
- Each deck: `u8` card count, then that many `u16` little-endian card IDs, stored sorted. The
  format allows any size up to 255.
- Card IDs: system cards 0–20 (shared by all characters); skill cards 100–111 (12 per character,
  except Patchouli who has 15: 100–114); spell cards from 200 (seen up to 219). Card type follows
  from the ID range, but keep the per-character pools in a data table rather than hardcoding the
  ranges.
- Players typically fill all 4 slots for every character, so compatible mode has no free slot:
  it must overwrite one and back it up, and the backup and restore must survive crashes.
- **Game-side validation (observed in game, using a randomizer-generated profile):** a deck with 5
  copies of one card shows as 19/20 cards in the deck editor (the extra copy is dropped), and
  starting a match with it gives **no deck at all**. So the 4-copy limit is enforced separately from
  the 20-card size check, and an illegal deck fails *silently* (no crash, no desync, just no deck).
  The mod must validate before writing a deck and read it back afterwards.
- A second, independently produced profile (see DeckRandomizer below) parses with the same layout,
  with decks written **unsorted**, and loads in game. Sort order is therefore not required by the
  reader, but write sorted decks anyway to match what the game itself produces.
- Card pools: the union of IDs seen across profiles only gives a lower bound per character (e.g. 8 of
  Yuyuko's skill cards were seen). IDs 213-219 appear for several characters, so it is unclear
  whether they are character-specific or shared. An authoritative pool table is still needed.

**Installed mods to coexist with.** Goal: the mod is standalone and works alongside the usual
community bundle; don't require players to disable other mods. Enabled in a typical install:
Giuroll + Giuroll-UI, EasyDecks, OnlinePractice, InGameHostlist, PracticeEx, MemoryPatch,
ReplayInputView, SaveRep, ScoreTracker, and several smaller fixes. HandicapMod, InfiniteDecks and
LabTool are usually disabled.

- **Always chain hooks, never overwrite.** Character select `onProcess`/`onRender` are already
  wrapped by another mod before OnlinePractice loads.
- EasyDecks edits decks during netplay and may conflict with run-deck injection. Detect it and
  handle or warn about it.

**OnlinePractice** (a friend's netplay mod; only its `.ini` and `.log` are available, not the
source). Its startup log shows the hook set for a netplay overlay mod that works under Giuroll:

| Hook (1.10a address) | Purpose |
|---|---|
| BattleManager vtable state0–7 `0x8588fc`–`0x858914`, onKO `0x858918`, render `0x858924`, inputs `0x85892c` | Per-frame battle logic |
| Character select onProcess: server `0x8574e0`, client `0x857538`; onRender: `0x8574e4` / `0x85753c` | Overlay on netplay character select (server = host, client = joining player) |
| Loading screen onProcess: server `0x857500`, client `0x857558` | Detect lock-in and count "battle N of the set" |
| `sendto`/`recvfrom` IAT `0x857290` / `0x857280` | Custom packets with a protocol version, exchanged at character select |
| Net server/client destructors, vtable `0x858cac` / `0x858d14` | Reset state when the connection closes |
| Giuroll exports `addRollbackCb` and `getVersion` (absent in 0.6.17 and older) | Register rollback callbacks so mod state is saved and restored |

Giuroll also overrides `HeapAlloc`/`HeapFree` (IAT `0x857174` / `0x857170`) to track memory for
rollback; OnlinePractice chains onto Giuroll's version. It draws its own text with a font atlas
on the D3D9 device. Its packet layout is not known; ask its author.

**OnlinePractice.dll** (binary inspected, no source). It is a 32-bit **Rust** DLL (GNU target), not
MSVC/SokuLib, so treat it as a behavioural reference only. It exports `CheckVersion` and
`Initialize`, performs a protocol-versioned handshake at the netplay character select, falls back
to normal play when the opponent's protocol differs, disables itself if it finds unexpected code in
`th123.exe`, and lets a modded spectator follow the players' state only when protocol versions
match. It warns that a packet with a vanilla pair before its marker makes spectators lose sync.
Its packet layout is not recoverable without reverse engineering; ask its author.

**DeckRandomizer.exe** (a community tool, source not in the repo; 32-bit MSVC Release console
program). Writes a complete random `output.pf` (80 decks of 20 cards, a fixed default header that
differs from a real profile in one key binding). Useful as a source of test profiles. **It can
produce illegal decks** (a 5-copy deck was observed), so do not trust its output as valid. Back up a
real profile before loading its output, because it replaces all decks and a key binding.

**Privacy:** game logs contain the opponent's IP address and local Windows paths. Never commit
logs or personal profiles. Keep reference files from the game install in a gitignored folder.

## Roadmap

1. `core/` on macOS: deck model, validation (size/copy rules from settings), offer generator with
   pool filter, draft logic, settings, run save format, `.pf` deck reader (real profiles make
   good test data), tests.
2. Windows setup (done): VS "Desktop development with C++", git, CMake; ModTemplate builds and loads
   via SWRSToys.
   2b. Early game-side experiment, before any UI: find which memory the `GAME_MATCH` packet's deck is
   built from, and where the game reads decks from the `.pf` and trims them (see open questions).
3. Draft tool in game (offline): card-offer UI, writes run deck.
4. Post-match reward screen + **compatible mode** (slot injection, no networking) — first
   playable release.
5. Full mode networking: version handshake, pick visibility, waiting state, spectator relay.
6. Items: data-driven effects (max HP, meter gain, spirit regen rate/delay first).

## Open questions

- Which replacement rule is the default (community decision).
- Starting card pool and offer pool per character (system vs character cards).
- What happens if a player picks a different character mid-run (block / end run / plain match).
- Where the game trims/validates decks (profile read, deck editor, or match load), and whether decks
  are read from the `.pf` only at startup or from memory at character select. This decides how
  compatible mode can write a slot.
- Whether the game validates decks received over netplay (copy limit and size).
- Upper deck-size limits, HUD assumptions about 20 cards, replay/spectator behaviour. The copy
  limit is a separate check from size (answered above); raising it would need its own patch, so
  keep it fixed at 4 for now.
- Authoritative per-character card pools (check SokuLib headers and EasyDecks source).
