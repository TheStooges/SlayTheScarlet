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
- **ASCII only** in every file (source, comments, docs, ini, CMake): no em dashes, curly quotes, arrows
  or math symbols. The developer's Japanese-locale Windows misreads UTF-8 as Shift-JIS and shows
  garbled text, and MSVC reads source files in the system code page.

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
         card table, offer generation, draft logic, settings parsing, run save format.
         Builds and is unit-tested on any OS (initial development was on macOS with clang).
data/    Data files the mod loads at runtime. cards.csv is the card table (no text).
tests/   Console tests for core/, one file per module.
mod/     Windows-only DLL glue: SokuLib hooks, UI scenes, netplay, deck injection.
         Built only when MSVC is available.
lib/     SokuLib, as a git submodule. Run: git submodule update --init
```

The "probe" in the findings below was a throwaway logging mod, since removed. Its results are all
recorded here, and online logging now lives in `mod/netlog.cpp`.

`core/` modules: `deck` (model, validation), `pf` (profile read/write), `pool` (card table), `rng`
(PCG32), `offer` (offer generator, applying a taken offer), `settings` (`.ini` parsing, validation,
compatible-mode override, fingerprint), `run` (draft and reward state machine, save format), `util`.
`data/settings.ini` documents every setting and states the defaults.

By decision, every card is in the offer pool, including Catfish and the two rarity 0 spells. The game's
unlock rules (rarity, sequences, story rewards) do not limit the pool, because players use a fully
unlocked save.

Rule interpretations in `core/` that may need revisiting with the community:

- `who_picks`: a player left out (loser under `winner`, winner under `loser`) keeps their deck
  unchanged. `nobody` rolls an offer and any removals at random for both players.
- The draft ends early if the deck fills up. A draft with `copies_min` above 1 must use a fixed copy
  count, because a varying one could stop just short of `size_min` with no room for another pick.
- `copy_limit` is refused above 4 until the game's limit is patched (an illegal deck silently means
  no deck in game).
- REPLACE counts the copy limit before removing anything, so it stays legal whatever is removed.

Runs save their random state and any offers on screen, so reloading cannot reroll a pick. A run made
under different settings is detected through the fingerprint. By default the player can then only
start over. With `allow_rule_changes=true` (off by default, not part of the fingerprint) they may also
carry on under the new rules if the deck is legal under them; offers on screen survive this if still
possible, so editing the settings is not a free reroll.

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
- Spectators connect to the host; the host's mod must relay the client's events. Spectators must
  announce they are modded before receiving custom packets. Late joiners need a state snapshot.
- Fallback that works in every mode: diff each player's deck against the previous `GAME_MATCH`
  packet and show "P1 swapped X for Y" at match start.

## Reference material

- SokuDev org: https://github.com/SokuDev - SokuLib (reverse-engineered structures/addresses),
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
  Patches at `0x46BCA4` (HP init), `0x481F36` (round reset -> custom hook), `0x488F86` (regen cap).
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

### Findings from SokuLib headers (v1.10a addresses; structure comments are reverse-engineered, so verify)

- **Decks in memory:** `SokuLib::Profile` (globals `profile1`, `profile2`) holds
  `Dequeue<unsigned short> cards[20][4]` at offset 0x1AC: the in-memory form of the `.pf` decks.
  Candidate target for compatible-mode slot injection (write here instead of the file). NOT yet
  verified that character select or the packet actually reads from it.
- **Deck editor:** `ProfileDeckEdit` stores each deck as `Map<unsigned short, unsigned char>`
  (card ID -> count), which fits the copy limit being a per-ID check separate from deck size.
- **`GAME_MATCH` packet:** per player `u8 character, u8 skinId, u8 deckId, u8 deckSize,
  u16 cards[deckSize], u8 disabledSimultaneousButtons`; host then client, then `u8 stage, u8 music,
  u32 seed, u8 matchId`. The buffer is sized for up to 256 cards. The deck travels as an explicit
  card list, plus the slot number (`deckId`).
- **Opcodes already in use** (pick mod message IDs outside these; the list may be incomplete):
  base game 0x01-0x0B, 0x0D, 0x0E; SokuRoll 0x10-0x14; AdvancedPracticeMode 0x15-0x17;
  DesyncDetector 0x20-0x21; Giuroll battle input 0x6B; Soku2 0xD3. Inner `GameType` events
  (`GAME_MATCH` etc.) are 0x01-0x0B. OnlinePractice's opcodes are unknown; ask its author.
- **Card tables:** `SaveDataManager` (instance at 0x899F60) has `systemCards` (0x9C) and
  `characterCards[20]` (0xA8), each `Map<int,int>` (see the probe findings below).
- **In-match deck:** `Player::deckInfo` (offset 0x57C) has `cardById` (`Map<int, CardInfo>` with
  name, type, cost-or-slot, description), `original` and `queue` ID deques, and `availSkills[32]`.
  Card lookup function at 0x436620.
- **Map node layout:** `Map::Node` is left, parent, right, value, color, isNil. `isNil` comes
  AFTER the value, so its offset depends on the map's value size (0x15 for `Map<int,int>`, 0x71 for
  `Map<int,CardInfo>`). Do not assume the modern MSVC layout.

### Findings from the probe (offline, v1.10a, verified in game)

- **Authoritative card data:** the game's own `data/csv/<folder>/spellcard.csv` and
  `data/csv/common/spellcard.csv`, readable through `SokuLib::CSVParser` (the `data` deque holds the
  cells). `SokuLib::getCharName` gives the folder name (e.g. Reisen is `udonge`, Cirno `chirno`,
  Meiling `meirin`). Under `th123e` the text is English. 21 names have a literal `<a> ` suffix to
  strip before display. Columns: id, name, type, cost-or-slot, description, an unknown flag, rarity
  (1-5, higher is rarer; 0 appears to mean it never drops at random, as with Catfish), three pairs
  of unlock target and its rarity (0,0 when unused), owner index (a system card's value is the
  character whose defeat awards it; Catfish is 21), display sort order. The flag does NOT mark the
  initially unlocked cards. In this install all skills are rarity 1, spells 2-5, and system cards 5.
  `reference/Card Collecting Notes.md` explains the unlock system these drive; the mod does not
  use it, and `data/cards.csv` does not carry rarity or unlock targets yet.
- **Text is read at runtime:** `data/cards.csv` holds no names. The mod should read them from the
  game, so players see the language they launched with. `th123e.exe` is an 8 KB launcher for the
  same `th123.exe`, so addresses and the version hash are shared.
- **Card pools:** 482 cards. 21 system (0-20, type 0); 12 skills per character, 15 for Patchouli
  (type 1); 9-14 spells per character (type 2). Spell IDs have gaps and are per character: 213 is a
  different card for Patchouli, Cirno and Utsuho. The save's maps list exactly the same IDs as the
  CSVs on a save that has every card.
- **Skill inputs:** for skills, cost-or-slot is the input: 0=236, 1=214, 2=623, 3=421, 4=22. The
  three versions of a skill share it (100, 104, 108 for most characters; ID stride 5 for Patchouli),
  and the lowest ID is the default. The community writes these as d236 (default) and a421
  (alternate), in numpad notation; `sts_skill_label` produces that. Most characters use only 4 of the
  5 inputs. The numbers do not follow ID order.
- **Map values:** range 4 to 1166, probably copies owned from the unlock system (inferred, not
  confirmed). Whether a save with fewer than 4 copies restricts a deck is untested.
- **`cardById` is deck-only:** it holds the deck's unique skill and spell cards, no system cards, so
  it cannot be used as a pool or name list. Two of 18 dumps were missing cards, both on side 1, possibly
  because it is filled during background loading (not proven). Do not read it until the match has
  started.
- **`profile1`/`profile2`:** both are filled by the main menu and hold all 80 decks of the selected
  `.pf` files, matching the files as sets of cards.
- **Writing a deck to memory works offline:** overwriting `profile1.cards[c][s]` in place (same
  size, 20 cards) at the main menu or at character select changes the deck used by the next VS
  match, including `cardById` and the in-match `original` deck. Verified with Reimu as P1 in two
  matches, including a write between matches. The written deck stayed in memory across
  match -> select and title -> select, and no `.pf` file changed on disk after closing the game.
  Not yet tested: netplay and the `GAME_MATCH` packet, changing profile, P2, and writes that change
  the deck size (these must allocate through the game's allocator).
- **The profile menu replaces a written deck:** with no scene change, the slot went back to the
  profile's own deck on each visit (the log first sees it on the title scene, with no menu open).
  The editor also shows the `.pf` deck, and saving from it wrote only the player's edit: the run
  deck did not reach the `.pf`, and the run deck was untouched. So real decks are safe, but a deck
  written once at the title is NOT enough: `mod/` rewrites the active run deck on every frame of
  offline character select (`VTable_Select.onProcess`), which reached the match after a profile menu
  visit. Netplay select scenes have their own vtables and are not hooked yet.
- **Input:** menus read the global `inputMgrs.input`, updated outside the scene. Character select
  reads `Select::leftKeys`/`rightKeys`, updated inside its own `onProcess`, so blanking its input from
  a hook does not stop it: skip its `onProcess` instead, returning its last value. Keep skipping until
  the closing button is released, or that press also picks a character.
- **Scene return values:** a scene's `onProcess` returns the NEXT scene. Returning a stale value from
  a paused scene really does switch scene (it jumped straight into a match), so only use a value
  seen on two consecutive frames as "stay".
- **Match results:** `CharacterManager.score` is rounds won (seen as 2 to 0). After a VS match the
  game shows a screen listing vanilla card unlocks, then character select. SokuLib's `VTable_Result`
  is NOT that screen: it is `CMenuResult`, the Result item on the title menu, and hooking it never
  fired. The post-match screen is `CMenuEnd`, vtable 0x859528, confirmed in game by replacing it
  (found from RTTI, and the code using `data/menu/result/result.dat` is next to its functions).
  `mod/` draws the card offer in its place, returning 1 from `onProcess` to keep the menu, and then
  leaves through the game's own exit path by holding `inputMgrs.input.a` at 1 until it closes. A run
  match with no offer due closes the screen the same way, without showing the list; a match with a
  different deck or character still shows the vanilla list, as normal play.
- **Skill input images:** `data/profile/deck2/022_SkillCard000Cost.bmp` to `004` (64x32) are the deck
  screen's images, one per skill input, in `cost_or_slot` order. The top half says "Skill Card", the
  bottom half has the arrows and button name. Paths load as `.bmp` though the archive holds `.cv2`.
- **Button names:** `profile1.keyboardBindings` holds DirectInput scan codes (name them with
  `GetKeyNameText`), and `controllerBindings` pad buttons counted from 0, while the key config screen
  counts from 1.
- **Card text layout:** descriptions are stored in fixed rows of 36 bytes, padded with spaces and
  sometimes splitting a word. `createFromText` does not wrap, so `mod/` draws each row separately.
- **Battle manager:** pointer at 0x8985E4; player pointers at +0x0C (left) and +0x10 (right).

### Findings from netplay (loopback, modded against an install without the mod, Giuroll on both)

- **A deck written to memory reaches the opponent.** With the run deck written to `profile1`, as host
  and as client, the sent `GAME_MATCH` carried it, the modded side loaded it, and the unmodded side
  visibly played against it (seen on screen, not logged). No packet change is needed. Compatible mode's
  slot injection works as designed.
- **Each side only sends its own deck.** A `GAME_MATCH` from the host has the host's deck and 0 cards
  for the client, and the reverse from the client. The other player's character bytes in that packet
  can be junk, and so can the client's own (seen as 2, skin 119, deck 246, while the match used the
  right ones), so do not rely on them. Packets are 59 bytes for two 20-card decks in total.
- **The local player's deck is in `profile1` on both sides.** As client the player is on the right
  (`rightPlayerInfo`) but their deck still comes from `profile1`. Which side a run deck is on follows
  from the role, not the profile.
- **Modes:** `mainMode` is 4 as host and 5 as client. After a match as client the scene hooked as
  `SelectServer` ran with mode 5, so use `mainMode` for the role, not which vtable fired.
- **The post-match screen is `CMenuEnd` online too**, and runs on top of the battle scene: both
  `onProcess` hooks fire every frame, so the game (and network handling) keeps running under it. The
  card offer goes there. Online scenes are never paused; offline character select may be. A vanilla
  opponent sees their Result screen and then a black screen until the modded player finishes, which
  is the game's normal wait. It is very tolerant: a wait of 28 minutes on the offer held, and the next
  match played normally, so the offer needs no timer. If the opponent closes their game during an
  offer, the offer still works and saves; character select then hangs until the game disconnects.
- **Scene hooks and roles:** after a match, a client's select, loading and battle scenes all fired
  through the Server vtables with mode 5. Trust only `mainMode` for the role.
- **All runs use compatible rules for now** (`load_data` applies `sts_settings_force_compatible`),
  until full mode exists. Rules that are already compatible keep their fingerprint.
- **Packet types seen** (first byte): 0x01, 0x03-0x06, 0x0B, 0x0D, 0x0E, and 0x43, 0x6B, 0x6E, 0x9C,
  the last four presumably from Giuroll. Pick mod message IDs outside these.
- **Port clash when testing on one PC:** `clientport` in `configex123.ini` must be 0 on the side that
  joins. If it matches the host's port, the two instances cannot connect.

### Findings from a real install (SOKU -REWIRED- v1.5, OCE lab ver)

**Profile `.pf` deck format** (verified by parsing a real profile end to end):

- 0x6A-byte header: controller config (leading `0xFF`, 11 int32 DirectInput keyboard scancodes,
  joystick button numbers). No deck data.
- Then 80 decks: 20 characters x 4 slots, in SokuLib character order (Reimu, Marisa, Sakuya,
  Alice, Patchouli, Youmu, Remilia, Yuyuko, Yukari, Suika, Reisen, Aya, Komachi, Iku, Tenshi,
  Sanae, Cirno, Meiling, Utsuho, Suwako).
- Each deck: `u8` card count, then that many `u16` little-endian card IDs, stored sorted. The
  format allows any size up to 255.
- Card IDs: system cards 0-20 (shared by all characters); skill cards 100-111 (12 per character,
  except Patchouli who has 15: 100-114); spell cards from 200 (up to 219). Use `data/cards.csv`
  rather than hardcoding the ranges.
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

**Installed mods to coexist with.** Goal: the mod is standalone and works alongside the usual
community bundle; don't require players to disable other mods. Enabled in a typical install:
Giuroll + Giuroll-UI, EasyDecks, OnlinePractice, InGameHostlist, PracticeEx, MemoryPatch,
ReplayInputView, SaveRep, ScoreTracker, and several smaller fixes. HandicapMod, InfiniteDecks and
LabTool are usually disabled.

- **Always chain hooks, never overwrite.** Character select `onProcess`/`onRender` are already
  wrapped by another mod before OnlinePractice loads.
- **EasyDecks can destroy a player's real deck while a run is in use.** Its editor (C on character
  select) reads the deck from profile memory, so it shows the run deck, and saving writes that to the
  `.pf`. Seen in game: Komachi's Deck 1 on disk became the run deck plus the player's edit. The run
  deck itself was unharmed, because online character select rewrote it. This differs from the
  profile menu, which shows and saves the real deck. So runs are refused while `EasyDecks.dll` or
  `InfiniteDecks.dll` is loaded (`blocking_mod` in `mod/screen.cpp`). Both count as cheat mods in this
  community, so asking players to disable them is acceptable.

**OnlinePractice** (a friend's netplay mod; only its `.ini` and `.log` are available, not the
source). Its startup log shows the hook set for a netplay overlay mod that works under Giuroll:

| Hook (1.10a address) | Purpose |
|---|---|
| BattleManager vtable state0-7 `0x8588fc`-`0x858914`, onKO `0x858918`, render `0x858924`, inputs `0x85892c` | Per-frame battle logic |
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

**Debug keys in this install:** F1 (in Practice, reduces HP by 1000, resetting at 0 to 10000; in VS
Com, toggles HP regeneration), F2 (fills the hand from the deck) and F3 (empties it) are available
offline but not in online play. They are useful for
in-game testing, because a match starts with an empty hand.

**Privacy:** game logs contain the opponent's IP address and local Windows paths. Never commit
logs or personal profiles. Keep reference files from the game install in a gitignored folder.

## Roadmap

1. (done) `core/`: deck model, validation (size/copy rules from settings), offer generator with
   pool filter, draft logic, settings, run save format, `.pf` deck reader (real profiles make
   good test data), tests.
2. Windows setup (done): VS "Desktop development with C++", git, CMake; ModTemplate builds and loads
   via SWRSToys.
   2b. Early game-side experiment, before any UI: find which memory the `GAME_MATCH` packet's deck is
   built from, and where the game reads decks from the `.pf` and trims them (see open questions).
3. (done, offline) Draft tool in game: card-offer UI, writes run deck. Opened with F7 on the main
   menu, a stand-in for a proper way in. One run at a time is "in use" (`active.txt` next to the DLL,
   so it survives restarts), toggled with D on the deck screen and started by finishing a draft.
   Stopping restores the slot's previous contents, in memory only.
4. Post-match reward screen (done, offline: replaces the vanilla unlock list) + **compatible mode**
   (slot injection, no networking; netplay select scenes not hooked yet) - first playable release.
   Played in game: replace by choice (with a confirm step), by type and random, multiple copies, and
   `who_picks` winner and nobody. Reward modes add and remove do NOT work in game yet: they change the
   deck size, the mod only overwrites a slot of the same size, and the game rejects non-20 decks.
5. Full mode networking: version handshake, pick visibility, waiting state, spectator relay.
6. Items: data-driven effects (max HP, meter gain, spirit regen rate/delay first).

## Open questions

- Which replacement rule is the default (community decision).
- Starting card pool and offer pool per character (system vs character cards).
- What happens if a player picks a different character mid-run (block / end run / plain match).
- Where the game trims/validates decks (profile read, deck editor, or match load). Offline, a match
  reads decks from profile memory (see the probe findings); netplay is unconfirmed.
- Whether the game validates decks received over netplay (copy limit and size).
- Upper deck-size limits, HUD assumptions about 20 cards, replay/spectator behaviour. The copy
  limit is a separate check from size (answered above); raising it would need its own patch, so
  keep it fixed at 4 for now.
