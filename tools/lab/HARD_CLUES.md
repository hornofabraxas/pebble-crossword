# Hard-register clues: the house style

The thesis under test: difficulty lives in the clue, not the fill. The fill is
familiar English; the clue makes the solver work for it. These rules are ours.
Published crossword clues are never pasted into a writing prompt; the checker
(`check_clues.py`) compares against them afterwards and flags anything that
lands too close.

## Hard means misdirection, not obscurity

- **Lead the solver to the wrong sense first.** Pick a reading of the answer
  that is not the first one that comes to mind, then write a surface that
  sounds like something else. "Trip segments" for LEGS. "Diamond stops" for
  BASES. "Drinks for the table" for ROUND.
- **Part-of-speech traps.** Write a surface that reads as a noun phrase when
  the answer is a verb, or the reverse. "Follow, as a point" for SEE. "Put a
  scratch in" for MAR. "Goes through, as cash" for SPENDS.
- **Definition by consequence or context, not by synonym.** "It makes dough
  rise" for YEAST. "Weekend-welcoming letters" for TGIF. "Reaction to a paper
  cut" for OUCH.
- **Trivia, history, and pop culture, when the fact is solid and the crossing
  is fair.** "Milton's fallen angel" for SATAN, "Timbuktu's country" for MALI,
  "River through Cairo" for NILE, "Cold War divide" for IRON (CURTAIN). One
  identifying fact per proper noun; never a fact you are not sure of.
- **Clue by example, flagged.** "Merlot, e.g." for WINE, "Ruth or Aaron, once"
  for SLUGGER. "e.g.", "for one", "for short", "say" keep an example fair.
- **Colloquial and slang senses.** Reach for the casual reading: "Cool, in
  slang" for RAD, "No sweat" for EASY, "Bail on" for DITCH.
- **Sounds-like and hidden words.** A homophone ("Heard on the range?" for
  HERD) or a word buried in the surface, used sparingly so it never feels
  arbitrary.
- **A question mark marks a pun or a stretched reading.** Use it, and use it
  rarely: at most three per 15x15, one per 7x7.

## Never

- The answer, its root, or an inflection of it in the clue.
- A plain synonym, a dictionary gloss, or the most common published angle for
  the answer. If the clue could be the first line of the word's dictionary
  entry, it is not hard.
- A fill-in-the-blank unless the blank itself is the misdirection.
- Made-up facts, invented titles, or a "famous" attribution you cannot stand
  behind. When in doubt about a fact, clue the word's ordinary sense instead.
- Abbreviation signals that are not needed. If the answer is an abbreviation,
  the clue says so once ("for short", "briefly", "org.").

## Fit the wrist

- 45 characters is the comfortable ceiling; 60 is the hard cap. Short and
  sharp beats long and clever.
- The clue is read on a 200 px screen with no grid in view. It must stand on
  its own; never refer to another entry by number.
- Plain punctuation only: no em dashes, no smart quotes. Straight quotes are
  fine for titles.

## Vary the devices across the puzzle

A late-week NYT feels alive because no two clues work the same way. Do not clue
a whole grid with part-of-speech traps, or lean on trivia for every proper
noun. Across each puzzle, spread the palette: some pure misdirection, some
pop-culture or historical hooks, a bit of wordplay, an example clue, the odd
straight-but-fresh definition for breathing room. If you scan your clues and
they all pull the same trick, rewrite half of them.

## Tone

Confident, dry, a little sly. Every clue must be resolvable with certainty
once the solver has the answer: the "aha" is the whole point.

## Names and pop culture keep it fresh

A daily feels alive because it reaches past the dictionary. Where an answer
supports it, clue through a well-known film, song, show, brand, athlete,
author, or public figure, and clue a name through its best-known bearer:
"Series that made Jennifer Garner a spy" (ALIAS), "Scat legend Fitzgerald"
(ELLA), "Four-time major champ Naomi ___" (OSAKA). Two rules keep this from
turning obscure: the fact must be solid and one you are sure of, and it must be
recognizable to solvers both under and over fifty. If only one generation would
get it, pick another angle.

## The corpus checker is advisory on short answers

`check_clues.py` flags a clue whose content words overlap a published clue for
that answer. On three- and four-letter commons (ERA, OTTO, RUE, OSAKA) almost
every fair angle already exists somewhere in a corpus of millions, so a flag
there usually means "this is the natural clue," not "this is lazy." Keep a
fresh, specific, fact-based clue that happens to overlap; only rewrite when the
clue is a plain dictionary gloss. Reserve hard rejection for the longer answers,
where a flag really does mean you reached for the stock angle.
