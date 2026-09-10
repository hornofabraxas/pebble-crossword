"""select_cap.py: assemble a bundle whose every answer appears at most CAP
times across the WHOLE set, by GLOBAL selection from a candidate pool rather
than per-puzzle greedy generation.

Why this exists: the filler minimises local cost, so independent fills converge
on the same common glue (ERIE, AREA, SEE). A bundle-wide cap can only be met by
over-generating candidates and then CHOOSING a subset that spreads usage. This
tool is both the chooser and the feasibility dry-run: point it at the shipped
puzzles plus every leftover screen_*/fills_* candidate on disk and it reports
how large a cap-clean set it can build and which answers are the bottleneck.

Candidates:
  - shipped: tools/lab/lab_spec.py (has file, difficulty, grid)
  - leftovers: screen_*.json / fills_*.json (grid + words; no difficulty)
Each is classed mini (<=7 rows) or regular by grid size.

Selection: greedy. Repeatedly add the candidate that fits under the cap and is
"freshest" (its answers are the least-used so far), skipping any candidate whose
answer-set overlaps an already-picked grid too much (near-duplicate fills).

Usage (tools/lab/.venv/bin/python):
  select_cap.py [--cap N] [--overlap J] [--keep-shipped] [--only mini|regular]
"""
import json, glob, os, sys, collections
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))
import lex


def grid_answers(grid):
    g = [r.replace(' ', '') for r in grid]
    return [ans for _, _, ans in lex.probe_entries(g)]


def cls_of(grid):
    return "mini" if len(grid) <= 7 else "regular"


def load_shipped():
    import importlib.util
    spec = importlib.util.spec_from_file_location("lab_spec", os.path.join(HERE, "lab_spec.py"))
    m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
    out = []
    for p in m.PUZZLES:
        out.append(dict(origin="shipped", file=p["file"], difficulty=p.get("difficulty"),
                        cls=cls_of(p["grid"]), answers=grid_answers(p["grid"])))
    return out


def load_leftovers():
    out = []
    for f in sorted(glob.glob(os.path.join(HERE, "screen_*.json"))):
        try:
            d = json.load(open(f))
        except Exception:
            continue
        if not isinstance(d, list):
            continue
        for i, r in enumerate(d):
            if not isinstance(r, dict) or "grid" not in r:
                continue
            out.append(dict(origin=os.path.basename(f), file="%s#%d" % (os.path.basename(f), i),
                            difficulty=None, cls=cls_of(r["grid"]), answers=grid_answers(r["grid"])))
    for f in sorted(glob.glob(os.path.join(HERE, "fills_*.json"))):
        try:
            d = json.load(open(f))
        except Exception:
            continue
        if not isinstance(d, dict):
            continue
        for kind in ("mini", "regular"):
            for i, r in enumerate(d.get(kind, [])):
                grid = r.get("grid")
                if not grid:
                    continue
                out.append(dict(origin=os.path.basename(f), file="%s:%s#%d" % (os.path.basename(f), kind, i),
                                difficulty=None, cls=cls_of(grid), answers=grid_answers(grid)))
    return out


def jaccard(a, b):
    A, B = set(a), set(b)
    return len(A & B) / len(A | B) if (A or B) else 0.0


def select(cands, cap, overlap, per_cls_limit=None):
    """Greedy: add the freshest candidate that keeps every answer <= cap and is
    not a near-duplicate of an already-picked grid."""
    count = collections.Counter()
    picked = []
    picked_sets = []
    picked_by_cls = collections.Counter()
    pool = list(cands)

    def fits(c):
        # every answer would stay <= cap
        add = collections.Counter(c["answers"])
        return all(count[a] + add[a] <= cap for a in add)

    def freshness(c):
        # lower = fresher (its answers are less used already). Tie-break: fewer
        # answers already at cap-1 (leaves more headroom).
        return sum(count[a] for a in c["answers"])

    changed = True
    while True:
        best = None; bestkey = None
        for c in pool:
            if per_cls_limit and picked_by_cls[c["cls"]] >= per_cls_limit.get(c["cls"], 10**9):
                continue
            if not fits(c):
                continue
            if any(jaccard(c["answers"], s) >= overlap for s in picked_sets):
                continue
            key = (freshness(c), len(c["answers"]))
            if best is None or key < bestkey:
                best, bestkey = c, key
        if best is None:
            break
        picked.append(best)
        picked_sets.append(set(best["answers"]))
        picked_by_cls[best["cls"]] += 1
        count.update(best["answers"])
        pool.remove(best)
    return picked, count


def main():
    args = sys.argv[1:]
    cap = 5; overlap = 0.45; keep_shipped = False; only = None
    i = 0
    while i < len(args):
        a = args[i]
        if a == "--cap": cap = int(args[i+1]); i += 2
        elif a == "--overlap": overlap = float(args[i+1]); i += 2
        elif a == "--keep-shipped": keep_shipped = True; i += 1
        elif a == "--only": only = args[i+1]; i += 2
        else: i += 1

    shipped = load_shipped()
    leftovers = load_leftovers()
    cands = shipped + leftovers
    if only:
        cands = [c for c in cands if c["cls"] == only]
    print("candidate pool: %d shipped + %d leftover = %d  (mini %d, regular %d)" % (
        len(shipped), len(leftovers), len(cands),
        sum(1 for c in cands if c["cls"] == "mini"), sum(1 for c in cands if c["cls"] == "regular")))

    if keep_shipped:
        # Seed the count with shipped, then only add leftovers that keep the cap.
        base = [c for c in cands if c["origin"] == "shipped"]
        extra = [c for c in cands if c["origin"] != "shipped"]
        count = collections.Counter()
        picked = list(base)
        picked_sets = [set(c["answers"]) for c in base]
        for c in base: count.update(c["answers"])
        over0 = {a: n for a, n in count.items() if n > cap}
        print("\n[keep-shipped] shipped alone has %d answers over cap %d (excess %d)" % (
            len(over0), cap, sum(n - cap for n in over0.values())))
        added = 0
        for c in sorted(extra, key=lambda c: sum(count[a] for a in c["answers"])):
            add = collections.Counter(c["answers"])
            if any(count[a] + add[a] > cap for a in add):
                continue
            if any(jaccard(c["answers"], s) >= overlap for s in picked_sets):
                continue
            picked.append(c); picked_sets.append(set(c["answers"])); count.update(c["answers"]); added += 1
        print("[keep-shipped] leftovers addable without breaking cap: %d" % added)
        report(picked, count, cap)
        return

    picked, count = select(cands, cap, overlap)
    report(picked, count, cap)


def report(picked, count, cap):
    nm = sum(1 for c in picked if c["cls"] == "mini")
    nr = sum(1 for c in picked if c["cls"] == "regular")
    print("\n=== SELECTED %d puzzles under cap %d: mini %d, regular %d ===" % (len(picked), cap, nm, nr))
    at_cap = sorted([a for a, n in count.items() if n >= cap], key=lambda a: -count[a])
    print("answers at the cap (%d of %d distinct): %s%s" % (
        len(at_cap), len(count), ", ".join("%s:%d" % (a, count[a]) for a in at_cap[:30]),
        " ..." if len(at_cap) > 30 else ""))
    # how many distinct answers were used, and the 3-letter pressure
    threes = [a for a in count if len(a) == 3]
    threes_at = [a for a in threes if count[a] >= cap]
    print("distinct answers used: %d  (3-letter distinct used: %d, of which at cap: %d)" % (
        len(count), len(threes), len(threes_at)))


if __name__ == "__main__":
    main()
