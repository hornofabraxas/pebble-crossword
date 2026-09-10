"""check_caps.py: enforce the bundle's answer-repeat cap model as a ship gate.

The shipped model (see project docs): an answer may appear at most
  - 5 times across ALL minis (7x7), counted bundle-wide, and
  - 6 times within a single regular (15x15) difficulty shelf (challenging and
    peaceful counted separately).
Cross-shelf and mini-vs-regular repeats are separate shelves and not summed: a
solver sees one puzzle at a time, and a 15x15 needs common short glue.

Mini/regular is inferred from grid height (7 = mini, else regular). Difficulty
comes from each puzzle's `difficulty` field. Runs on the standard library plus
lex (no wordfreq), so CI can call it with system python3.

Usage: python3 tools/lab/check_caps.py [tools/lab/lab_spec.py]
Exits non-zero and prints each violation if the caps are exceeded.
"""
import collections, importlib.util, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE)); sys.path.insert(0, HERE)
import lex

MINI_CAP = 5   # bundle-wide, across every 7x7
REG_CAP = 6    # per difficulty shelf, within the 15x15s

def load(path):
    s = importlib.util.spec_from_file_location("lab_spec", path)
    m = importlib.util.module_from_spec(s); s.loader.exec_module(m)
    return m.PUZZLES

def violations(puzzles):
    mini = collections.Counter()
    reg = collections.defaultdict(collections.Counter)  # difficulty -> Counter
    for p in puzzles:
        grid = [r.replace(' ', '') for r in p["grid"]]
        is_mini = len(grid) == 7
        diff = p.get("difficulty", "challenging")
        for dkey, num, ans in lex.probe_entries(grid):
            if is_mini: mini[ans] += 1
            else: reg[diff][ans] += 1
    out = []
    for ans, n in sorted(mini.items(), key=lambda x: -x[1]):
        if n > MINI_CAP: out.append("MINI-OVER-CAP  %s used %d > %d (bundle-wide minis)" % (ans, n, MINI_CAP))
    for diff, cnt in sorted(reg.items()):
        for ans, n in sorted(cnt.items(), key=lambda x: -x[1]):
            if n > REG_CAP: out.append("REG-OVER-CAP   %s used %d > %d (regular %s shelf)" % (ans, n, REG_CAP, diff))
    return out

if __name__ == "__main__":
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "lab_spec.py")
    v = violations(load(path))
    for line in v: print(line)
    print("cap violations:", len(v), "(mini cap %d bundle-wide, regular cap %d per shelf)" % (MINI_CAP, REG_CAP))
    sys.exit(1 if v else 0)
