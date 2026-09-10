"""accumulate.py: build a cap-clean 200-puzzle set across iterative generation
rounds. Persists the accepted set and emits the seed ledger for the next round,
so each CI batch is generated avoiding the answers already accepted (not just
the ones the original bundle over-used). Prefers keeping shipped puzzles (they
already have clues) before pulling in freshly generated candidates.

State: accepted.json = [{origin, cls, answers, grid, file, difficulty}].
Each run adds cap-safe candidates from --pool up to the per-class targets, then
rewrites accepted.json and writes ledger_seed.json (answer -> count of the
accepted set) for the next round's LAB_LEDGER seed.

Usage (tools/lab/.venv/bin/python or python3):
  accumulate.py --pool <dir> [--cap 5] [--mini 100] [--regular 100]
                [--overlap 0.45] [--accepted accepted.json]
                [--ledger-out ledger_seed.json] [--reset]
"""
import importlib.util, json, os, sys, glob, collections
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lex


def grid_answers(grid):
    return [a for _, _, a in lex.probe_entries([r.replace(' ', '') for r in grid])]


def cls_of(grid):
    return "mini" if len(grid) <= 7 else "regular"


def load_shipped():
    spec = importlib.util.spec_from_file_location("lab_spec", os.path.join(HERE, "lab_spec.py"))
    m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
    out = []
    for p in m.PUZZLES:
        out.append(dict(origin="shipped", file=p["file"], difficulty=p.get("difficulty"),
                        cls=cls_of(p["grid"]), grid=p["grid"], answers=grid_answers(p["grid"])))
    return out


def load_pool(pool_dir):
    out = []
    for f in sorted(glob.glob(os.path.join(pool_dir, "screen_*.json"))):
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
                            difficulty=None, cls=cls_of(r["grid"]), grid=r["grid"],
                            answers=grid_answers(r["grid"])))
    return out


def jaccard(a, b):
    A, B = set(a), set(b)
    return len(A & B) / len(A | B) if (A or B) else 0.0


# Shelves and their caps. Minis are capped bundle-wide (one shelf of 100);
# regulars are capped PER DIFFICULTY (challenging and peaceful counted
# separately), so a common word may appear up to 5 times in each 50-puzzle
# shelf - what a solver working through one difficulty actually experiences.
SHELVES = {"mini": 100, "reg_challenging": 50, "reg_peaceful": 50}


def shelf_of_shipped(e):
    if e["cls"] == "mini":
        return "mini"
    return "reg_peaceful" if e.get("difficulty") == "peaceful" else "reg_challenging"


def main():
    args = sys.argv[1:]
    pool_dir = "."; cap = 5; reg_cap = None; overlap = 0.45
    accepted_path = os.path.join(HERE, "accepted.json")
    ledger_out = os.path.join(HERE, "ledger_seed.json"); reset = False
    i = 0
    while i < len(args):
        a = args[i]
        if a == "--pool": pool_dir = args[i+1]; i += 2
        elif a == "--cap": cap = int(args[i+1]); i += 2
        elif a == "--reg-cap": reg_cap = int(args[i+1]); i += 2
        elif a == "--overlap": overlap = float(args[i+1]); i += 2
        elif a == "--accepted": accepted_path = args[i+1]; i += 2
        elif a == "--ledger-out": ledger_out = args[i+1]; i += 2
        elif a == "--reset": reset = True; i += 1
        else: i += 1

    if reg_cap is None: reg_cap = cap
    def cap_of(shelf): return cap if shelf == "mini" else reg_cap

    accepted = []
    if os.path.exists(accepted_path) and not reset:
        accepted = json.load(open(accepted_path))

    counts = {s: collections.Counter() for s in SHELVES}   # per-shelf answer counts
    by_shelf = collections.Counter()
    picked_sets = []
    for e in accepted:
        s = e["shelf"]; counts[s].update(e["answers"]); by_shelf[s] += 1
        picked_sets.append(set(e["answers"]))

    def fits(shelf, ans):
        c = counts[shelf]; add = collections.Counter(ans); cp = cap_of(shelf)
        return all(c[a] + add[a] <= cp for a in add)

    def place_regular(ans):
        # the regular shelf with room that stays under cap, preferring the emptier
        opts = [s for s in ("reg_challenging", "reg_peaceful")
                if by_shelf[s] < SHELVES[s] and fits(s, ans)]
        if not opts:
            return None
        return min(opts, key=lambda s: by_shelf[s])

    def add_entry(c, shelf):
        c = dict(c); c["shelf"] = shelf
        accepted.append(c); picked_sets.append(set(c["answers"]))
        counts[shelf].update(c["answers"]); by_shelf[shelf] += 1

    def try_add(entries, label):
        added = 0
        for c in sorted(entries, key=lambda c: sum(min(counts[s][a] for s in SHELVES) for a in c["answers"])):
            if any(jaccard(c["answers"], s) >= overlap for s in picked_sets):
                continue
            if c["cls"] == "mini":
                if by_shelf["mini"] >= SHELVES["mini"] or not fits("mini", c["answers"]):
                    continue
                add_entry(c, "mini"); added += 1
            else:
                # a kept shipped regular stays in its own difficulty; a new one is placed
                if c["origin"] == "shipped":
                    shelf = shelf_of_shipped(c)
                    if by_shelf[shelf] >= SHELVES[shelf] or not fits(shelf, c["answers"]):
                        continue
                else:
                    shelf = place_regular(c["answers"])
                    if shelf is None:
                        continue
                add_entry(c, shelf); added += 1
        print("  +%d from %s" % (added, label))
        return added

    have_files = {e["file"] for e in accepted}
    if not any(e["origin"] == "shipped" for e in accepted):
        try_add([s for s in load_shipped() if s["file"] not in have_files], "shipped (kept)")
    pool = [c for c in load_pool(pool_dir) if c["file"] not in have_files]
    try_add(pool, "candidates")

    json.dump(accepted, open(accepted_path, "w"))
    # Seed the next round for REGULAR generation: a word is worth avoiding only
    # when it is near the cap in BOTH regular shelves (else it can still be
    # placed in the emptier one), so seed with the per-word minimum.
    seed = {}
    allw = set(counts["reg_challenging"]) | set(counts["reg_peaceful"])
    for w in allw:
        seed[w] = min(counts["reg_challenging"][w], counts["reg_peaceful"][w])
    json.dump(seed, open(ledger_out, "w"))

    kept = sum(1 for e in accepted if e["origin"] == "shipped")
    print("\nACCEPTED  mini %d/100  reg_challenging %d/50  reg_peaceful %d/50  (shipped-kept %d, new %d)" % (
        by_shelf["mini"], by_shelf["reg_challenging"], by_shelf["reg_peaceful"], kept, len(accepted) - kept))
    for s in SHELVES:
        cp = cap_of(s)
        over = [a for a, n in counts[s].items() if n > cp]
        at = sum(1 for n in counts[s].values() if n == cp)
        print("  %-16s over-cap %s ; at cap %d ; distinct %d" % (s, over or "none", at, len(counts[s])))
    print("wrote %s and %s" % (accepted_path, ledger_out))


if __name__ == "__main__":
    main()
