"""Familiarity per answer from the xd clue corpus, restricted to the nzfeng core list.
fam = log(1 + weighted uses), uses weighted 1.0 for 1990+, 0.3 before; also distinct-clue count,
and (v0.18) the wordfreq Zipf frequency in ordinary English, taken over the word and its
plain inflections so TOASTERS is judged by TOASTER. Run with tools/lab/.venv (wordfreq)."""
import collections, json, math, os, re, sys
from wordfreq import zipf_frequency   # first, so a run outside tools/lab/.venv fails before the corpus pass
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lex
REPO = lex.REPO   # repo root, derived from this file's location
OUT = os.path.join(os.path.dirname(__file__), "fam.json")
if not os.path.exists(os.path.join(REPO, "corpus/core.txt")):
    sys.exit("score.py needs the reference corpus (corpus/core.txt, corpus/xd/clues.tsv) to rebuild fam.json;\n"
             "it is gitignored and sourced separately. The generator itself only needs the committed fam.json.")
words = set()
for ln in open(os.path.join(REPO, "corpus/core.txt"), encoding="utf-8", errors="replace"):
    w = ln.strip().upper()
    if 3 <= len(w) <= 15 and w.isalpha() and w.isascii():
        words.add(w)
uses = collections.Counter(); clues = collections.defaultdict(set); recent = collections.Counter(); tok = collections.Counter()
TOK = re.compile(r"[A-Za-z]+")
for y, a, clue in lex.read_clues():
    for t in TOK.findall(clue):
        if 3 <= len(t) <= 15: tok[t.upper()] += 1
    if not (3 <= len(a) <= 15 and a.isalpha() and a.isascii()): continue
    w = 1.0 if y >= 1990 else 0.3
    uses[a] += w
    if y >= 1990: recent[a] += 1
    clues[a].add(clue.lower())
def zipf(a): return round(max(zipf_frequency(f, "en") for f in lex.forms(a)), 2)
FIELDS = ("fam", "nclues", "recent", "cluetok", "core", "zipf")   # gen.py reads by these positions
db = {a: [round(math.log1p(uses[a]), 3), len(clues[a]), recent[a], tok[a], a in words, zipf(a)] for a in uses if recent[a] >= 3 or a in words}
json.dump({"fields": list(FIELDS), "rows": db}, open(OUT, "w"))
print(len(words), "core words;", len(db), "seen in corpus")
for L in (3, 4, 5, 7, 10, 15):
    xs = sorted((v[0], a) for a, v in db.items() if len(a) == L)
    print(L, "letters:", len(xs), "top:", [a for _, a in xs[-8:]], "median fam %.2f" % xs[len(xs)//2][0])
