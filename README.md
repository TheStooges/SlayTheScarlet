# Slay the Scarlet

A deck-building run mod for Touhou 12.3 Hisoutensoku, inspired by Slay the Spire and by the OCE
Hisoutensoku community's "Slay the Scarlet" wauvent.

You draft a starting deck for one character, one card at a time. After each match you are offered
new cards to swap in, so the deck changes as the "run" goes on.

**This is an early test release.** Online play has only been tested on one PC so far, between a
player with the mod and a player without it, so expect rough edges and please report problems.

## Requirements

- Hisoutensoku **v1.10a**. The English translation (`th123e`) is fine.
- **SWRSToys**, the mod loader included in the usual community bundles.
- A fully unlocked save (`score123.dat`), as the community normally uses. Other saves have not been tested.
- **EasyDecks and InfiniteDecks must be disabled.** Their editors could save a run deck over your real
  deck, so runs are refused while either one is loaded.

Giuroll is supported for online play. Other common mods should not need disabling.

## Install

1. Copy the `SlayTheScarlet` folder into the game's `modules` folder.
2. Add this line to `SWRSToys.ini`, in the game's folder:

   ```
   SlayTheScarlet=modules/SlayTheScarlet/SlayTheScarlet.dll
   ```

3. In the same file, put a `;` at the start of the `EasyDecks` and `InfiniteDecks` lines if they are
   enabled.
4. Start the game. The main menu should show `F7: Slay the Scarlet` in the bottom right corner.

To uninstall, remove the line and delete the folder. The mod never changes your profiles or
`score123.dat`, so your real decks and your save are untouched.

To update, replace `SlayTheScarlet.dll` and `cards.csv`. Keep `settings.ini`, which holds your rules,
and the `runs` folder, which holds your runs.

## How to play

The buttons for each screen are listed at the bottom of that screen, using your own key config.

1. **Press F7 on the main menu** and choose a character. Each character has one run.
2. **Draft your deck.** Each of the 20 picks is a choice of 3 cards. A finished draft goes into use
   straight away.
3. **Play a VS match, offline or online, as that character on Deck 1 (![green Deck 1 symbol](images/deck1.svg)).**
   Your real Deck 1 is safe: the run deck only replaces it in memory, while the run is in use.
4. **Take an offer after the match.** Choose 1 of 3 cards, then choose which card in your deck it
   replaces. You cannot replace a card with another copy of itself.
5. **Back to character select.** Your next match uses the changed deck.

Only one run can be in use at a time. Use the deck screen, under F7, to look through your deck, stop
or resume using a run, or start over. Starting over needs two presses, so one slip cannot lose a run.

A match on a different character or deck slot is a normal match: no offer, and your run is unchanged.

An offer cannot be skipped. It is saved, so closing the game brings back the same cards rather than
new ones. There is no time limit for now.

### Online

Only you need the mod. Your opponent sees a normal deck, and after a match sees the usual black
screen while you choose. Each machine sends its own deck when the next match starts, so nothing about
the online protocol changes.

Offline, only player 1 uses the run deck.

## Settings

Rules are set in `settings.ini`, which lists and explains every option. You can change the draft
length, how many cards are offered, which card types appear and how often, who gets an offer after a
match (both players, winner, loser, or nobody for a random change), and whether you replace chosen
cards, a card type, or random cards.

The defaults are provisional, until the community has played with them.

If you change the rules, a run made under the old rules can only be started over. To let runs carry
on under the new rules when their deck is still legal, set `allow_rule_changes=true`.

## Current limits

- Decks are always 20 cards with at most 4 copies of a card. 
  The size_min, size_max and copy_limit settings have no effect for now.
- The `add` and `remove` reward modes act as `replace`, because the game rejects decks of other sizes.
- Pick visibility, opponent deck display and items are planned, and have no effect yet.
- Two players who both have the mod have not been tested together yet. Each should simply manage their
  own deck.

## Reporting problems

Send `sts.log` from the `SlayTheScarlet` folder, along with what you were doing. It is rewritten each
time the game starts, so copy it before restarting. It contains no IP addresses or player names.

## Building from source

This section is for developers. You need Visual Studio with "Desktop development with C++", CMake and
git. The mod must be built as 32-bit with MSVC, from the **x86 Native Tools Command Prompt**:

```
git clone https://github.com/TheStooges/SlayTheScarlet.git
cd SlayTheScarlet
git submodule update --init
mkdir build
cd build
cmake .. -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build . --target SlayTheScarlet
```

The DLL, `cards.csv` and `settings.ini` end up in `build/mod`.

`core/` holds the rules, offers and run saves as portable C with no game dependencies, and `tests/`
covers it. `mod/` is the Windows DLL: hooks, screens and deck handling. `CLAUDE.md` has the design
notes and reverse-engineering findings.

## Credits

This is an unofficial fan mod, and you need your own copy of the game.

- **Touhou 12.3 Hisoutensoku** is by Team Shanghai Alice and Twilight Frontier.
- **Slay the Spire**, by Mega Crit, inspired the run format.
- **[The original "Slay the Scarlet" event](https://www.youtube.com/watch?v=aq1PMH7CWYc)** was hosted
  by the OCE Hisoutensoku community before this mod automated it. Stooge got its modified install
  working, and created the
  [card collecting notes and flowcharts](https://drive.google.com/drive/folders/1SMToH54savI-kohoauLHBUqlog8rj2mr)
  by hand.
- **[SokuLib](https://github.com/SokuDev/SokuLib)**, by Gegel85 and the SokuDev contributors, provides
  the reverse-engineered game structures and addresses. It is built into the DLL under the MIT
  licence: see `lib/SokuLib/LICENSE` in the source, or `LICENSE-SokuLib.txt` in a release.
- **[ModTemplate](https://github.com/SokuDev/ModTemplate)**, from SokuDev, was the project's starting
  point.
- **[SokuMods](https://github.com/SokuDev/SokuMods)**, from SokuDev, was a reference for how mods are
  put together, and **[HandicapMod](https://github.com/SokuDev/HandicapMod)** for how decks and
  netplay packets work.
- **OnlinePractice**, from OCE Hisoutensoku, was a reference for which parts of the game a netplay mod
  needs to hook.
