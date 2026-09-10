# Changelog

Player-facing changes. Versions are `major.minor`.

## v1.0

- **1.0.** The first full release: 200 original puzzles, voice or type input, and
  the modeless controls settled in.
- **The app is now "Crosswords."** The launcher entry reads Crosswords, plural.

## v0.38

- **200 puzzles.** The bundle grows from 80 to 200: 100 minis (7x7) and 100
  regulars (15x15), an even 50 Peaceful and 50 Challenging of each size. Every
  grid is generated and every clue is written for this project.
- **More variety, less repetition.** No answer shows up more than a handful of
  times across the whole set, and no two clues read the same. A pass over the
  Challenging puzzles also swapped out plain dictionary-style definitions for
  clues that make you work a little harder.

## v0.37

- **"After every word" checking now guides you to what's left.** When you check
  after every word and the grid is full but some answers are wrong, Up and Down
  (and swipes) now step through the words still to fix, the same way checking at
  the end does. Correct, locked words are skipped, so you land straight on the
  ones that need another look.
- **A full-screen finish.** Solving a puzzle now fills the screen with SOLVED! /
  CONGRATULATIONS! and the time you took, instead of a small banner across the
  middle.
- **Puzzles remember your place.** Leave a puzzle on, say, 7 Across and the next
  time you open it you come back to 7 Across, rather than the top of the list.

## v0.36

- **The Controls cards page one way.** A swipe, a tap, or the select button moves
  forward through the cards, and stepping past the last one closes them. BACK
  leaves at any time. Paging no longer loops back to the first card, so you always
  see each card in order.

## v0.35

Smoother slides and a first-run tour.

- **Changing clues is smoother.** The slide when you move between clues now glides
  cleanly instead of stuttering.
- **The crossing-word slide follows the clue's direction.** Tapping a letter to
  see the crossing word slides in from the side that matches it: Across from the
  left, Down from the right, and back the other way.
- **The Controls cards slide too.** Paging through the Controls uses that same
  page-turn: the next card comes in from the right, the previous from the left.
- **A first-run tour.** The first time you open the app, the three Controls cards
  walk you through the gestures and buttons, then leave you at the main menu.

## v0.34

More polish on the Controls screens.

- **The button guide has a title.** The buttons card now reads "BUTTON CONTROLS",
  like the other help cards.
- **A bigger "all clues in menu" line.** The reminder at the bottom of the
  crossing-word card is larger and easier to read.
- **A smoother clue slide.** The slide when you change clues plays over a few
  more frames, so it glides instead of jumping.

## v0.33

A clearer Controls guide.

- **The button guide looks native now.** On the Controls screen, each button has
  a rounded tab tucked flush against the edge of the screen right where that
  button sits, with a plain arrow, checkmark, or back arrow instead of the old
  icons. The labels are bigger and bold, so it reads at a glance.

## v0.32

New typing and cleaner ways to move around.

- **Swipe up and down to change clues.** A swipe now moves between clues (the
  screen slides the way your finger goes, and faster than before). The up and
  down buttons still do the same thing.
- **A letter box opens the crossing word.** Tap a square to focus it, then tap it
  again to jump into the word that crosses there. Swiping is no longer tied up
  with the crossing word.
- **Squares select on release.** A tap picks a square only when you lift your
  finger, so a swipe that happens to start on a box just changes the clue instead
  of flickering a square first.
- **A bigger letter picker.** Dragging across the clue now opens a full-screen
  picker: the letter sits big in a green band up top, and the whole area below is
  yours to drag through the alphabet. Tap the right side to step one letter on,
  the left side to step one back, and tap the letter itself to drop it in and move
  to the next square.
- **Controls, in three cards.** The old how-to screen is now a set of cards
  (swipe, buttons, other clues) under a Controls row in Settings.
- **Skip filled squares is on by default.** New installs now jump past squares
  that already hold a letter while you type.

## v0.31

One way to play, no modes.

- **No more Speak / Type switch.** The controls are the same all the time: press
  SELECT to speak an answer, drag across the clue to scroll a letter, use the up
  and down buttons to move between clues, swipe a word up or down to jump to a
  crossing word, and tap a square to focus it. There is nothing left to toggle,
  and typing by dragging works whether or not your phone is connected.
- **A first-run guide.** The first time you open a puzzle, a short "how to play"
  card names the gestures. You can bring it back any time from a new row at the
  bottom of Settings.
- **A little more room.** The old mode label between the word and the clue is now
  a small grip mark, so the clue and the crossing letters get a touch more space.

## v0.30

Two fixes to the play screen.

- **Crossing letters show more often.** The small boxes above and below the word,
  the shared squares of the crossing answers, now appear on more clues. A short
  word with big letter boxes used to drop them to keep the clue at its largest
  size; now the clue steps down one size first so the crossing letters can stay.
  The letter boxes themselves are unchanged.
- **Clearing is less pushy.** When a word is marked wrong, tap a square and start
  dragging to fix it and the CLEAR WORD button now steps aside, so changing a
  single letter no longer means dismissing the button first. It returns if the
  word is still wrong once you let go.

## v0.29

Ten more Challenging puzzles, and a clue-quality pass across the whole app.

- **Ten new Challenging puzzles.** Five 15x15 regulars and five 7x7 minis in the
  hard-clue register. The library is now 80 puzzles, 50 Challenging and 30
  Peaceful.
- **Sharper clues.** 163 existing clues, the ones that read as flat, ambiguous,
  or too much like a dictionary, were rewritten to be clearer or trickier as
  their difficulty calls for. A few factual slips were corrected along the way.
- Your solved marks and in-progress puzzles carry over; the new puzzles simply
  join the shelves.

## v0.28

Every clue in the app is now unique.

- **No repeated clues.** Ninety-seven clues that appeared in more than one
  Peaceful puzzle were rewritten so that no two puzzles share a clue. The grids
  and answers are unchanged; only the wording of some clues is fresher.
- Under the hood: the build can now check that no clue is duplicated anywhere in
  the bundle, and the generator can hold a ceiling on how often any one answer
  is reused across a difficulty (groundwork for the next batch of puzzles).

## v0.27

Ten more Challenging puzzles.

- **Ten new Challenging puzzles.** Five 15x15 regulars and five 7x7 minis, all
  in the hard-clue register: misdirection over obscurity, familiar answers you
  have to work for. The library is now 70 puzzles, 40 Challenging and 30
  Peaceful.
- Your solved marks and in-progress puzzles carry over; the new puzzles simply
  join the shelves.

## v0.26

Your progress now survives new puzzles, and the shelves show how far along you
are.

- **Solved marks and in-progress puzzles survive a new bundle.** Every puzzle
  has an id now, and both follow their puzzle when a release adds, removes or
  reorders puzzles. Upgrading from v0.25 keeps what is on the watch (this
  release ships the same puzzle set); from v0.26 on, only a puzzle that is
  actually removed loses its progress.
- **The difficulty picker counts your solves.** Each row shows solved/total
  for that shelf, and when a shelf is finished, opening it says it is replaying
  a solved puzzle rather than quietly forgetting you solved it.
- **Time spent shows on the Continue list**, beside each puzzle's name.
- Under the hood: the letter scrubber and the progress store now have host
  tests, and the build checks that the bundled puzzles are packed from their
  sources.

## v0.25

Ten more Peaceful puzzles, and the two difficulties are even again.

- **Ten new Peaceful puzzles.** Five 15x15 regulars and five 7x7 minis, clued in
  the gentle register: clear, warm definitions that click without a fight.
- **Even split restored.** Thirty Peaceful and thirty Challenging, so either
  difficulty opens onto a full shelf. The library is now sixty puzzles.
- As with every change to the bundled set, in-progress bundled puzzles are
  dropped on first launch.

## v0.24

Steadier letter picking in Type mode.

- **A bead on the progress bar shows where you are in the alphabet** while the
  letter picker is open: A at the left, Z at the right. It goes as soon as you
  keep the letter.
- **A rest on a letter locks it.** Pause for a moment on the letter you want and
  a small wobble, or the sideways twitch of a lifting finger, no longer steals a
  step. The scroll picks up again once your finger has clearly moved on.
- **Slower means finer.** Creeping up to a letter now takes more of the screen
  per step than cruising through the alphabet, so the last letter is the
  easiest one to land on.

## v0.23

Ten more Challenging puzzles, and sharper clues in one of the old ones.

- **Ten new Challenging puzzles.** Five 15x15 regulars and five 7x7 minis, all
  written in the hard-clue register: misdirection over obscurity, familiar
  answers you have to work for.
- **Sharper clues in an existing puzzle.** A handful of flat, dictionary-style
  clues in one Challenging 15x15 were reworked for more bite.

## v0.22

Reveal a word, a corrected timer, and a smoother Type mode.

- **Reveal word.** Hold SELECT for the menu and pick Reveal word, behind a quick
  confirm, to fill the current answer in and lock it. Handy when a corner has you
  stuck.
- **Continue is always on the home screen.** With nothing in progress it now sits
  there greyed out instead of disappearing, so the layout stays steady.
- **The finish timer is right.** It counts the whole time a puzzle is open and no
  longer shows 0:00, or a stale time from an earlier sitting, when you solve.
- **A buzz when you keep a typed letter** in Type mode. It fires only as a letter
  is committed, never while you scroll, and has its own Typing buzz setting.
- **Smoother, steadier letter scrolling.** The drag is filtered against stray
  touch jitter, so a small wobble near your target no longer flips the letter.
- **New setting: Skip filled squares** advances past letters you already have to
  the next empty square.

## v0.21

Ten more Challenging puzzles, and the two difficulties now match.

- **Ten new Challenging puzzles**: five 7x7 minis (Mini 16 to 20) and five 15x15
  regulars (Regular 16 to 20), clued in the same misdirection-first style as the
  rest of the Challenging set. The library is now forty puzzles.
- **Even split.** Twenty Peaceful and twenty Challenging, ten of each size on
  each side, so either difficulty always opens onto a full shelf.
- As with every change to the bundled set, in-progress bundled puzzles are
  dropped on first launch.

## v0.20

Thirty puzzles, both difficulties stocked, and clue text packed tighter.

- **Twenty more puzzles**, all Peaceful: ten 7x7 minis and ten 15x15 regulars,
  clued in a new gentle register (direct definitions, still with the odd name or
  pop-culture nod). The library is now thirty puzzles, fifteen of each size.
- **Peaceful is open.** The difficulty picker no longer says "Coming soon" for
  Peaceful; both Peaceful and Challenging are stocked and show their style.
- **Clue text is compressed.** A shared-dictionary packer shrinks the bundled
  clue text by about 40 percent, so many more puzzles fit in the watch's storage
  as the library grows.
- **The app is entirely self-contained** — every puzzle is bundled on the
  watch (voice input still uses the phone for dictation).
- As with every change to the bundled set, in-progress bundled puzzles are
  dropped on first launch.

## v0.19

Ten puzzles of our own, in two difficulties.

- **The library is ten hand-clued generated puzzles**: five 7x7 minis and
  five 15x15 regulars, all built for this project. Everything plays offline
  from the watch.
- **Two difficulties.** Picking Mini or Regular opens a list headed CHOOSE
  DIFFICULTY with Peaceful and Challenging. Every puzzle today is Challenging:
  misdirection on familiar words, sprinkled with names and pop culture the way
  a good daily is. Peaceful is reserved for gentler clues and reads "Coming
  soon" until it has puzzles.
- **Menu.** New puzzle lists just Mini (7x7) and Regular (15x15); the temporary
  Playtest row is gone now that these are the real puzzles.
- As with every change to the bundled set, in-progress bundled puzzles are
  dropped on first launch.

