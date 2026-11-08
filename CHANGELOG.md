# Changelog

What changed in each release of Slay the Scarlet. Releases are on the
[Releases page](https://github.com/TheStooges/SlayTheScarlet/releases).

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
