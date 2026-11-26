# Roadmap

The findings referred to below are in `findings.md`, and how built features behave in `design.md`.
Item numbers are referred to from elsewhere, so keep them.

Order (decided 2026-11-25): changes that alter the game less come sooner, changes that alter it more
come later. Things only the mod sees (its screens, settings, which cards it offers, such as rarity)
go before anything that changes a match itself (items, status and curse cards), which come much later.
After v0.6: reorganising the in-game settings (details to come from the developer), then the v0.6
spectator results, then rarity (15); items (6) and status and curse cards (16) much later.
Also mod-only, not yet placed: a proper way into the mod instead of F7 (3; F7 stays for now, by
decision, but this may change); the opponent's deck
display, perhaps at a run's end, and run endings (12), both undecided; turning off starting over for
events (14); one set of rules online (11), to be revisited; the open community questions. Still to
play: the set seed across machines (14) and player 2 in local VS. Changing the match, so after rarity
and before items: deck sizes other than 20 with the add and remove rewards (4, 5), and a copy limit
above 4; both need patches to the game's checks, and full mode online.

1. (done) `core/`: deck model, validation (size/copy rules from settings), offer generator with
   pool filter, draft logic, settings, run save format, `.pf` deck reader (real profiles make
   good test data), tests.
2. Windows setup (done): VS "Desktop development with C++", git, CMake; ModTemplate builds and loads
   via SWRSToys.
   2b. Early game-side experiment, before any UI: find which memory the `GAME_MATCH` packet's deck is
   built from, and where the game reads decks from the `.pf` and trims them (see open questions).
3. (done, offline) Draft tool in game: card-offer UI, writes run deck. Opened with F7 on the main
   menu, a stand-in for a proper way in. One run at a time is "in use" (in Deck 1 by default; `[run] deck`, 1 to 4, picks the slot by v0.5, a local choice and not a rule, and a run in use moves when it changes; the slot's icon is from `data/scene/select/character/kidoairaku`, five icons of which the first four are Deck 1 to 4) (`runs/state.txt` next to the DLL, `active.txt` before v0.4,
   beside the `run_XX.sav` saves, so it survives restarts), toggled with D on the deck screen and started by finishing a draft.
   Stopping restores the slot's previous contents, in memory only.
4. Post-match reward screen (done, offline: replaces the vanilla unlock list) + **compatible mode**
   (slot injection, no networking; netplay select scenes not hooked yet) - first playable release.
   Played in game: replace by choice (with a confirm step), by type and random, multiple copies, and
   `who_picks` winner and nobody. Reward modes add and remove do NOT work in game yet: they change the
   deck size, the mod only overwrites a slot of the same size, and the game rejects non-20 decks.
5. Full mode networking: version handshake, pick visibility, waiting state, spectator relay.
   Everything up to v0.4 is client-side: it works against vanilla players, sends no custom packets
   and changes none (the network hooks only read, for logging). First step (built after v0.5, not yet
   seen with real spectators; needs other people): `mod/net/spectator.cpp` shows each player's deck
   changes on a modded spectator's screen, worked out from the `GAME_MATCH` decks it already receives
   (`sts_deck_changes` in `core/deck`), so nothing is sent and it works for unmodded players too. Shown
   at the start of the match, not on the loading screen: the packet only arrives just before the
   battle. Each side's changes show as card images (each copy its own card, outlined green for in and red
   for out, no text) at half size under that player's half of the screen, cards in then cards out on
   one row, 8 to a row, for 8 s, then fade. It is the fallback once
   packets exist (they reach a spectator during loading, but only with both players modded). Only a
   match numbered one after the last gets a notice (a new pair or a late join gets none), and only
   while the spectator's replay requests ask for that match (their match byte counts the same way as
   `GAME_MATCH`'s, seen in the v0.3.2 spectator log), so a catch-up replay of the previous match does
   not show it. Live picks (showing a player's choice as it is made) were planned next, and are
   dropped by decision (2026-11-25): the changes shown afterwards are enough.
   Decided (2026-11-18): packets only after a handshake, so compatible mode's rule (nothing custom to
   a peer without the mod) holds. Once the opponent is known to have the mod, the players can also
   compare settings, for tournaments (casual play can ignore a mismatch). Deck changes are sent on
   confirm and shown on the spectator's loading screen; the comparison above shows only when that
   packet was missed. Two settings, not rules: players see each other's changes only when BOTH
   players have theirs on (off by default); a spectator has its own on/off for showing changes (on
   by default). Delivery (decided 2026-11-19, to avoid needless traffic): each change has a serial
   and is acknowledged hop by hop, as the game does with GAME_MATCH and MATCH_ACK. The sender sends
   once and resends only while unacknowledged, with a growing gap, stopping at the next GAME_MATCH.
   A receiver acknowledges every copy (so a lost acknowledgement is answered by the next resend) and
   ignores serials it has seen. The client acknowledges the host, then sends on to its modded
   spectators and waits for theirs, and each spectator does the same for its own.
   Handshake (built 2026-11-18, `mod/net/peers.cpp`; seen working between two copies on one PC on
   2026-11-19: each side found the other, same and different rules told apart, the hidden packet was
   1024 bytes as with Giuroll, a match played normally; not yet with spectators). The settings load
   on the first title frame so the hello carries their hash (a flag says whether they loaded). A peer's relation comes from
   the game's packets (HOST_GAME from the parent, CLIENT_GAME from a child). Each machine sends a hello
   (type 0x77, "STS", protocol 1, kind 1, flags heard-you and spectator, u32 settings hash) with the
   game's packets to each parent and child, every 0.5 s until answered, giving up after 10; a hello
   that has not heard from us gets an answer at once. Received mod packets are hidden from the game by
   returning the whole buffer's length, as Giuroll does (it also drops the type). Peers are forgotten
   on QUIT or after 15 s of silence. The log-only observer (`observer.txt`) takes no part.
   Deck changes (built 2026-11-19; between two copies on one PC, over two matches with a reward after
   each on both sides, every change arrived and was acknowledged on the first send, and the second was
   measured from the second match's deck; not yet with spectators): `spectator_player_frame`, on
   every online player frame, compares the run deck (`g_active_deck`) with the player's own deck in the
   last GAME_MATCH it sent (only if that was the run deck; a client's own character byte can be junk)
   and sends the difference, with a new serial whenever the deck changes again (owed rewards). Kind 2
   (side, character, serial, flags, cards in, cards out), acknowledged by kind 3 (side, serial);
   `peers:` log lines. No match number: between the players GAME_MATCH's match byte does not count (the
   host sent 229 and the client 36 in both matches, stage and music junk on the client), so a player
   counts its own matches (a GAME_MATCH sent over 2 s after the last is a new one), delivery stops at
   the next GAME_MATCH, and a spectator stamps a change on arrival with its own next match number
   (its GAME_MATCH does count, and arrives before each battle). A spectator draws the changes it
   received on `LoadingWatch` for that match, with no time limit, and at the start of the battle the
   comparison only for a side whose change did not arrive. Settings (built 2026-11-19, not yet
   tried): `[netplay] share_changes` (false) and `spectator_changes` (true), not rules, in a Netplay
   menu between Runs and Sounds. The hello's bit 3 says a player shares; `peers_opponent_shares` is the
   opponent (the client for the host, the host for the client) having the mod and sharing. Sharing
   between players, by decision on the post-match screen (it keeps the players in step): when the
   mod is done with that screen (`done_with_result` in `mod/main.cpp`), the player sends a change with
   the finished flag (bit 1, the whole change since the match, empty without a run deck) and the
   screen opens `ST_OPPONENT`: "Girls are now praying..." (as Touhou games show a wait) until the opponent's finished change arrives
   (cancel stops waiting; the opponent leaving ends it), then, as soon as it arrives, both players'
   changes, player 1's (the host, `profile1name`) above player 2's, each headed by the profile name
   and character, cards in on the left and out on the right (confirm
   goes on; with a time limit it closes after 15 s), or nothing if neither deck changed. A player who saw
   the game's own list instead (not a run match) sends theirs from character select. What the
   opponent sent, and whether this player's finished change has gone, are cleared when a battle starts
   (the first battle frame after character select), not at GAME_MATCH: the first GAME_MATCH goes out at
   character select before any match, and a finished change sent from there counted as the match's
   (seen 2026-11-20: one side showed the changes at once with the other's empty, the other waited for
   ever). Character select only sends the finished change once a battle has been played. Whether the
   screen has been shown this match (`g_share_shown`) clears at online character select: a battle frame
   with `menuManager` empty never came in an online match (seen 2026-11-20, the second match showed
   nothing on either side). Esc on the win quote (battle match state 6) skips the screen after the match:
   the game sends Giuroll's 0x6E at once and the battle scene returns 8 (character select), as it does
   when that screen closes normally (state 7); the other side then shows a black screen until its own
   screen after the match closes (found with a probe, 2026-11-24). So an online battle returning
   character select sends the finished change if it has not gone, and a praying player also stops 1 s
   after the opponent's 0x6E with no finished change. Skipping that screen used to skip the match's
   reward too (it was only counted at the next title, and lost if another match ended first), so
   `deck_on_skipped_result` now counts it there and starts its reward without a screen
   (`record_skipped_reward`): offers are saved with the run and come up after the next match, as an
   owed reward's do; a reward with nothing to choose changes the deck at once.
   Win quote (decided 2026-11-24, seen working on one PC): an online run match's reward starts as the
   win quote begins (`deck_on_win_quote_frame` in `mod/run/match.cpp`, `begin_reward_early`), saved, so
   closing the game there keeps it, and the screen after the match only shows it. With a reward
   waiting, Esc on the win quote goes to that screen instead of skipping it: the mod turns Esc's held
   count from 1 to 2 (see the keyboard finding) and presses confirm every other frame until the screen
   opens (15 ms in the test). It reads only that count, only then, and keeps nothing. By decision Esc on
   the win quote counts as bad manners anyway, and a player who wants out can close the game; the
   skipped-screen fallback above stays for anything that gets past it.
   Rules warning (built 2026-11-24, not yet seen): online character select draws "StS: Opponent's
   rules differ." (the StS label says which mod it is from) in red at the bottom left, above the game's key prompts, while the opponent (`opponent_peer`) has the mod, both
   settings hashes are known, and they differ (`peers_opponent_rules_differ`), from onRender hooks on
   `VTable_SelectServer`/`SelectClient`. It only warns; nothing is blocked.
   Reconnecting without restarting the game (tested 2026-11-24) carried state over: a change still
   being delivered went to the next opponent, and the next connection measured a new run deck (drafted
   in between) against the last connection's deck. So when the last peer is forgotten (QUIT or 15 s of
   silence), `end_connection` in `mod/net/peers.cpp` stops delivery and forgets the serials seen, and
   `changes_on_connection_end` forgets the player's last deck, the opponent's change, and a
   spectator's record of the players.
   By decision (2026-11-15), deck sizes other than 20 are for full mode only: the game nullifies a
   deck that is not 20 cards (seen with DeckRandomizer), so a vanilla opponent would get no deck.
6. Items: data-driven effects (max HP, meter gain, spirit regen rate/delay first). Can be built and
   tested offline, but by decision (2026-11-15) online they need full mode, as both machines must
   know both players' items. Ideas (2026-11-16, not yet checked for feasibility; each needs testing
   in game first):
   - Effects to mix and match into double-edged items, the pairs decided by balance: flat HP or
     card meter gain rate changes; passive HP loss or meter gain over time; spirit regen delay or
     rate changes; starting the match with something done (1 card's worth of meter, 1 spirit orb
     crushed); starting with an alternate skill already equipped (level 0), but the first Sprinkle
     weather (which maxes every skill's level while it lasts) does not affect that player; weak or
     strong shot bullets that are bigger (and possibly do more damage) but cost more.
   - Character items: one per character, only that character can get it, a pure buff (no debuff) to
     their own system card (the system card whose owner index in `spellcard.csv` is that
     character). Examples: Reimu's Spiritual Strike Talisman applies 100% limit instead of 60%;
     Sakuya's Stopwatch lets her attack during the time stop.
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
8. (done, for v0.5) Card sounds on the offer screens (rewards, and the draft for consistency). Done so
   far: `data/card_sounds.csv` has a row for every card (collected with a sound probe and
   a script, since moved out of the repo to the gitignored `reference/sound_probe/`, with a note on
   putting the probe back in the build),
   built into the DLL, and `mod/card_sounds.cpp` plays a row through the game's sound manager
   (0x89F9F8). The gallery (bottom right of the F7 menu, `mod/screens/gallery.cpp`) lists every card and plays
   its row, for fine-tuning the table by ear. It shows only cards found in a run: every card offered
   in a run (draft or reward, taken or not, and the offers a random pick was made from) or in a starting
   deck (recorded when the run starts, and for saved runs from the deck the draft left), recorded in `runs/state.txt` (`found` lines, after everything older builds read)
   on each save and, when the gallery opens, from the offers in every saved run. The rest are face down
   (`data/infoEffect/cardFaceDown`) and silent. Unlock the gallery (a row under the kinds, pressed twice) shows every card from then on (`gallery_unlocked` in `state.txt`); the row then reads Gallery unlocked, greyed out, or All cards obtained once every card is found in runs. The found counts stay true. With `[sound] card_sounds=automatic`, a card's first sound plays when the cursor has rested for 74 frames, wherever it falls in the move.
   The draft and reward screens play the offer under the cursor the same way, first outcome only
   (`card_sounds_follow_offers`, once a frame from `screen_update`). The deck screen plays a card only on Use Card.
   `[sound] card_sounds` (file only since 2026-11-25, when the Sounds menu left the settings screen; not a rule, like the roulette): `use_card` (the default: Use Card, `EV_CARD` from `input.spellcard`, plays the card under the cursor on the offers, the deck screen, and in the gallery), `automatic` (offers and gallery cards also play after the settle delay), or `off` (no card sounds; the 048 warning before a reset always plays). Key prompts name buttons by action (Melee, Weak Shot, Strong Shot, Dash, Use Card) with the key of the device the profile is set to (`profile.controllerIndex`: -1 keyboard, else a pad; the `.pf` header's first byte). A settings row that needs another setting first is greyed out and skipped (`settings_row.disabled`), not hidden. Shared
   sounds named so far: 001 light hit, 006 cut, 007 heavy hit, 008 slash, 020 block, 022 wall or ground
   bounce, 023 spell card declared (the default for spells), 026 spirit crystal landing (never a
   card's), 027 light swing, 029 swing, 031 dash, 036 card generated, 053 weather activated, 054 card used (the
   default for skills and system cards), 039/040/041/061 menu. Move 690 declares a skill card without
   doing the skill (the card used while holding a direction), playing only 054; a skill card used
   without a direction does the skill itself. By decision, skill and system card rows leave out 054 when the
   card has other sounds, and spell rows leave out 023 unless asked for. A card whose sounds vary (Reisen's
   Elixir explodes on every fourth use; Sanae's Omikuji Bomb has four fortunes) lists each outcome in
   full, separated by `>`. The gallery plays them in turn, one more each time the card plays, and names
   it; the offer screens will play only the first. By decision, outcomes are never random.
   The script leaves such rows alone unless given `--replace`.
   Original plan: highlighting an offer
   plays the sound that card makes in a match, e.g. the sound of the spell itself. Feasible in part:
   SokuLib exposes the game's sound load/play functions (`ADDR_LOAD_SOUND` 0x401AF0, `ADDR_PLAY_SOUND`
   0x401D50) and each character's sound table in a match (`CharacterManager.soundTable`, `playSE`).
   The missing piece is which sound belongs to which card: `spellcard.csv` does not say. It lives in
   each character's animation (pattern) data, as sound cues on the move's frames. Needs that format
   reverse-engineered, or a hand-made card-to-sound table (about 480 cards). Investigate feasibility
   first. Details: play only once the cursor settles, so fast scrolling does not stack sounds; a
   setting to turn it off, cosmetic so not in the fingerprint (like `show_deck`). Cheaper fallback
   if it stalls: one shared game sound per card type (e.g. `SFX_SPELL_ACTIVATE` for spells).
9. (built for v0.4, played online between two copies on one PC, 2026-11-20) Time limit: `[reward] time_limit`, off (0) by default for now
   (the developer's choice), 0 to 90 in steps of 15, hashed only when set. Counted in frames in the run's `offer_frames` (saved every second),
   on every offer screen. On time-out `sts_run_reward_time_out` keeps the offer and cards already
   chosen and rolls the rest from the run's RNG. With a limit, the deck screen after a reward moves on
   by itself after 15 s (not saved; it only shows a change already made). Original plan: Not needed technically (vanilla waited 28 minutes);
   it keeps the opponent from waiting on a black screen. A setting, e.g. `[reward] time_limit`
   in seconds, 0 = none and the default, part of the fingerprint (it changes how a reward can turn
   out). On time-out, keep any choices already made and make the rest at random: the offer, then
   the cards or type to replace. The random choices come from the run's saved RNG, so restarting
   cannot reroll them, but the save must also record the time used, or restarting the game resets
   the timer (a small save format change). In compatible mode only the local player sees the timer;
   showing it to a modded opponent needs full mode messages. See how much players stall in phase 2
   before setting a default.
10. (built for v0.4, played in game; it looks right, 2026-11-25) Roulette: `[reward] roulette`,
   on by default, not a rule (left out of the fingerprint and of a run's saved rules). With
   `offer_pick=random` the core keeps the offers it picked from in `sts_run.rolled` (not saved, so a
   restart just shows the pick); on a time-out with no offer chosen, `sts_run_random_offer` draws the
   index first (the same draw the time-out makes) so the roulette can show it, and the time-out
   takes it afterwards. 20 steps from 3 to 19 frames apart, then a pause: 3 s; the start is chosen so
   the last step lands on the pick. The sweep (`ST_SWEEP`) shows cards that left at random (replace
   random or by type, or a time-out that picked some): the deck as it was, the cards that could leave
   lit, the cursor slowing onto each that left and marking it, 150 frames of movement shared between
   them. The change is made and saved first; g_run.deck stands in for the old deck while it shows.
   Original plan: instead of jumping to the deck-changed screen, the cursor
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
   between replace and skip (No offers kept for debugging; it will become the add and remove mode,
   name pending). Reorganised by decision (2026-11-25), keeping every setting in the file and core,
   as items may change them later: every value goes round past either end (numbers too, in
   `sts_settings_step`); cards per pick and per offer (3) and fewest and most copies (1 and 4, the
   new default) are off the screen; card types step through Weighted, One of each, and Random only;
   when you get an offer leaves out Random (so its chance shows only when a file sets Random); the
   Sounds menu is gone (`card_sounds` stays `use_card`); "Player chooses ..." reads "Choose an offer", "Choose cards", "Choose a type"
   (an offer, not a card, as it can give several copies); on every screen the key prompts put
   directions on one line and buttons below, the last line at the bottom (`add_key_prompts` in
   `mod/screens/screen_draw.cpp`; a third line for the run toggle on the character menu and deck screen).
   A value the screen leaves out still shows when the file sets it, until stepped past
   (`value_on_screen` in `mod/screens/settings_screen.cpp`). Saving
   runs `sts_settings_validate` and the starter check, lists the runs whose rules would change and
   asks, refuses if the file changed on disk since the screen opened, then writes through
   `sts_settings_rewrite` (only changed values; missing settings added to their section) to a
   temporary file moved over the old one. The settings reload at once: a run in use whose rules
   changed stops as at start-up (`state.txt` kept), and comes back if they are changed back.
   One set of rules online (2026-11-25, to be revisited): forcing the host's rules on both players
   would clash with run decks built under each player's own rules, perhaps beyond reconciling, so for
   now the warning at character select (v0.6) is enough. An alternative idea: the draft begins on
   connecting to the host, under the host's rules; it would get in the way of the usual character
   select, so it may be too hard.
12. (built for v0.4, played online between two copies on one PC, 2026-11-24) Match limit and score screen: `[run] matches`, 0 =
   endless and the default, up to 20 (by decision; the history keeps 100, so a finished run always
   has every match), part of the fingerprint only when set. The score screen lists the deck before
   the first match and each match; the chosen one shows the deck after it (worked out backwards from
   the final deck by `sts_run_deck_after`), with the cards it brought in outlined in green and the ones it took out in a row beside it in red;
   A opens that deck on the deck screen (g_run.deck stands in for it until B). A finished run
   skips the rules check (its rules no longer matter), and a rule change that lowers the limit to the
   matches played finishes the run when it carries on. By decision, no history files: a run's record
   lives in its save until it is started over. Original plan: `[run] matches`, part of
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
   Slay the Spire); both would be settings beside `matches`. Still undecided (2026-11-25), and kept.
   Showing the opponent's full deck (the old "opponent deck display" setting) may pair with these,
   at the end of a run.
13. (done, for v0.2) Screen background: every mod screen draws over something not meant for it
   (the stopped battle scene after a match, so plain black; the title menu before one, showing
   through the text). Now every screen draws the deck editor's gear background
   (`data/scene/select/bg`), then on run screens the character's portrait (`data/stand/<folder>`)
   at tint alpha 0x48, then a 0x60 black shade. 0x70 was too strong: white text over a light
   portrait (Komachi's apron) was hard to read. Falls back to plain black if loading fails.
14. (built for v0.4, played on one PC, 2026-11-25; still to check that two machines give the same
   draft for one character) Set seed: `[run] seed`, a word of up to 24 letters,
   digits, `-` and `_`, case-sensitive by decision, blank by default, hashed only when set. In game it
   is typed into a text box of the mod's own on Settings, Runs (A opens it; letters, digits, - and _
   read with GetAsyncKeyState; Enter keeps, Esc cancels; the screen's buttons are ignored meanwhile).
   SokuLib does not expose the game's own name entry box.
   `sts_run_seed` turns it into the run's seed; the RNG's stream is already the character. Restart
   counts live in `runs/state.txt` (core `state`, which replaced `active.txt`): per character and
   per rules fingerprint of the run discarded, by starting over or deleting, so an event's count is
   its own and changing the rules and back does not reset it. A new run copies its count into the
   save, and the score screen shows seed, fingerprint and count. Kept by decision, though nobody
   asked for the count yet. Starting over stays allowed (and counted). Original plan: Within a run rerolling is already
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
   Decided (2026-11-25): starting over is never allowed in an event, so an event's rules must be
   able to turn it off (not built yet). A player can still delete the run's save by hand; that stays
   the way out of an emergency such as a misclick, and is not counted.

15. (idea, 2026-11-25; offers built 2026-11-26, the look not yet) Card rarity, as in Slay the
   Spire: Common, Uncommon and Rare, a tier for every card chosen and balanced by the community for strength, NOT the game's own rarity
   column (that drives unlocks and says nothing about strength). Rare cards are the strong ones a deck is
   built around (e.g. Sakuya's Killing Doll and Private Square); without tiers a strong card turns up as
   often as any other and is an instant pick every time. Decided so far: Rare appears less often than
   Uncommon, Uncommon less often than Common; Rare never in the draft.
   Open: whether Uncommon can appear in the draft; whether Rare is a lower weight or only offered every N
   matches (or a chance after N). Look: Uncommon cards outlined cyan, Rare gold, wherever cards show
   (offers, deck, gallery, the change screens); maybe an effect behind a hovered card, with Tenshi's
   4-cost State of Enlightenment and Remilia's 4-cost Millennium Vampire suggested as visuals to borrow
   (their effect files are not found yet). Fits the rules-are-settings principle: the tiers would be a
   data file (e.g. a column in `data/cards.csv`, or a `data/rarity.csv` the community can edit), and how
   tiers are offered would be settings; both change what is offered, so both count towards the
   fingerprint, and offer generation filters by tier before rolling, as it does for the copy limit.
   Decided (2026-11-26), replacing "every starter card is Common": a fourth rarity, Starter, for
   Spiritual Strike Talisman (Bomb), Divine Raiment of the Dragon Fish (Hangeki), and each character's
   starter skill and spell. As in Slay the Spire, an offer's tier is rolled for the whole offer, so all
   its cards share one tier; each tier needs enough cards for variety. A first draft of the tiers is in
   `data/rarity.csv` (not read by the mod yet; the community balances it), made from card usage over
   about 10.6 million games (a spreadsheet the developer was given, `card_scores.csv`, kept in the
   gitignored `reference/`). Its four bands per character (C, UC, R, SR) became three, R and SR both
   Rare. Skills and spells are ranked within their own type (`tier_skill`, `tier_spell`): ranked against
   all of a character's cards, spells crowd the top and about 10 to 12 cards per character came out
   Rare. A system card has one rarity for every character, the average of its per-character tiers:
   A Penny Saved (Coin) Rare; Magic Potion, Tengu Fan and Sword of Scarlet Perception Uncommon; the
   rest Common. That gave each character 5 to 8 Rare cards (Coin among them), 5 to 8 Uncommon of
   their own plus 3 system, and 7 to 10 Common of their own plus 15 system. Things to handle when it is
   built: a tier can run short (cards at the copy limit drop out), so fall back to a neighbouring tier;
   with one of each card type, a Rare offer's system card is always Coin, Bomb or Hangeki (accepted);
   and Common is mostly system cards. The developer then adjusted the tiers by hand (each character
   now has 5 to 7 Rare cards of their own, Patchouli the most). Decided (2026-11-26): Starter cards
   are offered; Bomb and Hangeki count as Rare, being strong, and the starter skills and spells as
   Common. Counted that way, with system cards, each character has 8 to 10 Rare (17 to 21% of
   their offerable cards), 13 to 15 Uncommon, and 20 to 24 Common.
   Built (2026-11-26): the mod reads `data/rarity.csv` (built into the DLL) and rolls each pick's
   tier by weight; see `docs/design.md`. Decided the same day: draft 75/24/1 with at most one rare
   card per draft (the starter's not counting), rewards 60/30/10, and a reward's tier shared by both
   players of a match (from the match's seed). Also built with it: copy odds, default halving
   (8:4:2:1), so more copies are rarer. All of these are settings only code sets: settings.ini now
   matches the screen. Still open: Rare as only every N matches instead of (or as well as) a weight;
   the look (outlines, effects); whether any of these go on the in-game screen. To check online:
   that host and client get the same seed, so the same tier.
   Look, built (2026-11-26): each offer's box is outlined in its rarity's colour (common grey,
   uncommon cyan, rare a deeper gold than the cursor), on the draft and reward screens only; the
   selected box keeps the cursor's fill, with a double outline. For the effect behind a card, a
   debug-build probe logged each distinct frame of both players' objects in Practice mode,
   by image index in `data/character/<name>/<name>.pat` (a version 5 pattern: u8 version, u16 image
   count, then 128-byte names). It is not committed: it is kept with the archive tools in the
   gitignored `reference/effect_probe/`, as the sound probe is, with a README on putting it back.
   Found and built the same day (see findings.md, card effect sprites; `mod/screens/offer_effects.cpp`):
   by decision, Remilia's flames loop behind uncommon offers (Remilia Scarlet, for the mod's name) and
   Tenshi's burst plays behind rare ones (Tenshi, for the weather of Scarlet Weather Rhapsody), always,
   not only under the cursor. With the probe's second run, both follow the game's timing from when the
   offers appear: Remilia's flames fade in over 26 frames and loop every 40; Tenshi's glow, ring and
   sparks play once (25, 42 and 50 frames) and her lightning repeats every 28 frames. Each spell's
   sound plays as its effect appears (uncommon: Millennium Vampire's; rare: State of Enlightenment's),
   through the card sounds, so it follows that setting. Tried and removed the same day: one offer of
   each rarity per pick, and the draft's first three picks forced to common, uncommon and rare; both
   were for seeing the outlines and effects.

16. (idea, 2026-11-25; by decision, well after the open beta) Status and curse cards, as in Slay the
   Spire: universal cards, not tied to a character. Status cards bloat the deck so a hand has less to
   work with, sometimes with an extra effect; the simplest does nothing but cost the time and meter to
   use it. Curse cards are usually unplayable, sometimes cannot be removed, and give a debuff, usually
   while in the hand. Like items (roadmap 6), they change the match, so: full mode only, both machines
   must know both decks' extra cards before the first frame, and any debuff must survive Giuroll's
   rollback. Everything up to v0.6 is still playable with only the game's own elements; this would be
   the first that is not. Hard parts, none investigated: card IDs the game does not have (a deck with an
   unknown or illegal card is silently dropped, see the findings), their art and text, how a card that
   does nothing (or cannot be played) is used up, and the deck-loading patches. Out of scope by decision:
   new skill, spell or system cards, which would need new moves, effects and sounds.

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
