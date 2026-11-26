# Slay the Scarlet

A deck-building run mod for Touhou 12.3 Hisoutensoku, inspired by Slay the Spire and by the OCE
Hisoutensoku community's "Slay the Scarlet" wauvent.

You draft a deck for one character, one card at a time. After each match you are offered new cards
to swap in, so the deck changes as the run goes on.

**This is a test release.** Online play has been tested over a real network, both against a player
without the mod and between two players who have it. Spectating with the mod has not been tested
with other people yet. Expect rough edges, and please report problems.

## Requirements

- Hisoutensoku **v1.10a**. The English translation (`th123e`) is fine.
- **SWRSToys**, the mod loader included in the usual community bundles.
- A fully unlocked save (`score123.dat`), as the community normally uses. Other saves have not
  been tested.
- **EasyDecks and InfiniteDecks must be disabled.** Slay the Scarlet does not work alongside them,
  so they are refused while either one is loaded.

Giuroll is supported for online play. Other common mods should not need disabling.

## Install

1. Download the zip of the latest release from the
   [Releases page](https://github.com/TheStooges/SlayTheScarlet/releases).
2. Unzip it, and copy the `SlayTheScarlet` folder into the game's `modules` folder.
3. Add this line to `SWRSToys.ini`, in the game's folder:

   ```
   SlayTheScarlet=modules/SlayTheScarlet/SlayTheScarlet.dll
   ```

4. In the same file, put a `;` at the start of the `EasyDecks` and `InfiniteDecks` lines if they
   are enabled.
5. Start the game. The main menu should show `F7: Slay the Scarlet` in the bottom right corner.

The mod's menus follow the game's language: Japanese on the Japanese game (`th123`), and English
under the English translation (`th123e`).

**To uninstall**, remove the line and delete the folder. The mod never changes your profiles or
`score123.dat`, so your real decks are untouched.

**To update**, delete the `SlayTheScarlet` folder and unzip the new one in its place. This also
deletes your rules (`settings.ini`) and your decks and progress (the `runs` folder), so copy those
back afterwards to keep them.

## How to play

Each screen lists its buttons at the bottom, using your own key config.

1. **Press F7 on the main menu** and choose a character, or Random at the bottom. Each character has
   one run.
2. **Draft your deck.** Each pick is a choice of 3 cards, and the deck is full after 20 picks. A
   finished draft goes into use straight away.
3. **Play a VS match** as that character on Deck 1 (![green Deck 1 symbol](docs/images/deck1.svg)),
   offline or online. Your real Deck 1 is safe: the mod's deck only replaces it in memory, while
   in use.
4. **Take an offer after the match.** Choose 1 of 3 offers. An offer can give more than one copy
   of its card, and you then choose that many cards in your deck for it to replace. A card cannot
   replace another copy of itself. More copies are rarer: 1 copy about half the time, and 4 about
   1 time in 15.
5. **Back to character select.** Your next match uses the changed deck.

Decks are always 20 cards, with at most 4 copies of a card.

Every card is common, uncommon, or rare, and the cards of one pick or offer are all of the same
rarity, shown by the colour of the offer's outline. Rare cards are the strongest: they come up 1
time in 10 after a match, and seldom in the draft, which gives at most one.

A match on another character or deck slot is a normal match: no offer, and your deck is unchanged.

### Runs

Each profile has its own runs, and one of them can be in use at a time. On the F7 menu, the button
shown at the bottom starts or stops using the selected character's deck. The deck screen does the
same, and lets you look through your deck or start over. The F7 menu can also delete a
character's progress. Starting over and deleting both need two presses, so one slip cannot lose
anything.

An offer cannot be skipped. It is saved, so closing the game brings back the same cards, not new
ones. If the connection drops during an offer, the game goes back to the title with the offer still
on screen, and the game's network menu appears once you have taken it.

Runs are endless unless a match limit is set. Then the last match gives no offer: the run ends and
shows its score, with your wins and losses and what each match's offer changed. Choosing a match
shows the deck as it was after it: cards it brought in are outlined in green, and the ones it took
out are shown beside them in red. The deck slot goes back to your own deck, and the score can still
be viewed from the F7 menu until you start that character over.

### Player 2

In VS Player, player 2 can play a run too, on their own profile. The Player 1 / Player 2 switch at
the top of the F7 menu chooses whose characters it shows. After a match, each player who used the
mod's deck gets their own offer, player 1 first. Online, only player 1's runs are used.

### Gallery

The Gallery, at the bottom right of the F7 menu, shows every card in the game by type and character.
A card turns face up once it has been offered to you, taken or not, or was in a starting
deck. "Unlock the gallery" shows every card at once (press it twice; it cannot be undone).

The Use Card button plays the sounds the card under the cursor makes in a match. This works on the
offers, on the deck screen, and in the Gallery.

### Online

Only you need the mod. Your opponent sees a normal deck, and after a match sees the usual black
screen while you choose. Nothing about the online protocol changes.

If your opponent has the mod too:

- Each of you plays your own run and takes your own offers. Your offers after a match are of the
  same rarity, so if one of you is offered rare cards, so is the other.
- With "Share deck changes with your opponent" on for both of you (off by default), you both see
  the cards each deck gained and lost after the match. Whoever finishes first waits, with "Girls are
  now praying...", until the other is done.
- If their rules differ from yours, character select says so at the bottom left. It does not stop
  you playing.

A spectator with the mod sees each player's deck changes under that player's side of the screen:
green outlines for cards that came in, red for cards that left. Turn this off with "Show deck
changes when spectating".

The mod only sends its own messages to machines that answer that they have it too, so players and
spectators without it are not affected.

## Settings

Press F7 and choose **Settings**, under the characters. The settings are in five menus:

- **Draft:** a starting deck, and which card types are offered.
- **Rewards:** whether there are offers at all, when you get one (after every match, only if you
  win, or only if you lose), whether you choose the card that comes in or it is picked at random,
  what leaves the deck (cards you choose, a type you choose, or random cards), a time limit, and
  which card types are offered.
- **Runs:** a set seed, a match limit (up to 20), which deck slot is used, and whether a deck
  carries on after a rule change.
- **Netplay:** sharing deck changes with your opponent, and showing them when spectating.
- **Display:** the roulette animation for random picks, and whether the deck so far is shown while
  drafting.

In a menu, up and down choose a setting, left and right change it, and the lines below explain it.
Changes are kept across menus until you save. Saving writes only what changed to `settings.ini`, so
the file keeps its comments and can be shared as a set of rules. The file holds the same settings
as the screen, and explains each one.

The defaults are provisional, until the community has played with them.

**Starting deck** (off by default): each draft starts from 8 set cards for the character: 2
copies each of Spiritual Strike Talisman, Divine Raiment of the Dragon Fish, a default skill, and a
spell card. 12 picks fill the rest.

**Time limit** (off by default): when it runs out, any choices you made are kept and the rest are
made at random. The time spent is saved, so closing the game does not give you more.

**Set seed**, for events: a word such as `OCE2026`, typed with the keyboard (Enter to keep it, Esc
to cancel; capitals count). Everyone on the same character then gets the same draft, and starting
over gives the same draft back. The score screen shows the seed, the rules fingerprint, and how many
times that character's run was started over, for a screenshot.

**Rule changes:** a run made under other rules can go back to its own rules, which puts them back
in `settings.ini`. It first lists each setting that would change, and any other characters that
would then need the same. Otherwise it can only start over, unless "Carry on after rule changes" is
on and the deck is still legal.

## Reporting problems

Send `sts.log` from the `SlayTheScarlet` folder, along with what you were doing. It is rewritten
each time the game starts, so copy it before restarting. It contains no IP addresses or player
names.

## Building from source

See [docs/building.md](https://github.com/TheStooges/SlayTheScarlet/blob/master/docs/building.md).

## Licence

Slay the Scarlet is released under the MIT licence: see `LICENSE`, or `LICENSE.txt` in a release.

## Credits

This is an unofficial fan mod, and you need your own copy of the game.

- **Touhou 12.3 Hisoutensoku** is by Team Shanghai Alice and Twilight Frontier.
- **Slay the Spire**, by Mega Crit, inspired the run format.
- **[The original "Slay the Scarlet" event](https://www.youtube.com/watch?v=aq1PMH7CWYc)** was
  hosted by the OCE Hisoutensoku community before this mod automated it. Stooge got its modified
  install working, and created the
  [card collecting notes and flowcharts][card notes] by hand.
- **[SokuLib](https://github.com/SokuDev/SokuLib)**, by Gegel85 and the SokuDev contributors,
  provides the reverse-engineered game structures and addresses. It is built into the DLL under the
  MIT licence: see `lib/SokuLib/LICENSE` in the source, or `LICENSE-SokuLib.txt` in a release.
- **[ModTemplate](https://github.com/SokuDev/ModTemplate)**, from SokuDev, was the project's
  starting point.
- **[SokuMods](https://github.com/SokuDev/SokuMods)**, from SokuDev, was a reference for how mods
  are put together, and **[HandicapMod](https://github.com/SokuDev/HandicapMod)** for how decks and
  netplay packets work.
- **OnlinePractice**, from OCE Hisoutensoku, was a reference for which parts of the game a netplay
  mod needs to hook.

[card notes]: https://drive.google.com/drive/folders/1SMToH54savI-kohoauLHBUqlog8rj2mr

#

wau [?!]
