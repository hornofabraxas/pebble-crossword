# tools/lab: the puzzle generator and clue pipeline

Every bundled puzzle (`puzzles/chal_*.xd` and `puzzles/peace_*.xd`) comes from
here: `lab_spec.py` is the master source for all of them, and `emit.py` writes
the `.xd` files from it. Do not hand-edit the `.xd` files.

Setup once: `uv venv tools/lab/.venv && uv pip install --python tools/lab/.venv/bin/python wordfreq`
(the venv is gitignored). `score.py` and `gen.py` need it; `check_clues.py`,
`emit.py` and `../xd2blob.py` run on a plain `python3`.

- `lex.py`: the shared helpers: `forms(word)` (the word plus its plain
  inflections stripped, the rule every other script goes through) and
  `read_clues()` over `corpus/xd/clues.tsv`.
- `score.py`: familiarity per answer from `corpus/xd/clues.tsv` (log of clue
  usages, weighted to 1990+), distinct-clue count, how often the word appears
  as a token inside clue text, and the wordfreq Zipf frequency in ordinary
  English (taken over the word and its plain inflections). Writes `fam.json`
  (gitignored, 20 s) with a `fields` header that gen.py checks, so a stale
  file fails loudly instead of running with the Zipf gate off.
- `gen.py <seed> <minis> <regulars> [mini_size=7]`: eligibility (curated
  nzfeng list, or a corpus-familiar extra), a Zipf floor (`LAB_ZIPF_CORE` 3.2,
  `LAB_ZIPF_EXTRA` 3.6; this is what keeps SERA, INST and STA out), a
  crosswordese index, a ban list, symmetric 5x5 or 7x7 patterns made on the
  fly (7x7: 4 to 11 blacks, 12 to 18 words, at least six entries of five
  letters or more; 5x5: the four classic shapes) and the bundled 15x15 patterns, and an MRV backtracker with
  look-ahead value ordering, arc consistency, random restarts, a shared-stem
  rule and a glue cap (`LAB_BUDGET` nodes per attempt, `LAB_PATTERN_TIME`
  seconds per pattern). Prints each fill with per-word stats; writes
  `fills_<seed>.json`. Run several seeds in parallel and pick the cleanest fill
  by hand: few three-letter words, no shared stems, extras that are ordinary
  names.
- `HARD_CLUES.md`: the house style for hard-register clues. Clue writers get
  this file, the grid and the answers, and nothing from the corpus.
- `check_clues.py [spec]`: validates a spec before emit: every slot clued, 60
  chars max, the answer or its root absent from the clue, no answer repeated
  across the spec, and two corpus-reference checks that read
  `corpus/xd/clues.tsv` without printing any of it: "close to a published
  clue" (content-token overlap of 0.6 or more with any published clue for that
  answer) and "stock clue" (every content token among the answer's most common
  published clue tokens). Swap or rewrite anything it flags. Two-word clues on
  very common short answers (TRY, RATE) can tie a published clue by accident;
  a 0.67 on a clue you composed yourself is a judgment call, not a copy.
- `lab_spec.py` + `emit.py`: the chosen grids with the final clues, run
  through the same structural checks (`check_clues.clue_errors`) and written as
  `.xd` with their `Difficulty: peaceful` or `challenging`. Then
  `python3 tools/xd2blob.py` repacks the bundle. CI checks both steps are
  current, so a spec edit without an emit and a repack fails the build.

Findings: the curated list alone cannot fill a 15x15 (too few 3s and 4s);
corpus-familiar names make it fill; with the Zipf floor about one 15x15
pattern in six fills inside a 60 s / 60k-node budget (one in three at 150k
nodes and 120 s), so run six seeds in parallel. 7x7 minis with the tighter
pattern fill in seconds to a minute. Clue writers, working from the style
guide alone, land on a published angle for roughly a third of entries on the
first pass and for nearly every three-letter word; the checker is what makes
the set original, not the prompt.
