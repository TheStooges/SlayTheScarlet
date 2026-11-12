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
- **Readable names:** no one-letter names or cryptic abbreviations for variables, parameters or struct
  fields. The first loop index is `wau` (not `i`); `j` and `k` are fine as nested ones, and `id`, `db` and `ok` are fine as words.
- **ASCII only** in every file (source, comments, docs, ini, CMake): no em dashes, curly quotes, arrows
  or math symbols. The developer's Japanese-locale Windows misreads UTF-8 as Shift-JIS and shows
  garbled text, and MSVC reads source files in the system code page. The one exception, by
  decision: translations in `data/lang/` are UTF-8 (the mod converts them to Shift-JIS on load).
- **On-screen text** goes through `tr("...")` with the English as a string literal, never built from
  pieces (Japanese word order differs), so each line can be translated whole. A test fails if
  `data/lang/ja.txt` lacks any `tr` literal in `mod/`, or keeps one no longer used. Logs stay English.

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

## Repository layout (intended)

```
core/    Portable C. NO Windows or SokuLib headers. Rules engine, deck model, validation,
         card table, offer generation, draft logic, settings parsing, run save format.
         Builds and is unit-tested on any OS (initial development was on macOS with clang).
data/    Data files the mod loads at runtime. cards.csv is the card table (no text). lang/ holds
         translations of the mod's own text (UTF-8).
tests/   Console tests for core/, one file per module.
mod/     Windows-only DLL glue: SokuLib hooks, UI scenes, netplay, deck injection.
         Built only when MSVC is available.
lib/     SokuLib, as a git submodule. Run: git submodule update --init
```

The "probe" in the findings below was a throwaway logging mod, since removed. Its results are all
recorded here, and online logging now lives in `mod/netlog.cpp`.

`core/` modules: `deck` (model, validation), `pf` (profile read/write), `pool` (card table), `rng`
(PCG32), `offer` (offer generator, applying a taken offer), `settings` (`.ini` parsing, validation,
compatible-mode override, fingerprint), `run` (draft and reward state machine, save format), `lang`
(translation files, checking each keeps its English line's %d/%s blanks), `util`.
`data/settings.ini` documents every setting and states the defaults.

Language: the game's text is Japanese unless `th123e` replaced its data, so `mod/text.cpp` checks
whether the game's card names are ASCII. If not, it loads `lang/ja.txt`. Card names and
descriptions always come from the game. System/Skill/Spell and cost labels stay English, as in the
Japanese game. Error details from `core/` are still English.

By decision, every card is in the offer pool, including Catfish and the two rarity 0 spells. The game's
unlock rules (rarity, sequences, story rewards) do not limit the pool, because players use a fully
unlocked save.

Offers roll a card type first and a card within it second, because a flat roll over all cards favours
system cards (21 of them, against 12 skills and 9-14 spells: about 45-50% of offers). By decision
(v0.3.1), one setting, `card_types`, picks how, separately for the draft and rewards: `random` (a flat
roll over all cards, so system cards come up most; `STS_OFFER_BY_CARD`), `weighted` (each type equally
likely, the default and the old 1/1/1 weights, so its fingerprint is unchanged), `mostly_system`,
`mostly_skill`, `mostly_spell` (that type weighs 4 against 1 and 1: two offers in three) and
`one_of_each` (system, skill, spell in turn). The old `types`, `offer_style` and `*_weight` keys are
read and ignored. The offer generator still has `type_mask`, `style` and `weights` inside, which
`card_types` fills in; only code can set other values (shown as "custom").

Rule interpretations in `core/` that may need revisiting with the community:

- `who_picks`, by decision (v0.3), says when this player gets an offer, from their own result alone:
  `both` (win or lose), `winner` (only if they won), `loser` (only if they lost), `random` (a
  `random_chance` in 100 chance, win or lose, 50 by default and stepping by 25 on the screen, from the
  run's saved RNG so a restart cannot reroll it; hashed only while `who_picks=random`). Each player's mod
  applies it alone and nothing is exchanged, which avoids any chance of a desync. With no offer the
  deck stays unchanged. Which card comes in is a separate setting, `offer_pick`: `choose`, or `random`
  (one offer is kept, saved as the only offer so a restart cannot change it). With `replacement=random`
  as well, the swap makes itself and the deck screen shows it. The old `who_picks=nobody` was that
  automatic swap; it is gone, and a file that says it reads as `both`.
- The draft adds one copy per pick until the deck reaches `size_min` (by decision, v0.3: `picks`,
  `copies_min` and `copies_max` left `[draft]`; older files still list them and they are ignored).
  Rewards keep their copy range, and add/remove grow or shrink the deck towards `size_max`/`size_min`.
- `copy_limit` is refused above 4 until the game's limit is patched (an illegal deck silently means
  no deck in game).
- REPLACE counts the copy limit before removing anything, so it stays legal whatever is removed.
- REPLACE never removes copies of the card being added, in any replacement mode, as that would waste
  the reward. The generator allows for this, so an offer that could not be completed is never made.

Runs save their random state and any offers on screen, so reloading cannot reroll a pick. A run made
under different settings is detected through the fingerprint. By default the player can then only
start over. With `allow_rule_changes=true` (off by default, not part of the fingerprint) they may also
carry on under the new rules if the deck is legal under them; offers on screen survive this if still
possible, so editing the settings is not a free reroll.

A match finished while an offer still waits (see the disconnect finding under netplay) does not lose
its reward: it is owed, saved with the run (`owed` line, save format 2; version 1 saves still load),
and its offers come up once the waiting offer is taken, after the deck screen shows that change.

Save format 3 (v0.4) adds, after the `owed` line: the run's `seed`, `restarts` (starts over before
this run, for the set seed), `offer_frames` (time on the offers on screen, for the time limit), the
`rules` the run was made under (every rule setting as `section.key=value`, not `show_deck` or
`allow_rule_changes`, for putting them back), and a `history` of up to 100 matches (won, rounds,
opponent, and the cards that came in and left), plus a `finished` phase. A match joins the history
pending when it ends, and its change is filled in when its reward is settled, oldest first, so owed
rewards line up. An endless run forgets its oldest matches past 100 (`history_forgotten` keeps the
numbering). Version 1 and 2 saves load with these empty; such a run cannot put its rules back.
Older releases refuse a format 3 save as from a different version.

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
- Spectators hang under the client, not the host (per the protocol doc below: the host has one child,
  the client; the client one, the first spectator; each spectator up to 4 more). So the host's
  events must pass through the client's mod, then down the spectator tree. Spectators must announce
  they are modded before receiving custom packets. Late joiners need a state snapshot.
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
  The Card Collecting Notes and flowcharts explain the unlock system these drive (not in this repo:
  https://drive.google.com/drive/folders/1SMToH54savI-kohoauLHBUqlog8rj2mr, with a local copy in the
  gitignored `reference/`). The mod does not use it, and `data/cards.csv` does not carry rarity or
  unlock targets yet.
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
- **Game archives** (`th123a.dat`, `th123b.dat`, `th123c.dat`; 11,663 files): u16 file count, u32
  list size, then the list, XORed with MT19937 (seeded with list size + 6, low byte of each output)
  and then a rolling key (start 0xC5, step 0x83, step grows by 0x53). Entries are u32 offset, u32
  size, u8 name length, Shift-JIS name. File data is XORed with `((offset >> 1) | 0x23) & 0xFF`.
  `th123e.dat` has another layout. A `.cv2` image is u8 bits, u32 width, u32 height, u32 row width,
  u32 palette, then pixels; 24-bit images are stored as 4 bytes per pixel (BGRX), 32-bit as BGRA.
  Listed read-only with a throwaway Python script; nothing in the repo reads archives. Useful
  finds: `data/scene/select/bg` (gear background, 640x480), `data/stand/<folder>` (portraits,
  640x480), `data/stand/cutin/<folder>` (spell cut-ins), `data/scene/title/15_back` (title sky),
  `data/profile/deck2/name/<folder>` (name banners, 360x100).
- **Button names:** `profile1.keyboardBindings` holds DirectInput scan codes (name them with
  `GetKeyNameText`), and `controllerBindings` pad buttons counted from 0, while the key config screen
  counts from 1.
- **Card text layout:** descriptions are stored in fixed rows, padded with spaces where a sentence
  ends early and sometimes splitting a word: 36 bytes under `th123e`, 30 bytes (15 full-width
  characters) in the Japanese game, matching its deck editor. `createFromText` does not wrap, so
  `mod/` draws each row separately at the width for the game's language.
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
- **A disconnect during an offer** (seen with and without InGameHostlist) sends the game to the title
  with its network menu open. Game menus are drawn after the scene, so the menu covered the offer,
  and the offer kept reading buttons under it. Now the network menu's own `onProcess`/`onRender`
  (`VTable_ConnectMenu`, 0x859300) run the offer instead while it is open, so it shows straight away,
  and the menu appears once it is taken. These hooks go in on the first title frame, after every
  mod has loaded, to be the outermost. InGameHostlist still drew its host list and menu items over
  the offer: it draws with ImGui from EndScene whenever the top menu's vtable pointer is 0x859300
  (source: github.com/SokuDev/InGameHostlist). So while the offer is open the menu object points
  at a copy of the vtable, which hides it from such checks. Under any other title menu the offer
  pauses. The owed queue
  stays as a fallback.
- **Junk bytes in `GAME_MATCH`:** a client's own character byte was 110 in one packet and the
  opponent's character in another, so the packet log finds each deck by searching every profile slot.
- **Scene hooks and roles:** after a match, a client's select, loading and battle scenes all fired
  through the Server vtables with mode 5. Trust only `mainMode` for the role.
- **All runs use compatible rules for now** (`load_data` applies `sts_settings_force_compatible`),
  until full mode exists. Rules that are already compatible keep their fingerprint.
- **Packet types seen** (first byte): 0x01, 0x03-0x06, 0x0B, 0x0D, 0x0E, and 0x43, 0x6B, 0x6E, 0x9C.
  0x6B and 0x6E are Giuroll's (see the Giuroll notes; it owns 0x6B-0x80); 0x43 and 0x9C are from
  something else in the bundle. Pick mod message IDs outside all of these.
- **Port clash when testing on one PC:** `clientport` in `configex123.ini` must be 0 on the side that
  joins. If it matches the host's port, the two instances cannot connect.
- **Spectating cannot be tested on one PC:** a third copy could not spectate two copies connected to
  each other on the same machine. Spectator tests need at least one other person.
- **Stall when testing on one PC (2026-11-11, not this mod):** after connecting, the host stopped
  responding for about a minute before reaching character select, then carried on. It happened with
  this mod disabled in both copies, so the cause is elsewhere (another mod, a hidden Windows prompt, or
  the unfocused window). It went away after a restart, so it was probably something left over on the
  PC rather than a setting. The copies also differed in `giuroll.ini` `soku2_compatibility_mode`; keep
  it the same in both.
- **Reward options online (v0.3.1, played by the developer):** changing the when-you-get-an-offer,
  what-comes-in, what-leaves and copy settings in netplay worked, with no problems seen.
- **Spectating (a modded spectator, run in use, watching two unmodded players over a real
  network, 8 matches):** the mod stayed out of it. No offer, select or result lines were logged, as
  the spectator scenes are not hooked. A spectator receives one `GAME_MATCH` per match holding BOTH
  players' decks (99 bytes for two 20-card decks, against 59 between players), so the "P1 swapped X
  for Y" fallback can work on a spectator from the decks alone. The spectator also sends each
  `GAME_MATCH` on with the same decks and a deck byte counting up (5, 6, 7, 8), presumably relaying
  to spectators connected through it. Mind this before sending custom messages to spectators.
- **Spectating two modded players (v0.3.2, a real network, both with runs in use, the spectator
  logging its scenes and requests, Japanese game):** the spectator's scenes are `LoadingWatch` then
  `BattleWatch` (mode 6), and it sat on `LoadingWatch` for the whole offer and character select,
  about 60 to 95 s between matches, as the developer described. Each new `GAME_MATCH` (99 bytes, both
  decks) arrived just before its battle, numbered 1 to 4. Its replay requests read as the protocol
  doc says (u32 frame, u8 match): the first asked for match 0 from frame 0xFFFFFFFF, then each match
  from its start, with one reply per request (3,270 and 3,269 in match 1). The frame never went back,
  so the catch-up replay was not seen this time. When a player disconnected, the spectator got QUIT
  (0x0B). The player who stayed (client) took their offer after the QUIT, so the offer survived.
- **Both players modded (v0.1, a real network, both with runs in use, 6 matches with each side as
  host):** each side sent its run deck and the other loaded exactly that deck in every match. Both got
  offers (`who_picks=both`), and the changed deck reached the opponent in the next match, including
  after a restart. Closing the game or disconnecting during an offer kept it, and it came back on
  restart or could still be taken. Matches on another character or deck slot gave no offer, and
  stopping a run put the profile's real deck back. Spectating with two modded players is not tested.

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
  `InfiniteDecks.dll` is loaded (`blocking_mod` in `mod/run_deck.cpp`). Both count as cheat mods in this
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

**Giuroll** (read from source, github.com/Hagb/giuroll-hagb, the maintained fork of
github.com/Giufin/giuroll; Rust). It hooks the game's packet read (after `recvfrom`, at 0x41DAEA):
first byte 0x6B is its battle input, 0x6E an opponent's Esc, 0x6C a probe it answers with 0x6D, and
**any other first byte from 0x6D to 0x80 is swallowed** (it sets the length to 0x400 so the game
drops it). So Giuroll owns 0x6B-0x80: never use a byte in that range. Making the game drop a packet
by its length is a trick a mod can use for its own packets too. Giuroll also watches the game's sent
packets (0x4171B4, 0x4171C7) and changes what is sent to spectators (only frames with a buffer of
them, at 0x42DAA6). It refuses to load if another netcode mod is present.

**Hisoutensoku protocol** (github.com/delthas/touhou-protocol-docs, `protocol_123.md`; incomplete,
not checked against the game). Top-level types: 0x01 HELLO, 0x02 PUNCH, 0x03 OLLEH, 0x04 CHAIN (every
0.5 s to children), 0x05 INIT_REQUEST (carries a game_id that keeps incompatible clients out), 0x06
INIT_SUCCESS, 0x07 INIT_ERROR, 0x08 REDIRECT, 0x0B QUIT, 0x0D/0x0E game data from parent/child
(sub-types: 0x01 LOADED, 0x02 LOADED_ACK, 0x03 INPUT, 0x04 MATCH, 0x05 MATCH_ACK, 0x08
MATCH_REQUEST, 0x09 REPLAY, 0x0B REPLAY_REQUEST). Spectators send REPLAY_REQUEST every 3 frames and
get GAME_MATCH (both decks) when a new match starts, else REPLAY (compressed inputs). It does not say
what the game does with an unknown first byte, or what a spectator sees between matches; SokuLib has
only `LoadingWatch` (0x858180) and `BattleWatch` (0x85758C) spectator scenes.

**Spectators, from the developer's experience:** spectators run Giuroll too (the community shares one
copy of the game), so a packet type in Giuroll's dropped range (0x6F-0x80) is ignored by every peer
without this mod, players and spectators alike. A spectator waits on the loading screen, unable to do
anything but leave, until the players finish character select; the result screen and character select
are never shown to it. Watch for this: a spectator is sometimes sent to the loading screen and then
back to the battle, where a fast-forwarded replay of the previous match plays. The cause is unknown; a
guess is that the spectator was behind and is still catching up on that match's inputs (Giuroll holds
back frames for spectators). So anything shown on a spectator's loading screen must be tied to the
match it belongs to (the `GAME_MATCH` match id), not to scene changes, and must survive a trip back to
`BattleWatch`.

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
   menu, a stand-in for a proper way in. One run at a time is "in use" (`runs/active.txt` next to the DLL,
   beside the `run_XX.sav` saves, so it survives restarts), toggled with D on the deck screen and started by finishing a draft.
   Stopping restores the slot's previous contents, in memory only.
4. Post-match reward screen (done, offline: replaces the vanilla unlock list) + **compatible mode**
   (slot injection, no networking; netplay select scenes not hooked yet) - first playable release.
   Played in game: replace by choice (with a confirm step), by type and random, multiple copies, and
   `who_picks` winner and nobody. Reward modes add and remove do NOT work in game yet: they change the
   deck size, the mod only overwrites a slot of the same size, and the game rejects non-20 decks.
5. Full mode networking: version handshake, pick visibility, waiting state, spectator relay.
6. Items: data-driven effects (max HP, meter gain, spirit regen rate/delay first).
7. (done for v0.2, played in game) Starting deck, as in Slay the Spire: the draft starts
   from built-in starter cards (`default_starter` in `core/settings.c`) and the picks fill the rest.
   Off by default (`[starter] enabled=false`). By decision (v0.3), the cards are not configurable,
   in the file or in game: v0.2's `common` and per-character lists in `[starter]` are read and
   ignored. The developer's chosen starter, based on defensive options as a beginner's
   crutch: 2x each of Spiritual Strike Talisman (0) and Divine Raiment of the Dragon Fish (13) for
   everyone (`common`), plus per character 2x a chosen default skill and 2x a spell card. 8 cards,
   so 12 picks. A
   Penny Saved (12) is left out on purpose, as one of the strongest cards in the game. The v0.2
   lists used cards from each character's default deck, except Sakuya's Silver Bound and Yukari's
   Border Between 2D and 3D (`reference/default.pf` has the default decks). After v0.3 the community
   revised them (2026-11-10), for this reason: the two system cards are strong enough to be worth
   having over drafting them, so the spell cards are deliberately weak: spells matter less to how a
   character plays than skills do. A spell already mediocre stays (Reimu, Sakuya, Reisen, Aya,
   Komachi; Suwako too, as a weak character); Yuyuko keeps hers, as she has big damage without cards.
   The 3-cost-or-less rule is broken on purpose for Patchouli (4-cost Emerald Megalopolis), and Yukari
   drops to 1-cost Chen. Alice moved to d236 to make up for a weak spell. New skill inputs: Alice,
   Patchouli, Youmu, Aya, Iku, Tenshi, Meiling, Utsuho. Youmu and Remilia keep their v0.2 spells,
   decided for v0.3.1. Every list is tentative and may be rebalanced in a later release. The comments in
   `g_starter_cards` name every card. The lists count
   towards the fingerprint only while the starter is on, so older runs keep their rules. Starter
   cards are normal cards afterwards (copy limit, can be replaced). The lists may be tuned later by
   the community, in code. Not chosen: per-player choice between starters (one option would
   dominate), and all default skills (uneven for Patchouli, and fixes the whole skill set).
   A deck that starts small and grows over matches still needs the deck size patch.
8. Card sounds on the offer screens (rewards, and the draft for consistency): highlighting an offer
   plays the sound that card makes in a match, e.g. the sound of the spell itself. Feasible in part:
   SokuLib exposes the game's sound load/play functions (`ADDR_LOAD_SOUND` 0x401AF0, `ADDR_PLAY_SOUND`
   0x401D50) and each character's sound table in a match (`CharacterManager.soundTable`, `playSE`).
   The missing piece is which sound belongs to which card: `spellcard.csv` does not say. It lives in
   each character's animation (pattern) data, as sound cues on the move's frames. Needs that format
   reverse-engineered, or a hand-made card-to-sound table (about 480 cards). Investigate feasibility
   first. Details: play only once the cursor settles, so fast scrolling does not stack sounds; a
   setting to turn it off, cosmetic so not in the fingerprint (like `show_deck`). Cheaper fallback
   if it stalls: one shared game sound per card type (e.g. `SFX_SPELL_ACTIVATE` for spells).
9. Time limit on the reward screen (after v0.2). Not needed technically (vanilla waited 28 minutes);
   it keeps the opponent from waiting on a black screen. A setting, e.g. `[reward] time_limit`
   in seconds, 0 = none and the default, part of the fingerprint (it changes how a reward can turn
   out). On time-out, keep any choices already made and make the rest at random: the offer, then
   the cards or type to replace. The random choices come from the run's saved RNG, so restarting
   cannot reroll them, but the save must also record the time used, or restarting the game resets
   the timer (a small save format change). In compatible mode only the local player sees the timer;
   showing it to a modded opponent needs full mode messages. See how much players stall in phase 2
   before setting a default.
10. Roulette for random picks (after v0.2): instead of jumping to the deck-changed screen, the cursor
   runs through the offers, fast at first and slowing down (gaps from a few frames to about 15,
   about 2 s in all, move sound per step, confirm sound at the end), and settles on the result.
   Used on a time-out (item 9), with `offer_pick=random`, and possibly over the deck grid when
   `replacement=random` picks the cards to remove. The result is rolled first and the animation only
   shows it: pick the starting position so the slow-down lands on it. Runs per frame like every
   screen, never a blocking loop. Confirm skips to the result. Cosmetic setting, e.g.
   `reward_animation=true`, not in the fingerprint (like `show_deck`). Testable offline with
   `offer_pick=random`.
11. (done for v0.3, played in game) Settings screen: edit `settings.ini` in game, from a
   Settings entry on its own row under the F7 character grid (chosen over a button). It opens a list
   of menus (Draft, with the starter's on/off; Rewards; Runs, which the match limit of item 12 will
   join) ending with Save changes; edits are
   kept across menus until saved, and A also saves from inside a menu. Not an external
   tool, which would take players out of the game and could drift from the mod's version. One row
   per setting, grouped by section; up/down chooses, left/right steps numbers and cycles choices, A
   saves, B leaves (twice with unsaved changes), C sets the default. Help lines are built into the
   mod through `tr()` (translated), not read from the file's comments. Settings with no effect yet
   (deck size and copy limit, forced by compatible mode; pick visibility, opponent deck, items) are
   not shown, by decision: they join a menu when they do something. The reward mode only steps
   between replace and skip, Saving
   runs `sts_settings_validate` and the starter check, lists the runs whose rules would change and
   asks, refuses if the file changed on disk since the screen opened, then writes through
   `sts_settings_rewrite` (only changed values; missing settings added to their section) to a
   temporary file moved over the old one. The settings reload at once: a run in use whose rules
   changed stops as at start-up (`active.txt` kept), and comes back if they are changed back.  In full mode both players need one set of rules (probably the host's), and the
   screen should show which rules are in force.
12. Match limit and score screen (after v0.2): `[run] matches`, 0 = endless and the default, part of
   the fingerprint. Counts finished VS matches with the run deck, as `matches_played` does now (a
   disconnect before the result screen does not count). The last match skips the offer, since the
   card would never be played, and goes to a score screen. The run then enters a new finished phase
   (save format change): no longer in use, Deck 1 back to normal, final deck still viewable, only
   start over continues. Score screen: wins and losses plus rounds, the final deck, and the deck's
   history ("Match 3: Silver Bound for Killing Doll"). Neither results nor history are saved yet,
   so each match's outcome and change must be recorded (bounded by the limit). A single score
   number is a community rule, so start with the record and add a formula setting later if wanted.
   Optional: a short summary per finished run in a `runs/history` file. Online: in compatible mode
   runs are independent (the opponent may not be on a run); in full mode the players could share
   the limit and see each other's score screen (custom messages). Opponent names stay local only.
   Open: a "first to N wins" limit instead, and ending a run early after N losses (like dying in
   Slay the Spire); both would be settings beside `matches`.
13. (done, for v0.2) Screen background: every mod screen draws over something not meant for it
   (the stopped battle scene after a match, so plain black; the title menu before one, showing
   through the text). Now every screen draws the deck editor's gear background
   (`data/scene/select/bg`), then on run screens the character's portrait (`data/stand/<folder>`)
   at tint alpha 0x48, then a 0x60 black shade. 0x70 was too strong: white text over a light
   portrait (Komachi's apron) was hard to read. Falls back to plain black if loading fails.
14. Set seed, for events (planned with v0.4, beside item 12). Within a run rerolling is already
   impossible (the RNG state is saved), but starting over seeds a new run from the clock, so a player
   can delete runs until they like a draft. A rule setting, e.g. `[run] seed`, blank = random as now
   and the default, part of the fingerprint so an event's shared `settings.ini` carries it. Each run's
   seed comes from the event seed plus the character: everyone on one character gets the same draft
   offers, different characters get different ones. Starting over then gives the same draft back.
   Rewards follow on from the run's RNG, so runs diverge with players' choices, but nobody can reroll.
   Offers are local only, so no netplay or desync concern. What it does not close: a player can draft,
   start over and draft again knowing every coming offer; and the mod is local, so a determined player
   can edit the seed or the run file (the save checksum only catches accidents). So make it visible
   rather than impossible: count starts over in the run (save format change) and show the seed, the
   rules fingerprint and that count on item 12's score screen, for players to submit as a screenshot.
   Open: whether events allow starting over at all (allow and count, only before the first match, or
   never), as an event rule setting.

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
