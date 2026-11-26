# Findings

What is known about the game, the protocol, and the mods this one lives with, from reading
sources and from probes in game. Addresses are for v1.10a.

The "probe" in the findings below was a throwaway logging mod, since removed. Its results are all
recorded here, and online logging now lives in `mod/net/netlog.cpp`.

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
  u32 seed, u8 matchId`. The mod reads the seed from the host's packet (`HOST_GAME`, sent on the
  host, received on the client) for each match's reward rarity; that both see the same value is
  assumed from the layout, not yet checked over a real network. The buffer is sized for up to 256 cards. The deck travels as an explicit
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
  Not yet tested: netplay and the `GAME_MATCH` packet, changing profile, and writes that change
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
- **Keyboard keys (read from th123.exe with dumpbin, 2026-11-24):** the raw DirectInput buffer is at
  0x8A01B8 (a byte per scan code). Before the scene runs, 0x40A950 counts how many frames each watched
  key has been held, an int per scan code at 0x8998D8 (Esc, scan code 1, at 0x8998DC; watched keys are
  registered at 0x442A6A: Esc, then F1 and on). The game's key check, 0x43DE30(key, ...) through
  0x40AA60, takes a key as pressed when its count is exactly 1. So hiding a press from the game means
  changing that count (1 to 2: held, not pressed), not the raw buffer, which is read too early.
- **Online battle scene** (`onProcess` 0x4282F0 host, 0x4285C0 client): on the win quote (battle
  manager +0x88 == 6) it runs the battle manager's step (vtable +0x0C) and returns 8, character select,
  when that returns 1 (Esc), through 0x428394, where Giuroll sends its 0x6E.

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
  profile menu, which shows and saves the real deck. So runs are refused while `EasyDecks.dll` is
  loaded (`blocking_mod` in `mod/run/run_deck.cpp`).
- **InfiniteDecks is incompatible for another reason** (from the developer, 2026-11-25): it replaces
  the four deck slots with a list of decks, so a run deck written into a slot could not be chosen.
  Runs are refused while `InfiniteDecks.dll` is loaded too. Both count as cheat mods in this
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
