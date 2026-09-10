"""dump_ledger.py: write a bundle-wide answer-usage ledger {ANSWER: count} as
JSON, counted from a lab spec (default tools/lab/lab_spec.py). Used to seed the
generator's soft saturation steering (LAB_LEDGER) so newly generated grids
avoid answers already near the bundle-wide cap.

Usage: dump_ledger.py [out.json] [lab_spec.py]
"""
import importlib.util, json, os, sys, collections
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lex

out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "ledger.json")
spec_path = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "lab_spec.py")
spec = importlib.util.spec_from_file_location("lab_spec", spec_path)
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)

c = collections.Counter()
for p in m.PUZZLES:
    for _, _, a in lex.probe_entries([r.replace(' ', '') for r in p["grid"]]):
        c[a] += 1
json.dump(dict(c), open(out, "w"))
over = sum(1 for n in c.values() if n >= 5)
print("ledger: %d distinct answers from %d puzzles (%d already >=5) -> %s" % (len(c), len(m.PUZZLES), over, out))
