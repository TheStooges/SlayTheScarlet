# Changelog

What changed in each release of Slay the Scarlet. Releases are on the
[Releases page](https://github.com/TheStooges/SlayTheScarlet/releases).

## 0.3 - unreleased

### New

- A settings screen: press F7 and choose Settings under the characters to change `settings.ini` in
  game. Only changed values are written, so the file keeps its comments. Saving checks the settings
  work, and names any started runs whose rules would change before it saves. The new rules apply
  straight away. The run in use stops if its rules changed, and comes back if they are changed back.

### Changed

- Pressing the start over button twice on the character screen now deletes the run and stays on that
  screen, instead of going straight into a new draft. Opening the character starts one. The button
  does nothing on a character with no run. The deck screen still starts over as before.
- The draft always adds one card per pick until the deck is full: 20 picks, or 12 with the starting
  deck. `picks`, `copies_min` and `copies_max` are no longer settings in `[draft]`; an older
  `settings.ini` that lists them still loads, and they are ignored. Rewards keep their copy range.
- Cards per pick and per offer go up to 3, as many as the offer screen fits.
- The starting deck's cards are built in. The card lists in `[starter]` are no longer settings; an
  older `settings.ini` that lists them still loads, and they are ignored. Only `enabled` remains.
- `who_picks` now reads as when you get an offer, from your own result: `both` (win or lose), `winner`
  (only if you win), `loser` (only if you lose) or the new `random` (a chance, win or lose, set by
  `random_chance`, out of 100, 50 by default). Each player's mod applies it alone, so nothing is
  exchanged with the opponent.
- `who_picks=nobody` is gone, and an older file that says it reads as `both`. Its automatic swap is now
  two settings: the new `offer_pick=random` (one of the cards offered is picked for you) together with
  `replacement=random`.

## 0.2 - 2026-11-08

### New

- Japanese menus on the Japanese game (`th123`). The mod's text follows the game's language, and the
  translation is in `lang/ja.txt`. Under the English translation (`th123e`) the menus stay English.
- An optional starting deck, off by default. With `[starter] enabled=true` in `settings.ini`, each
  draft starts from 2 copies each of Spiritual Strike Talisman and Divine Raiment of the Dragon Fish,
  plus 2 copies each of a default skill and a cheap spell card chosen for each character, and 12 picks
  fill the rest. The cards can be changed in the same section. The character screen says when it is on.
- The character screen can start or stop using the selected character's run, with the same button as
  the deck screen.
- The mod's screens have a background: the game's gear backdrop, with the run character's portrait
  faded in, instead of plain black or the title menu showing through.

### Changed

- Left and right wrap around within a row on the character screen and in the deck grid, as up and
  down already did within a column.
- Starting over makes a sound on the first press as well as the second, so it is noticed.
- Card descriptions follow the line breaks the game uses, in English and Japanese, and the text of
  every card type now starts at the same height (skill text sat lower).
- The list of deck changes shows card names only.

### Fixed

- A disconnect during an offer no longer loses anything. The offer stays on screen in place of the
  game's network menu, which appears once the offer is taken. If a match still finishes while an offer
  waits, its reward is queued behind that offer.

### Online

- Tested between two players who both have the mod, as host and as client. Each plays their own run.

### Upgrading

- Replace `SlayTheScarlet.dll` and `cards.csv`, and add the `lang` folder. Keep `settings.ini` and the
  `runs` folder. To use the starting deck, copy the `[starter]` section from the new `settings.ini`
  into yours.
- Runs from 0.1 carry on. Runs saved by 0.2 do not load in 0.1.

## 0.1 - 2026-11-07

The first test release.

### Runs

- Press F7 on the main menu to choose a character. Each character has one run, saved in the
  `runs` folder.
- Draft a starting deck: 20 picks, each a choice of 3 cards. The deck so far is shown while you
  draft, unless `show_deck=false`.
- The deck screen lets you look through a run's deck, start or stop using it, or start over.
  Starting over needs two presses, so one slip cannot lose a run.
- Only one run is in use at a time. It replaces Deck 1 of its character in memory only, so your
  real decks, profiles and `score123.dat` are never changed.

### After a match

- After a VS match played with the run deck, the game's card unlock screen is replaced by a card
  offer: choose 1 of 3 cards, then choose which card in your deck it replaces, with a confirm step.
- An offer is saved as soon as it appears, so closing the game brings back the same cards rather
  than new ones. There is no time limit.
- A match on another character or deck slot is a normal match, with the usual unlock screen.

### Online

- Works online against a player without the mod, with Giuroll. The run deck is sent as a normal
  deck, and the opponent sees the usual black screen while you choose an offer.
- Offline, only player 1 uses the run deck.

### Settings

- Every rule is in `settings.ini`: draft length, offers per pick, copies per offer, which card
  types appear and how often, who gets an offer after a match (both players, winner, loser, or
  nobody for a random change), and whether a reward replaces chosen cards, a card type, or random
  cards.
- A run made under different rules can only be started over, unless `allow_rule_changes=true`
  lets it carry on when its deck is still legal.

### Known limits

- Decks are always 20 cards with at most 4 copies of a card, so `size_min`, `size_max` and
  `copy_limit` have no effect yet.
- The `add` and `remove` reward modes act as `replace`.
- Pick visibility, opponent deck display and items have no effect yet.
- Runs are refused while EasyDecks or InfiniteDecks is loaded, as their editors could save a run
  deck over a real deck.
- Two players who both have the mod have not been tested together.
