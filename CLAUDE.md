# Slay the Scarlet - Touhou Hisoutensoku (12.3) mod

A deck-building "run" mod for Touhou Hisoutensoku, inspired by Slay the Spire. The community has
previously run this format manually as an event called "Slay the Scarlet". This mod automates it.

## Concept

- A run is locked to one character. Changing character means starting a separate run.
- **Starting deck:** built with a draft tool - 20 picks, each a choice between 3 cards (1 copy each).
- **After each match:** the vanilla card-reward screen (which only lists unlocks and is usually
  skipped) is replaced with a card-offer screen: choose 1 of 3 offers, which changes the deck.
- Players then return to character select as normal and continue the set with the modified deck.
- The card unlock system and `score.dat` are **not** touched. Players are assumed to have the
  community's fully unlocked save. All run state lives in the mod's own files.
- **Self-contained module, by decision:** the mod must work as a SWRSToys module on a normal install.
  The original tournament (start from default decks, collect cards through the game's unlock screen,
  rebuild freely) needed a separately modded install and was hard to get working, so it is background
  only and not a mode to reproduce. Do not add a card collection or deck-building screen for it.
- **Later phase:** double-edged items ("relics") that give a buff and a matching debuff, e.g.
  -1000 max HP / +10% card meter gain, or +10% spirit regen delay / +10% spirit regen rate.

## Design principle: rules are settings

The community has not settled the rules, so every rule variant is a setting (mod `.ini`), with
defaults to be chosen after playtesting. Do not hardcode any of these:

- Deck size (default 20; HandicapMod proves the engine accepts other sizes) and copy limit (default 4)
- Number of offers per draft pick (the draft itself is fixed: one card per pick until the deck
  reaches `size_min`, so 20 picks, or 12 with the starter; decided for v0.3)
- Reward mode: replace / add / skip / remove (deck size min/max as bounds)
- When you get an offer after a match: win or lose / only if you win / only if you lose / at random
- Which offered card comes in: player chooses / random
- Replacement control: player chooses specific cards / chooses by type (system, skill, spell) / random
- Offer copy counts: fixed 1 or random (e.g. 2x A / 4x B / 1x C)
- Opponent visibility of picks: dropped by decision (2026-11-25). Seeing the changes afterwards
  (`[netplay] share_changes`, v0.6) is enough; live picks would be much work for little gain.
- Opponent deck display: change only / full deck. Kept, undecided; perhaps only at the end of a run,
  beside the run endings of item 12 in docs/roadmap.md.
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
- **Readable names:** no one-letter names or cryptic abbreviations for variables, parameters or struct
  fields. The first loop index is `wau` (not `i`); `j` and `k` are fine as nested ones, and `id`, `db` and `ok` are fine as words.
- **ASCII only** in every file (source, comments, docs, ini, CMake): no em dashes, curly quotes, arrows
  or math symbols. The developer's Japanese-locale Windows misreads UTF-8 as Shift-JIS and shows
  garbled text, and MSVC reads source files in the system code page. The one exception, by
  decision: translations in `data/lang/` are UTF-8 (the mod converts them to Shift-JIS on load).
- **On-screen text** goes through `tr("...")` with the English as a string literal, never built from
  pieces (Japanese word order differs), so each line can be translated whole. A test fails if
  `data/lang/ja.txt` lacks any `tr` literal in `mod/`, or keeps one no longer used. Logs stay English.
- **No unbounded string writes in `mod/`:** never `sprintf`, `strcpy` or `strcat`. Use `format_text`
  (an array, whose size the compiler knows) or `format_bounded` (a pointer and its size), from
  `mod/format.h`: text that does not fit is cut short. Translations, profile names, card names,
  install paths and network data can all be longer than expected. Before a release, run the tests
  under AddressSanitizer and the static analyser (see `docs/building.md`).

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

- Release builds: `-DCMAKE_BUILD_TYPE=Release`. They keep a separate PDB for reading crash dumps; keep
  the one matching each release. Finished releases go in the gitignored `release/` folder.
- Making a release: `tools\build_release.bat` (tests, Release DLL, checks on its imports), then
  `python tools\package_release.py <version>` (zip and debug files in `release/`, refused with
  uncommitted changes, scanned for local paths and the user name). Attach only the zip to the GitHub
  release, as a pre-release while it is a test.
- If CMake was configured in the wrong prompt, delete `build/` and reconfigure; the compiler
  choice is cached.
- Register the DLL in the game's `SWRSToys.ini` with an absolute, forward-slash path:
  `<Name>=C:/path/to/repo/build/<TargetName>.dll`. Rebuilds are picked up on the next launch.
- If the DLL does not load, check the `;` comment mark, the path, and that the exe is v1.10a
  (`CheckVersion` declines silently on any other hash).

## Repository layout

```
core/          Portable C. NO Windows or SokuLib headers. Rules engine, deck model, validation,
               card table, offer generation, draft logic, settings parsing, run save format.
               Builds and is unit-tested on any OS (initial development was on macOS with clang).
tests/         Console tests for core/, one file per module (test_deck.c for deck.c, and so on).
mod/           Windows-only DLL, built only when MSVC is available. At the top: entry points and
               scene hooks (main.cpp), what every part shares (internal.h, log, text, card_text,
               card_sounds, embedded).
  run/         The run deck in the game: writing it into the slot, run data, finished matches.
  screens/     The mod's screens: state and input (screen.cpp), drawing (screen_draw.cpp),
               settings, gallery, and the score screen's history.
  net/         Online: packet hooks and logging (netlog), the handshake and delivery (peers),
               deck changes for players and spectators (spectator).
  lib/         The DirectX import libraries.
data/          Data files: cards.csv (the card table, no text), card_sounds.csv, settings.ini (every
               setting and its default), lang/ (translations of the mod's text, UTF-8).
docs/          design.md, findings.md, roadmap.md, building.md, and the README's images.
tools/         Release scripts (build_release.bat, package_release.py).
lib/           SokuLib, as a git submodule. Run: git submodule update --init
```

Headers in `mod/` are included by name alone (`#include "peers.h"`): every `mod/` folder is on the
include path. A new source file goes in the CMake list in `mod/CMakeLists.txt`, and CMake must run
again for the translation test to see it.

Keep `core/` free of game dependencies so the logic can be developed and tested without the game.
Use integer maths for anything that could affect a match (determinism across machines).

## Netplay rules (critical)

Netplay is a primary requirement. Each machine simulates both players from shared inputs.

- **Deck changes happen between matches.** Decks are exchanged in the `GAME_MATCH` packet at
  character select, along with a shared random seed, so a deck change made *before* that packet is
  built reaches the opponent automatically and Giuroll is unaffected. Verify early in the debugger
  which data the packet is built from.
- **Card offers may use local RNG** (only the local player sees them). Anything affecting the
  match itself must be derived from shared data only - never local `rand()` or time.
- **Selection UI must run inside the game's frame loop** (scene/overlay), never a blocking loop, or
  network processing stops and the connection can time out. Test how long vanilla tolerates a
  player sitting on the results screen.
- **Items affect the in-match simulation**, so both machines must know both players' items before
  the first frame, effects must be per-player, and effects should be stateless or live in game
  memory so Giuroll's save/restore covers them. Avoid mod-side state that rollback can't restore.
- Mismatched mod versions or item/config files must be detected (version handshake) - they would
  otherwise desync silently.

### Modes

- **Full mode** (both players modded): all features, custom messages for picks/visibility/version.
- **Compatible mode** (only one player modded, opponent vanilla): deck rewards only. Must force:
  fixed 20 cards, max 4 copies, legal cards, no items, no in-match effects, **no packet changes or
  custom messages at all**. Inject the run deck by **writing it into a real deck slot** (dedicated
  slot, original contents backed up) before character select - NOT via a load-time hook, or the
  local game and the sent packet could disagree and desync. Opponent just sees a normal deck and
  the usual black screen while waiting.

### Spectators

- Build the selection screen as an event-driven state machine (driven by local input for players,
  by network events for spectators, local input ignored). Same screen, different driver.
- Spectators hang under the client, not the host (per the protocol doc in docs/findings.md: the host has one child,
  the client; the client one, the first spectator; each spectator up to 4 more). So the host's
  events must pass through the client's mod, then down the spectator tree. Spectators must announce
  they are modded before receiving custom packets. Late joiners need a state snapshot.
- Fallback that works in every mode: diff each player's deck against the previous `GAME_MATCH`
  packet and show "P1 swapped X for Y" at match start.

## More documentation

Read these when the task needs them; they are not loaded automatically.

- `docs/design.md`: how the rules, offers and run saves behave, and why (rule interpretations,
  save formats, player 2).
- `docs/findings.md`: reverse-engineering findings (addresses, scenes, packets, Giuroll, the real
  install, other mods). Check here before probing the game again.
- `docs/roadmap.md`: the roadmap, its agreed order, what each item built and decided, and the open
  questions.
- `docs/building.md`: building, installing for testing, and making a release.
