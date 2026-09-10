"""lex.py: the two things every lab script needs and used to re-implement.

forms(word): the word and its plain inflections stripped, lowercase, as a set;
score.py rates a word by the most common of these and check_clues.py matches
answers against clue tokens through them. A stripped form must keep four
letters (three for a bare plural) so crosswordese does not inherit a short
stem's frequency (OATER is not OAT).

read_clues(): (year, ANSWER, clue) rows of corpus/xd/clues.tsv, when that
optional reference corpus is present (it is gitignored and not shipped; the
generator itself needs only the derived tools/lab/fam.json)."""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))   # tools/lab -> repo root
CLUES = os.path.join(REPO, "corpus/xd/clues.tsv")
SUFFIXES = ("s", "es", "ed", "d", "ing", "er", "ers", "ly", "est")

def forms(word):
    w = word.lower(); out = {w}
    for suf in SUFFIXES:
        keep = len(w) - len(suf)
        if w.endswith(suf) and keep >= (3 if suf == "s" else 4): out.add(w[:keep])
    if w.endswith("ies") and len(w) >= 5: out.add(w[:-3] + "y")
    if w.endswith("ing") and len(w) >= 7: out.add(w[:-3] + "e")
    return out

def read_clues():
    if not os.path.exists(CLUES):
        print("lex: %s absent - corpus checks skipped (optional reference)" % CLUES, file=sys.stderr)
        return
    with open(CLUES, encoding="utf-8", errors="replace") as f:
        next(f)
        for ln in f:
            p = ln.rstrip("\n").split("\t")
            if len(p) < 4: continue
            try: y = int(p[1])
            except ValueError: y = 0
            yield y, p[2].strip().upper(), p[3].strip()


MAX_CLUE_CHARS = 60   # the on-watch comfortable clue-length cap used by the checker/emit

def probe_entries(grid):
    """Standard numbering of a filled grid: [(dirkey, number, answer)]."""
    rows, cols = len(grid), len(grid[0])
    def white(r, c):
        return 0 <= r < rows and 0 <= c < cols and grid[r][c] != '#'
    out = []; n = 1
    for r in range(rows):
        for c in range(cols):
            if not white(r, c): continue
            sa = (not white(r, c - 1)) and white(r, c + 1)
            sd = (not white(r - 1, c)) and white(r + 1, c)
            if sa or sd:
                if sa:
                    L = 0
                    while white(r, c + L): L += 1
                    out.append(('A', n, ''.join(grid[r][c + k] for k in range(L))))
                if sd:
                    L = 0
                    while white(r + L, c): L += 1
                    out.append(('D', n, ''.join(grid[r + k][c] for k in range(L))))
                n += 1
    return out
