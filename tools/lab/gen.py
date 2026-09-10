"""Generate minis (5x5 or 7x7, symmetric patterns made on the fly) and 15x15
themeless grids (bundled daily patterns) from the nzfeng core list plus
corpus-familiar extras, ranked by corpus familiarity with a crosswordese
penalty and (v0.18) gated by wordfreq Zipf frequency in ordinary English,
MRV filler with forward checking, fair-crossing check.
Usage: gen.py <seed> <minis> <regulars> [mini_size=7]. Output: grids + answers + stats."""
import json, math, random, sys, time, os, collections
HERE = os.path.dirname(os.path.abspath(__file__))
_fam = json.load(open(os.path.join(HERE, "fam.json")))
FIELDS = ("fam", "nclues", "recent", "cluetok", "core", "zipf")
if not isinstance(_fam, dict) or _fam.get("fields") != list(FIELDS):
    sys.exit("fam.json is stale or from an older score.py: run tools/lab/.venv/bin/python tools/lab/score.py")
db = _fam["rows"]   # a -> [fam, nclues, recent, cluetok, core, zipf]

# --- scoring -----------------------------------------------------------------
def cw_index(v):  # crosswordese: common in grids, rare in ordinary text (clue text as proxy)
    fam, _, _, tok = v[:4]
    return fam - math.log1p(tok)
bylen = collections.defaultdict(list)
for a, v in db.items(): bylen[len(a)].append(a)
pct = {}
for L, ws in bylen.items():
    fams = sorted(db[a][0] for a in ws if db[a][4])
    pct[L] = (fams[int(len(fams) * 0.10)], fams[int(len(fams) * 0.60)])   # (eligible floor, "easy" line), from the curated list
def rank(a):
    v = db[a]
    return v[0] - 0.4 * max(0.0, cw_index(v) - 2.5) - (0.0 if v[4] else 1.0)   # curated words first
BAN = set("""AERIE ERATO DELE ESSEN ARNO ITAL ALAI RTE RCA IDA SERA ETTA ERNE OLEO ESNE ANOA ALEE ETUI ERST ELAL ADIT AGUA NON SAN PER
ARTE ESTE ESTA ESS ENE ERN APER ELI OLDS RYES ELBA AIDA ESTES PAREE IBET EDU EASYAS OHARE ORSO ISEE AONE INRE ASTO ATSEA ASIS INON ISNT ETTU ONA
ATIT ONEA TBAR ONAIR ETAL ACTI OER ONIT ATAD ATEE TYSON ALBEE RICO SALAAM LOINCLOTH DILETTANTE ATHLEISURE TEETOTALER OCELOT TARE ASCOT AHEM AILED MOURNED
TORMENTORS ARGOT AARE MORES HASP RICER OLLIE PAS NOM DOLEFUL OPINE EMOTE ULNA LEST STERNUM PELE SPLATTER""".split())
BANNED_EXTRA = set()
def knob(name, default): return type(default)(os.environ.get(name) or default)   # an exported-but-empty variable means the default
ZIPF_CORE = knob("LAB_ZIPF_CORE", 3.2); ZIPF_EXTRA = knob("LAB_ZIPF_EXTRA", 3.6)   # ordinary-English floor (wordfreq Zipf): SERA 3.1, INST 2.8, STA 3.3 fall; ANKLE 4.0, NASA 4.2, ERIE 3.6 stay
def eligible(a):
    v = db[a]
    if a in BAN: return False
    if v[5] < (ZIPF_CORE if v[4] else ZIPF_EXTRA): return False
    if v[4]: return v[0] >= pct[len(a)][0] and v[2] >= 1 and cw_index(v) < 3.5 and (v[3] >= 8 or v[0] >= pct[len(a)][1] + 1.0)   # curated, and known outside puzzles
    return v[2] >= 100 and v[0] >= pct[len(a)][1] and v[3] >= 150 and cw_index(v) < 2.5   # extra (names): strongly present in ordinary text
def easy(a): return db[a][0] >= pct[len(a)][1] or db[a][3] >= 15   # common in puzzles, or plainly common in text

WORDS = {L: sorted([a for a in ws if eligible(a)], key=rank, reverse=True) for L, ws in bylen.items()}
INDEX = {}   # L -> {(pos, ch): bitmask over WORDS[L]}
for L, ws in WORDS.items():
    idx = collections.defaultdict(int)
    for i, w in enumerate(ws):
        for p, ch in enumerate(w): idx[(p, ch)] |= 1 << i
    INDEX[L] = idx
ALL = {L: (1 << len(ws)) - 1 for L, ws in WORDS.items()}

# --- grids -------------------------------------------------------------------
def runs_ok(g, n):
    for r in range(n):
        for line in (g[r], [g[i][r] for i in range(n)]):
            k = 0
            for c in range(n + 1):
                if c < n and line[c] != '#': k += 1
                else:
                    if 0 < k < 3: return False
                    k = 0
    return True
def connected(g, n):
    cells = [(r, c) for r in range(n) for c in range(n) if g[r][c] != '#']
    seen = {cells[0]}; st = [cells[0]]
    while st:
        r, c = st.pop()
        for dr, dc in ((1,0),(-1,0),(0,1),(0,-1)):
            q = (r+dr, c+dc)
            if 0 <= q[0] < n and 0 <= q[1] < n and g[q[0]][q[1]] != '#' and q not in seen:
                seen.add(q); st.append(q)
    return len(seen) == len(cells)
def slots_of(g, n):
    out = []
    for r in range(n):
        c = 0
        while c < n:
            if g[r][c] == '#': c += 1; continue
            s = c
            while c < n and g[r][c] != '#': c += 1
            if c - s >= 3: out.append([(r, x) for x in range(s, c)])
    for c in range(n):
        r = 0
        while r < n:
            if g[r][c] == '#': r += 1; continue
            s = r
            while r < n and g[r][c] != '#': r += 1
            if r - s >= 3: out.append([(x, c) for x in range(s, r)])
    return out
def make_grid15(rng, n=15, target=(40, 46), words=(72, 78), maxlen=9):
    for _ in range(4000):
        g = [['.'] * n for _ in range(n)]
        cells = [(r, c) for r in range(n) for c in range(n)]
        rng.shuffle(cells)
        black = 0; want = rng.randint(*target)
        for (r, c) in cells:
            if black >= want: break
            m = (n-1-r, n-1-c)
            if g[r][c] == '#': continue
            g[r][c] = '#'; g[m[0]][m[1]] = '#'
            if runs_ok(g, n): black += 1 if (r, c) == m else 2
            else: g[r][c] = '.'; g[m[0]][m[1]] = '.'
        if not connected(g, n): continue
        ns = len(slots_of(g, n))
        if words[0] <= ns <= words[1] and all(len(s) <= maxlen for s in slots_of(g, n)):
            return g
    return None

# --- filler ------------------------------------------------------------------
GLUE_CAP = 4
def stem_clash(a, b):
    # crude shared-stem test: one is the other plus a common suffix, or they share a 5+ letter prefix
    if a == b: return True
    for x, y in ((a, b), (b, a)):
        if x.startswith(y) and x[len(y):] in ("S", "ES", "ED", "D", "R", "ER", "ING", "LY"): return True
        if len(y) >= 4 and x.startswith(y[:-1]) and x[len(y)-1:] in ("ER", "ING", "ED", "ES"): return True
    return len(a) >= 5 and len(b) >= 5 and a[:5] == b[:5]
USED_BEFORE = set()
# Bundle-wide answer ledger: a soft cap on how many times one answer may appear
# across the WHOLE accepted set, not just within a single fill() or run. Seed it
# from a JSON file of {ANSWER: count} (env LAB_LEDGER) so sequential/parallel
# runs share one budget; LAB_WORD_CAP is the per-answer ceiling (0 = disabled,
# the historical behaviour). USED_BEFORE stays a hard within-run no-repeat set.
LEDGER = collections.Counter()
LEDGER_CAP = int(os.environ.get("LAB_WORD_CAP") or 0)
# Soft saturation steering (the lever that beats convergence): rather than a
# hard reject, demote a candidate whose bundle-wide use is at or near a soft cap
# so the filler prefers fresh answers but can still fall back to glue when a
# corner needs it. LAB_SOFT_CAP is the target ceiling (default = the hard cap if
# one is set); LAB_SAT_PENALTY scales the demotion (0 = off, historical). A word
# at/over the soft cap is pushed to the back of the candidate list (a large
# constant), so under-cap words always win when enough of them fit.
SOFT_CAP = int(os.environ.get("LAB_SOFT_CAP") or LEDGER_CAP or 0)
SAT_PENALTY = float(os.environ.get("LAB_SAT_PENALTY") or 0)
def sat_pen(w):
    if not SAT_PENALTY or not SOFT_CAP: return 0.0
    n = LEDGER[w]
    if n >= SOFT_CAP: return 1e6 + SAT_PENALTY * n        # at/over cap: avoid unless nothing else fits
    if n >= SOFT_CAP - 1: return SAT_PENALTY * n          # one below: gently avoid
    return 0.0
_LEDGER_PATH = os.environ.get("LAB_LEDGER")
if _LEDGER_PATH and os.path.exists(_LEDGER_PATH):
    LEDGER.update({k: int(v) for k, v in json.load(open(_LEDGER_PATH)).items()})
def ledger_full(w): return LEDGER_CAP and LEDGER[w] >= LEDGER_CAP
def ledger_add(words):
    LEDGER.update(words)
    if _LEDGER_PATH: json.dump(dict(LEDGER), open(_LEDGER_PATH, "w"))
def fill(g, n, rng, budget=400000, tlimit=90):
    slots = slots_of(g, n)
    cell_slots = collections.defaultdict(list)
    for si, s in enumerate(slots):
        for k, cell in enumerate(s): cell_slots[cell].append((si, k))
    L = [len(s) for s in slots]
    letters = {}
    used = set()
    assigned = [None] * len(slots)
    nodes = [0]; t0 = time.time(); deep = [0, None]
    def cands(si):
        m = ALL[L[si]]
        idx = INDEX[L[si]]
        for k, cell in enumerate(slots[si]):
            ch = letters.get(cell)
            if ch: m &= idx.get((k, ch), 0)
            if not m: return 0
        return m
    LET = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    def arc_ok(sj, mj):
        idx = INDEX[L[sj]]
        for k, cell in enumerate(slots[sj]):
            if cell in letters: continue
            other = [(so, ko) for (so, ko) in cell_slots[cell] if so != sj and not assigned[so]]
            if not other: continue
            so, ko = other[0]
            mo = cands(so)
            if mo == 0: return False
            idxo = INDEX[L[so]]
            if not any((mj & idx.get((k, ch), 0)) and (mo & idxo.get((ko, ch), 0)) for ch in LET): return False
        return True
    def bits(m, cap):
        out = []
        while m and len(out) < cap:
            low = m & -m; i = low.bit_length() - 1
            out.append(i); m ^= low
        return out
    def bt():
        nodes[0] += 1
        if nodes[0] > budget or time.time() - t0 > tlimit: return False
        best = None; bm = None; bc = 10**9
        for si in range(len(slots)):
            if assigned[si]: continue
            m = cands(si)
            c = m.bit_count()
            if c == 0: return False
            if c < bc: bc, best, bm = c, si, m
        if best is None: return True
        d = sum(1 for a in assigned if a)
        if d > deep[0]: deep[0] = d; deep[1] = sorted(L[si] for si in range(len(slots)) if not assigned[si])
        ws = WORDS[L[best]]
        scored = []
        for i in bits(bm, 120):
            w = ws[i]
            if w in used: continue
            put = []
            for k, cell in enumerate(slots[best]):
                if cell not in letters: letters[cell] = w[k]; put.append(cell)
            la = 0.0
            for cell in put:
                for (sj, _) in cell_slots[cell]:
                    if sj != best and not assigned[sj]:
                        mj = cands(sj)
                        c = mj.bit_count()
                        if c == 0: la = None; break
                        la += math.log(c)
                        if not arc_ok(sj, mj): la = None; break
                if la is None: break
            for cell in put: del letters[cell]
            if la is not None: scored.append((la - 0.35 * i / 10.0 - sat_pen(w) + rng.random() * (0.8 if n > 7 else 6.0), i))   # look-ahead, then saturation, then rank; minis vary more
        scored.sort(reverse=True)
        for _, i in scored[:30]:
            w = ws[i]
            if w in USED_BEFORE or ledger_full(w) or any(stem_clash(w, u) for u in used): continue
            if cw_index(db[w]) >= 2.5 and sum(1 for u in used if cw_index(db[u]) >= 2.5) >= GLUE_CAP: continue
            put = []
            for k, cell in enumerate(slots[best]):
                if cell not in letters: letters[cell] = w[k]; put.append(cell)
            assigned[best] = w; used.add(w)
            if bt(): return True
            assigned[best] = None; used.discard(w)
            for cell in put: del letters[cell]
        return False
    ok = bt()
    print('   deepest', deep[0], 'of', len(slots), 'nodes', nodes[0])
    return ok, slots, assigned, letters, nodes[0], time.time() - t0

def report(name, g, n, slots, assigned, letters):
    print("\n==", name)
    for r in range(n):
        print(' '.join(letters.get((r, c), '#') if g[r][c] != '#' else '#' for c in range(n)))
    # numbering
    num = {}; k = 0
    for r in range(n):
        for c in range(n):
            if g[r][c] == '#': continue
            sa = c == 0 or g[r][c-1] == '#'; sd = r == 0 or g[r-1][c] == '#'
            if (sa and c + 2 < n and g[r][c+1] != '#' and g[r][c+2] != '#') or (sd and r + 2 < n and g[r+1][c] != '#' and g[r+2][c] != '#'):
                k += 1; num[(r, c)] = k
    rows = []
    for si, s in enumerate(slots):
        d = 'A' if s[0][0] == s[1][0] else 'D'
        a = assigned[si]; v = db[a]
        rows.append((num[s[0]], d, a, v[0], cw_index(v), easy(a), v[4]))
    rows.sort(key=lambda x: (x[1], x[0]))
    for nmb, d, a, fam, cw, ez, core in rows:
        print("%3d%s %-15s fam %.2f  cw %.1f %s%s" % (nmb, d, a, fam, cw, '' if ez else '(hard) ', '' if core else '[extra]'))
    fams = [x[3] for x in rows]
    hard = [x for x in rows if not x[5]]
    # fair crossings: a cell where both words are hard
    cellw = collections.defaultdict(list)
    for si, s in enumerate(slots):
        for cell in s: cellw[cell].append(assigned[si])
    naticks = [(cell, ws) for cell, ws in cellw.items() if len(ws) == 2 and not easy(ws[0]) and not easy(ws[1])]
    print("words %d, mean fam %.2f, min fam %.2f, hard %d, extra(non-curated) %d, 3-letter %d, crosswordese(cw>=3) %d, unfair crossings %d" % (
        len(rows), sum(fams)/len(fams), min(fams), len(hard), sum(1 for x in rows if not x[6]), sum(1 for x in rows if len(x[2]) == 3),
        sum(1 for x in rows if x[4] >= 3), len(naticks)))
    for cell, ws in naticks: print("   unfair:", ws)
    return rows

if __name__ == "__main__":
    seed = int(sys.argv[1]) if len(sys.argv) > 1 else 7
    want = int(sys.argv[2]) if len(sys.argv) > 2 else 3
    want_reg = int(sys.argv[3]) if len(sys.argv) > 3 else want
    msize = int(sys.argv[4]) if len(sys.argv) > 4 else 7
    # Mini patterns are symmetric grids made on the fly: blacks and word counts by size,
    # every run 3+, and (7x7) at least six entries of five letters or more so it is not a
    # field of threes. The 5x5 row reproduces the four classic mini shapes.
    MINI = {5: dict(target=(0, 4), words=(10, 10), maxlen=5, long_min=0, tlimit=20),
            7: dict(target=(4, 11), words=(12, 18), maxlen=7, long_min=6, tlimit=40)}
    if msize not in MINI: sys.exit("mini_size must be one of %s" % sorted(MINI))
    cfg = MINI[msize]
    rng = random.Random(seed)
    print("eligible per length:", {L: len(w) for L, w in sorted(WORDS.items())})
    out = {"mini": [], "regular": []}
    patsm = []
    for _ in range(200):
        if len(patsm) >= 12: break
        gm = make_grid15(rng, n=msize, target=cfg["target"], words=cfg["words"], maxlen=cfg["maxlen"])
        if gm and gm not in patsm and sum(1 for sl in slots_of(gm, msize) if len(sl) >= 5) >= cfg["long_min"]: patsm.append(gm)
    if not patsm: sys.exit("no %dx%d pattern satisfies %r" % (msize, msize, cfg))
    k = 0
    while len(out["mini"]) < want and k < 60:
        gm = patsm[k % len(patsm)]
        ok, slots, asg, let, nodes, dt = fill(gm, msize, random.Random(seed * 100 + k), budget=200000, tlimit=cfg["tlimit"]); k += 1
        if ok:
            rows = report("MINI %d" % len(out["mini"]), gm, msize, slots, asg, let)
            USED_BEFORE.update(x[2] for x in rows); ledger_add(x[2] for x in rows)
            out["mini"].append({"grid": [''.join(let.get((r, c), '#') if gm[r][c] != '#' else '#' for c in range(msize)) for r in range(msize)], "words": [(x[0], x[1], x[2]) for x in rows]})
    # Symmetric 15x15 block layouts (masks only; tools/lab/patterns15.json). Set
    # LAB_PATTERN_INDEX to fill one specific layout (so a sweep of jobs each fills
    # a DISTINCT grid instead of all converging on the easiest layout).
    _pats = json.load(open(os.path.join(HERE, "patterns15.json")))
    allpats = [[['#' if ch == '#' else '.' for ch in r] for r in mask] for mask in _pats]
    only = os.environ.get("LAB_PATTERN_INDEX")
    pats = [allpats[int(only) % len(allpats)]] if only is not None else allpats
    k = 0
    # Each pattern gets one attempt of up to BUDGET nodes and PTIME seconds; a run that
    # exhausts its nodes early is reseeded until the pattern's PTIME is spent.
    BUDGET = knob("LAB_BUDGET", 60000); PTIME = knob("LAB_PATTERN_TIME", 60.0)
    while len(out["regular"]) < want_reg and k < 40:
        g = pats[(seed + k) % len(pats)]
        ok = False; t_start = time.time()
        for run in range(30):
            ok, slots, asg, let, nodes, dt = fill(g, 15, random.Random(seed * 1000 + k * 50 + run), budget=BUDGET, tlimit=PTIME)
            if ok or time.time() - t_start > PTIME: break
        k += 1
        if not ok: print("pattern", k, "failed"); continue
        rows = report("REGULAR %d" % len(out["regular"]), g, 15, slots, asg, let)
        USED_BEFORE.update(x[2] for x in rows); ledger_add(x[2] for x in rows)
        out["regular"].append({"grid": [''.join(let.get((r, c), '#') if g[r][c] != '#' else '#' for c in range(15)) for r in range(15)], "words": [(x[0], x[1], x[2]) for x in rows]})
    json.dump(out, open(os.path.join(HERE, "fills_%d.json" % seed), "w"), indent=1)
