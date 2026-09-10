"""Write the bundled puzzles (grid + clues) as puzzles/<file>.xd.
Spec: lab_spec.py defines PUZZLES = [dict(file, title, grid=[rows], clues={...},
difficulty="peaceful"|"challenging")]; difficulty defaults to challenging.
The structural clue checks are check_clues.clue_errors (run check_clues.py first
for the corpus checks too); nothing is written while any puzzle has an error."""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(REPO, "tools")); sys.path.insert(0, HERE)
import xwrecord as xw
import check_clues
puzzles = check_clues.load_spec(os.path.join(HERE, "lab_spec.py"))
bad = 0
for p in puzzles:
    grid = [r.replace(' ', '') for r in p["grid"]]
    probe = check_clues.lex.probe_entries(grid)
    clues = {}
    for dkey, num, ans in probe:
        key = "%s%d" % (dkey, num)
        cl = p["clues"].get(key)
        for e in check_clues.clue_errors(ans, cl): print(e, p["file"], key, ans, cl or ""); bad += 1
        if cl: clues[(dkey, num)] = (cl, ans)
    extra = set(p["clues"]) - set("%s%d" % (d, n) for d, n, _ in probe)
    if extra: print("EXTRA KEYS", p["file"], sorted(extra)); bad += 1
    built = xw.build_entries(grid, clues)
    if not built:
        print("BUILD FAILED", p["file"]); bad += 1; continue
    p["_built"] = (grid, built)
if bad:
    print("errors:", bad, "(nothing written)"); sys.exit(1)
for p in puzzles:
    grid, (rows, cols, entries) = p["_built"]
    tier = xw.tier_of_name(p.get("difficulty"), default=1)   # peaceful=0, challenging=1
    reg = "peaceful" if tier == 0 else "hard"
    xw.write_xd(os.path.join(REPO, "puzzles", p["file"] + ".xd"), p["title"], grid, entries, tier,
                extra_meta={"Author": "generated", "Source": "generated fill (nzfeng core + corpus-familiar extras, wordfreq-gated), original %s-register clues" % reg})
    print("wrote", p["file"], xw.TIER_NAMES[tier], len(entries), "entries")
