# Design decisions

How the rules and the run save work, and the decisions behind them. The project rules are in
`CLAUDE.md`; the reverse-engineering findings in `findings.md`; plans in `roadmap.md`.

## Rules and offers

`core/` modules: `deck` (model, validation), `pf` (profile read/write), `pool` (card table), `rng`
(PCG32), `offer` (offer generator, applying a taken offer), `settings` (`.ini` parsing, validation,
compatible-mode override, fingerprint), `run` (draft and reward state machine, save format), `state` (runs/state.txt: run in use,
restart counts), `lang`
(translation files, checking each keeps its English line's %d/%s blanks), `sounds` (the card sound
table, `data/card_sounds.csv`), `util`.
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
start over, or go back to the run's own rules (suggested by a community member, v0.4): offered when
`sts_run_rules_restorable` (the run saved its rules, format 3, and they still give its fingerprint).
It first shows each setting that would change (named as the settings screen names it) and the other
runs that would then show Rules changed, then rewrites `settings.ini` with those rules, keeping the
settings that are not rules. With
`allow_rule_changes=true` (off by default, not part of the fingerprint) they may also carry on under
the new rules if the deck is legal under them; offers on screen survive this if still possible, so
editing the settings is not a free reroll.

A match finished while an offer still waits (see the disconnect finding in findings.md, under netplay) does not lose
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

## Player 2, in local VS (built for v0.5, not yet played in game)

By decision, each profile keeps its own runs, so two players on one PC each have a run in use: player 1
on the left with `profile1`, player 2 on the right with `profile2`, in VS Player only (by decision,
player 2 never plays online). Runs live in `runs/<profile>/` (`run_XX.sav` and a `state.txt` with the
run in use and the restart counts), the folder named after the profile's file without `.pf`, with
anything but letters, digits, spaces and `- _ ( ) .` replaced and a hash added if anything was. The
gallery's cards found and its unlock stay in `runs/state.txt`, shared. Runs from before (in `runs/`
itself) are not picked up, by the rule on old saves. Player 2 on the same profile as player 1 has no
runs of their own. In `mod/run/run_deck.cpp`, `select_player` swaps the run-in-use values, so the rest of
the mod handles one player at a time; character select keeps both decks in place, the battle frame
works out each side's result, and `maybe_start_reward` gives player 1's reward and then player 2's,
straight after on the same post-match screen. The F7 menu has a Player 1 / Player 2 switch above the
characters; the screens, and the button names in their hints, follow the selected player.

Found with a probe (v0.5): each profile's own input (`profile1.manager.input`, `profile2.manager.input`)
does not update on the title screen, at character select or after a match, so a screen cannot read
player 2's controls alone. The shared `inputMgrs.input` updates everywhere and carries both players'
buttons (player 2's work the game's menus), so the mod's screens follow it: either player's buttons
work them. A profile's file and name (`profile.file`, `profile.name`) are set from the first title
frame, and player 2's is the one last used.
