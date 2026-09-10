#!/usr/bin/env python3
"""xd2blob.py: pack the bundled starter set (puzzles/*.xd) into
resources/data/puzzles.bin, the flash blob the watch plays offline.

Blob (little-endian):
  magic     : "XWD3"
  count     : u16
  dict_bytes: u16      size of the clue-text dictionary that follows
  dict      : u8 nentries, then per entry (u8 len, len bytes)  [tools/cluepack.py]
  offset    : u32[count]   byte offset of each record from the start of the file
  records   : see tools/xwrecord.py (record v2), clue bytes token-compressed

Each .xd carries a "Difficulty: peaceful|challenging" header; a missing header
means challenging (tier 1).
Records are ordered by class (minis first), tier, then title; the watch picks
within a class and tier by scanning record headers.

Usage:
  python3 tools/xd2blob.py [puzzles_dir] [out.bin]
"""
import glob
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xwrecord as xw  # noqa: E402
import cluepack as cp  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)


def main():
    pdir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(REPO, 'puzzles')
    outp = sys.argv[2] if len(sys.argv) > 2 else os.path.join(REPO, 'resources', 'data', 'puzzles.bin')
    files = sorted(glob.glob(os.path.join(pdir, '*.xd')))
    if not files:
        print("no .xd files in %s" % pdir, file=sys.stderr)
        sys.exit(1)

    parsed = []
    all_clues = []
    for f in files:
        meta, grid, clues = xw.parse_xd(f)
        built = xw.build_entries(grid, clues)
        if not built:
            print("  skip %s: invalid or over the watch's limits" % os.path.basename(f))
            continue
        rows, cols, entries = built
        title = meta.get('title') or os.path.splitext(os.path.basename(f))[0]
        tier = xw.tier_of_name(meta.get('difficulty'), default=1)
        parsed.append((rows, cols, grid, entries, title, tier))
        all_clues += [e[5] for e in entries]
    if not parsed:
        print("nothing packable", file=sys.stderr)
        sys.exit(1)

    # One shared dictionary over every clue; each clue stored token-compressed.
    for c in all_clues:
        if not c.isascii():
            print("clue has non-ASCII characters (use plain ASCII): %r" % c, file=sys.stderr); sys.exit(1)
    entries_dict = cp.build_dict(all_clues)
    codec = lambda clue: cp.compress(entries_dict, clue)
    # Safety: the compressed clue must round-trip and fit the u8 length field.
    for c in all_clues:
        comp = codec(c)
        assert cp.decompress(entries_dict, comp) == c, "clue roundtrip failed: %r" % c
        assert len(comp) < 256

    puzzles = []
    for (rows, cols, grid, entries, title, tier) in parsed:
        rec = xw.pack_record(title, rows, cols, grid, entries, tier, clue_codec=codec)
        if len(rec) > xw.MAX_REC:
            print("record too large for the watch (%d > %d): %s" % (len(rec), xw.MAX_REC, title), file=sys.stderr); sys.exit(1)
        puzzles.append((0 if rows <= 7 else 1, tier, title, rec))
    puzzles.sort(key=lambda p: (p[0], p[1], p[2]))
    records = [p[3] for p in puzzles]

    dictblob = cp.pack_dict(entries_dict)
    header = bytearray(b'XWD3')
    header += struct.pack('<HH', len(records), len(dictblob))
    header += dictblob
    off = len(header) + 4 * len(records)
    for rec in records:
        header += struct.pack('<I', off)
        off += len(rec)
    blob = bytes(header) + b''.join(records)
    os.makedirs(os.path.dirname(outp), exist_ok=True)
    with open(outp, 'wb') as f:
        f.write(blob)

    minis = sum(1 for p in puzzles if p[0] == 0)
    by_tier = ", ".join("%s %d" % (xw.TIER_NAMES[t], sum(1 for p in puzzles if p[1] == t)) for t in range(len(xw.TIER_NAMES)))
    raw = sum(len(c.encode('utf-8')) for c in all_clues)
    comp = sum(len(codec(c)) for c in all_clues)
    print("packed %d puzzles (%d minis, %d regulars; %s) -> %s (%d bytes)" % (len(records), minis, len(records) - minis, by_tier, outp, len(blob)))
    print("  dict %d entries (%d bytes); clue text %d -> %d bytes (%.0f%%); largest record %d"
          % (len(entries_dict), len(dictblob), raw, comp, 100.0 * comp / raw, max(len(r) for r in records)))


if __name__ == '__main__':
    main()
