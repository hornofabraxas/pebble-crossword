"""screen.py: pre-screen grid masks for fillability so generation picks from
known-good layouts instead of rediscovering them each run.

The lesson from earlier runs: the restricted wordlist (nzfeng core + gated
extras) has few common threes and fours, so most masks never fill and a long
per-pattern budget just burns wall-clock. This harness instead probes MANY
masks with a SHORT budget across a bounded pool of workers (so the machine is
not starved), then keeps the layouts that fill cleanly and fast. The kept
regular masks are appended to patterns15.json for reuse.

A mask is pre-filtered cheaply first (word count, three-letter-slot count) so we
do not spend a worker on a layout the restricted list cannot support.

Usage (run with tools/lab/.venv/bin/python):
  screen.py regular <n_fresh> <workers> <seeds> <budget> <tlimit>
  screen.py mini    <n_want>  <workers> <seeds> <budget> <tlimit> [size=7]
Writes screen_<kind>.json (every clean fill, ranked) and, for regular, appends
NEW masks that filled to patterns15.json.
"""
import json, os, sys, time, random, collections
from concurrent.futures import ProcessPoolExecutor, as_completed
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen   # noqa: E402  (loads fam.json, builds WORDS/INDEX at import)
import lex   # noqa: E402

# Short-slot ceilings for a mask. Fewer short slots ease the answer cap (common
# short words are the scarce pool) and read as better grids. Default keeps prior
# behaviour; set lower (LAB_MASK_THREES=6, LAB_MASK_FOURS=24) to push masks
# toward longer entries when the cap is getting tight.
MASK_THREES = int(os.environ.get("LAB_MASK_THREES") or 8)
MASK_FOURS = int(os.environ.get("LAB_MASK_FOURS") or 99)


def mask_str(g):
    return tuple(''.join('#' if g[r][c] == '#' else '.' for c in range(len(g[0]))) for r in range(len(g)))


def slot_stats(g, n):
    slots = gen.slots_of(g, n)
    lens = [len(s) for s in slots]
    return len(slots), sum(1 for L in lens if L == 3), sum(1 for L in lens if L >= 8)


# ---- candidate masks --------------------------------------------------------
def existing_masks():
    pats = json.load(open(os.path.join(HERE, "patterns15.json")))
    return [tuple(r for r in mask) for mask in pats]


def _valid_mask(g, n, wlo, whi, maxlen, threes_cap):
    if not gen.runs_ok(g, n) or not gen.connected(g, n):
        return None
    slots = gen.slots_of(g, n)
    ns = len(slots)
    if not (wlo <= ns <= whi):
        return None
    lens = [len(s) for s in slots]
    if max(lens) > maxlen:
        return None
    if sum(1 for L in lens if L == 3) > threes_cap:
        return None
    if sum(1 for L in lens if L == 4) > MASK_FOURS:
        return None
    if sum(1 for L in lens if L >= 8) < 4:
        return None
    return ns


def fresh_masks_15(rng, n_want, seen):
    """New symmetric 15x15 masks derived by mutating the proven low-three real
    layouts, not random black placement: random symmetric grids run 16-20
    three-letter slots (the sparse short-word list cannot fill those), while the
    real NYT skeletons sit at 4-8. We copy a low-three parent and toggle a few
    symmetric black-square pairs, keeping only variants that stay valid American
    grids with the word count and three-letter budget the restricted list can
    support. Fast (no reject-sampling) and fill-friendly by construction."""
    pats = json.load(open(os.path.join(HERE, "patterns15.json")))
    parents = []
    for m in pats:
        g = [['#' if ch == '#' else '.' for ch in r] for r in m]
        ns, threes, longs = slot_stats(g, 15)
        if threes <= MASK_THREES and longs >= 6:
            parents.append(m)
    out = []
    tries = 0
    while len(out) < n_want and tries < n_want * 600:
        tries += 1
        pm = rng.choice(parents)
        g = [['#' if ch == '#' else '.' for ch in r] for r in pm]
        for _ in range(rng.randint(1, 3)):
            r, c = rng.randint(0, 14), rng.randint(0, 14)
            mr, mc = 14 - r, 14 - c
            nv = '.' if g[r][c] == '#' else '#'
            g[r][c] = nv; g[mr][mc] = nv
        if _valid_mask(g, 15, 70, 78, 10, MASK_THREES) is None:
            continue
        ms = mask_str(g)
        if ms in seen:
            continue
        seen.add(ms); out.append(ms)
    return out


# ---- worker -----------------------------------------------------------------
def _fill_one(args):
    kind, mask, seed, budget, tlimit = args
    n = len(mask)
    g = [['#' if ch == '#' else '.' for ch in row] for row in mask]
    gen.USED_BEFORE = set()          # each probe is independent
    t0 = time.time()
    ok, slots, asg, let, nodes, dt = gen.fill(g, n, random.Random(seed), budget=budget, tlimit=tlimit)
    if not ok:
        return dict(kind=kind, mask=mask, ok=False, dt=time.time() - t0, seed=seed)
    # compact quality stats (mirror gen.report without the printing)
    grid = [''.join(let.get((r, c), '#') if g[r][c] != '#' else '#' for c in range(n)) for r in range(n)]
    words = []
    for d, num, ans in lex.probe_entries(grid):
        words.append((num, d, ans))
    ansset = [w[2] for w in words]
    fams = [gen.db[a][0] for a in ansset]
    threes = sum(1 for a in ansset if len(a) == 3)
    cwese = sum(1 for a in ansset if gen.cw_index(gen.db[a]) >= 3)
    extras = sum(1 for a in ansset if not gen.db[a][4])
    # unfair crossings: a cell where both crossing answers are "hard" (from slots)
    slotlist = gen.slots_of(g, n)
    filled = ['' for _ in slotlist]
    for si, s in enumerate(slotlist):
        filled[si] = ''.join(let.get(cell, '?') for cell in s)
    cellmap = collections.defaultdict(list)
    for si, s in enumerate(slotlist):
        for cell in s:
            cellmap[cell].append(filled[si])
    unfair = sum(1 for cell, ws in cellmap.items() if len(ws) == 2 and not gen.easy(ws[0]) and not gen.easy(ws[1]))
    return dict(kind=kind, mask=mask, ok=True, seed=seed, dt=time.time() - t0, nodes=nodes,
                grid=grid, words=words, nwords=len(words),
                meanfam=sum(fams) / len(fams), minfam=min(fams),
                threes=threes, cwese=cwese, extras=extras, unfair=unfair)


def score_fill(f):
    # higher is better: familiar, few threes/crosswordese/extras, no unfair crossings, fast
    return (f["meanfam"] - 0.15 * f["threes"] - 0.4 * f["cwese"] - 0.1 * f["extras"]
            - 2.0 * f["unfair"] - 0.002 * f["dt"])


def main():
    kind = sys.argv[1]
    n_want = int(sys.argv[2])
    workers = int(sys.argv[3]) if len(sys.argv) > 3 else 6
    seeds = int(sys.argv[4]) if len(sys.argv) > 4 else 2
    budget = int(sys.argv[5]) if len(sys.argv) > 5 else 45000
    tlimit = float(sys.argv[6]) if len(sys.argv) > 6 else 12.0
    rng = random.Random(int(os.environ.get("SCREEN_SEED", "20260907")))
    suffix = os.environ.get("SCREEN_SUFFIX", "")

    if kind == "regular":
        seen = set()
        exist = existing_masks()
        for m in exist:
            seen.add(m)
        fresh = fresh_masks_15(rng, n_want, seen)
        use_exist = [] if os.environ.get("SCREEN_NO_EXISTING") else exist
        print("candidate masks: %d existing + %d fresh (prefiltered)" % (len(use_exist), len(fresh)))
        cands = [("existing", m) for m in use_exist] + [("fresh", m) for m in fresh]
        tasks = []
        for origin, m in cands:
            for s in range(seeds):
                tasks.append((origin, m, hash((m, s)) & 0x7fffffff, budget, tlimit))
        n = 15
    else:
        size = int(sys.argv[7]) if len(sys.argv) > 7 else 7
        cfg = gen.MINI[size] if hasattr(gen, "MINI") else None
        # build mini masks on the fly like gen.py does
        MINI = {5: dict(target=(0, 4), words=(10, 10), maxlen=5, long_min=0),
                7: dict(target=(4, 11), words=(12, 18), maxlen=7, long_min=6)}
        cfg = MINI[size]
        seen = set(); masks = []
        tries = 0
        while len(masks) < max(n_want * 3, 18) and tries < 4000:
            tries += 1
            gm = gen.make_grid15(rng, n=size, target=cfg["target"], words=cfg["words"], maxlen=cfg["maxlen"])
            if not gm:
                continue
            ms = mask_str(gm)
            if ms in seen:
                continue
            if sum(1 for sl in gen.slots_of(gm, size) if len(sl) >= 5) < cfg["long_min"]:
                continue
            seen.add(ms); masks.append(ms)
        print("mini candidate masks:", len(masks))
        tasks = []
        for m in masks:
            for s in range(seeds):
                tasks.append(("mini", m, hash((m, s)) & 0x7fffffff, budget, tlimit))
        n = size

    print("probing %d fill tasks on %d workers (budget %d nodes, %.0fs each)" % (len(tasks), workers, budget, tlimit))
    t0 = time.time()
    results = []
    done = 0
    with ProcessPoolExecutor(max_workers=workers) as ex:
        futs = [ex.submit(_fill_one, t) for t in tasks]
        for fu in as_completed(futs):
            r = fu.result(); done += 1
            if r["ok"]:
                results.append(r)
            if done % 20 == 0:
                print("  %d/%d done, %d fills, %.0fs" % (done, len(tasks), len(results), time.time() - t0))
    print("probed in %.0fs; %d clean fills" % (time.time() - t0, len(results)))

    # dedup by answer-set (keep the best-scoring fill per distinct mask+answers),
    # and rank
    best = {}
    for r in results:
        key = (r["mask"], tuple(sorted(w[2] for w in r["words"])))
        if key not in best or score_fill(r) > score_fill(best[key]):
            best[key] = r
    ranked = sorted(best.values(), key=score_fill, reverse=True)
    print("\nRANKED (top 40):")
    for r in ranked[:40]:
        origin = r.get("kind")
        print("  score %.2f  fam %.2f min %.2f  words %d  3L %d cwese %d extra %d unfair %d  %.1fs" % (
            score_fill(r), r["meanfam"], r["minfam"], r["nwords"], r["threes"], r["cwese"], r["extras"], r["unfair"], r["dt"]))

    out = [dict(grid=r["grid"], words=[[w[0], w[1], w[2]] for w in r["words"]],
                meanfam=r["meanfam"], threes=r["threes"], cwese=r["cwese"],
                extras=r["extras"], unfair=r["unfair"], dt=r["dt"], score=score_fill(r),
                mask=list(r["mask"]))
           for r in ranked]
    json.dump(out, open(os.path.join(HERE, "screen_%s%s.json" % (kind, suffix)), "w"), indent=1)
    print("\nwrote screen_%s%s.json (%d fills)" % (kind, suffix, len(out)))

    if kind == "regular" and os.environ.get("SCREEN_ADOPT"):
        # append NEW masks that produced a clean fill to patterns15.json
        exist_set = set(existing_masks())
        new_masks = []
        seen_new = set()
        for r in ranked:
            m = r["mask"]
            if m in exist_set or m in seen_new:
                continue
            seen_new.add(m); new_masks.append([row for row in m])
        if new_masks:
            pats = json.load(open(os.path.join(HERE, "patterns15.json")))
            pats += new_masks
            json.dump(pats, open(os.path.join(HERE, "patterns15.json"), "w"), indent=0)
            print("appended %d new masks to patterns15.json (now %d)" % (len(new_masks), len(pats)))


if __name__ == "__main__":
    main()
