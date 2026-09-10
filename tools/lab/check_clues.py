"""check_clues.py: validate a lab spec's clues before emit.

Structural checks per clue (clue_errors, also used by emit.py): present,
MAX_CLUE_CHARS max, and the answer absent from the clue: no clue token shares
an inflected form with the answer, and an answer of five letters or more is
not a substring of any token. Across the spec: no answer repeated.

Corpus checks use corpus/xd/clues.tsv only as a reference: "close to a
published clue" (content-token overlap with any published clue for that
answer) and "stock clue" (every content token among the answer's most common
published clue tokens, i.e. the dictionary-lookup register). Prints only our
clue text and the flag; corpus text never leaves this script.

Usage: .venv/bin/python check_clues.py [lab_spec.py]"""
import collections, importlib.util, os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
sys.path.insert(0, HERE)
import lex

STOP = set("a an the of to in on at for with by from as is are was were be been it its this that or and but not no so than then who whom whose what which one ones some any all may might can could do does did has have had into over out up off".split())
TOK = re.compile(r"[a-z]+")
def content(text): return {min(lex.forms(t), key=len) for t in TOK.findall(text.lower()) if t not in STOP and len(t) > 1}

def load_spec(path):
    s = importlib.util.spec_from_file_location("lab_spec", path); m = importlib.util.module_from_spec(s); s.loader.exec_module(m)
    return m.PUZZLES

def clue_errors(ans, cl):
    """Structural problems with one clue for one answer, as short labels."""
    if not cl: return ["MISSING"]
    out = []
    if len(cl) > lex.MAX_CLUE_CHARS: out.append("LONG %d" % len(cl))
    a = lex.forms(ans); toks = TOK.findall(cl.lower())
    if any(a & lex.forms(t) for t in toks) or (len(ans) >= 5 and any(ans.lower() in t for t in toks)): out.append("ANSWER IN")
    return out

def check(puzzles, corpus=True):
    errors = warns = 0
    want = {}   # answer -> [(puzzle, key, clue)]
    for p in puzzles:
        grid = [r.replace(' ', '') for r in p["grid"]]
        for dkey, num, ans in lex.probe_entries(grid):
            key = "%s%d" % (dkey, num); cl = p["clues"].get(key)
            for e in clue_errors(ans, cl): print("%-10s" % e, p["file"], key, ans, "|", cl or ""); errors += 1
            if cl: want.setdefault(ans, []).append((p["file"], key, cl))
    # An answer reused across different puzzles is fine (a solver sees one at a
    # time); only flag it as informational, and only when it repeats WITHIN one puzzle.
    for ans, uses in want.items():
        files = [u[0] for u in uses]
        if len(files) != len(set(files)): print("DUP-IN-PUZZLE", ans, [u[:2] for u in uses]); errors += 1
        elif len(uses) > 1: warns += 1  # cross-puzzle reuse: counted, not printed
    # Ship gate 1: no two slots anywhere in the bundle carry the identical clue
    # text. The whole app ships together, so a solver moving between puzzles
    # would see the same clue twice. Normalised by case and surrounding space.
    seen = collections.defaultdict(list)
    for p in puzzles:
        for key, cl in p["clues"].items():
            if cl: seen[cl.strip().lower()].append((p["file"], key))
    for cl, uses in seen.items():
        if len(uses) > 1: print("DUP-CLUE  ", uses, "|", cl); errors += 1
    # Ship gate 2 (opt-in): within a difficulty, no answer appears more than the
    # cap. Repeats across difficulties are separate shelves and not counted here.
    # Enabled by setting LAB_ANSWER_CAP; unset/0 = off, so retrofitting the cap
    # onto an existing set is a deliberate choice, not a surprise build break.
    cap = int(os.environ.get("LAB_ANSWER_CAP") or 0)
    if cap:
        by_diff = collections.defaultdict(collections.Counter)
        for p in puzzles:
            grid = [r.replace(' ', '') for r in p["grid"]]
            for dkey, num, ans in lex.probe_entries(grid):
                by_diff[p.get("difficulty", "challenging")][ans] += 1
        for diff, cnt in sorted(by_diff.items()):
            for ans, n in sorted(cnt.items(), key=lambda x: -x[1]):
                if n > cap: print("OVER-CAP  ", diff, ans, "used", n, "> cap", cap); errors += 1
    if not corpus: return errors, warns
    pub = collections.defaultdict(list)
    for _, a, clue in lex.read_clues():
        if a in want: pub[a].append(clue.lower())
    for ans, uses in want.items():
        clues = pub.get(ans, [])
        sets = [content(c) for c in clues]
        freq = collections.Counter(t for st in sets for t in st)
        top = {t for t, _ in freq.most_common(8)}
        for (pf, key, cl) in uses:
            mine = content(cl)
            if not mine: continue
            close = max((len(mine & st) / len(mine | st) for st in sets if st), default=0.0)
            if close >= 0.6: print("CLOSE %.2f" % close, pf, key, ans, "|", cl); warns += 1
            elif mine <= top and len(clues) >= 5: print("STOCK     ", pf, key, ans, "|", cl); warns += 1
        if not clues: print("NEW ANSWER", ans, "(no published clues to compare)")
    return errors, warns

if __name__ == "__main__":
    puzzles = load_spec(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "lab_spec.py"))
    have_corpus = os.path.exists(lex.CLUES)
    if not have_corpus:
        print("(reference corpus absent: structural + duplicate gates enforced; advisory CLOSE/STOCK checks skipped)")
    errors, warns = check(puzzles, corpus=have_corpus)
    print("errors:", errors, "warnings:", warns)
    sys.exit(1 if errors else 0)
