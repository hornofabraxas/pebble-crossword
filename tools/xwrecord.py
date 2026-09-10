"""xwrecord.py: the puzzle record format shared by the watch bundle (xd2blob.py)
and the puzzle CDN (build_corpus.py), plus .xd parsing and difficulty scoring.

Record v2 (all integers little-endian; parsed by src/c/puzzle.c):
    rows      : u8
    cols      : u8
    nentries  : u8
    titlelen  : u8
    tier      : u8      0 easy, 1 medium, 2 hard (see score_tiers)
    title     : titlelen bytes (UTF-8)
    grid      : rows*cols bytes   solution; 0 == block, else 'A'..'Z'
    entries   : nentries x:
        dir     : u8    0 = Across, 1 = Down
        number  : u8
        row     : u8
        col     : u8
        len     : u8
        cluelen : u8,   clue bytes (UTF-8, not NUL-terminated)

v1 (v0.1..v0.5) had no tier byte. The bundled blob magic is "XWD2" for v2 and
the CDN serves v2 under /v2, so a client never has to sniff the version.

Limits mirror the watch (src/c/crossword.h): grids up to 15x15 (a 16x16 fill
plus its timer would not fit a 256-byte persist value), <= 96 entries, answers
<= 11 letters (wrapped 6 + 5 on the watch; a 28px box floor).
"""
import math
import os
import re
import struct

MAX_ENTRIES = 96
MAX_DIM = 15
MAX_REC = 8192   # a packed record must fit the watch's REC_MAX
MAX_CLUE = 200
MAX_ANS = 11
TIER_NAMES = ("peaceful", "challenging")   # the two difficulties; every bundled puzzle is one of them
POOL_TIERS = TIER_NAMES                          # both tiers are pools

CLUE_RE = re.compile(r'^([AD])(\d+)\.\s*(.*?)\s*~\s*([A-Za-z]+)\s*$')


# ---------------------------------------------------------------------------
# .xd parsing
# ---------------------------------------------------------------------------
def parse_xd(path):
    """Returns (meta dict, grid rows list or None, {(dir,num): (clue, ANSWER)})."""
    txt = open(path, encoding='utf-8', errors='replace').read().replace('\r\n', '\n')
    lines = txt.split('\n')
    meta = {}
    for ln in lines:
        if ':' in ln and not CLUE_RE.match(ln.strip()):
            k, v = ln.split(':', 1)
            k = k.strip().lower()
            if k in ('title', 'author', 'editor', 'date', 'difficulty', 'size'):
                meta[k] = v.strip()
    grid = None
    i = 0
    while i < len(lines):
        if lines[i].strip() and re.fullmatch(r'[A-Za-z#._]+', lines[i]):
            blk = []
            while i < len(lines) and lines[i].strip() and re.fullmatch(r'[A-Za-z#._]+', lines[i]):
                blk.append(lines[i].upper().replace('_', '#').replace('.', '#'))
                i += 1
            if len(blk) >= 2 and len(set(len(b) for b in blk)) == 1:
                grid = blk
                break
        i += 1
    clues = {}
    for ln in lines:
        m = CLUE_RE.match(ln.strip())
        if m:
            clues[(m.group(1), int(m.group(2)))] = (m.group(3), m.group(4).upper())
    return meta, grid, clues


def build_entries(grid, clues, require_15=False):
    """Validate + extract entries. Returns (rows, cols, entries) or None.
    Strict: solution grid of [A-Z#] only (no rebus), every run clued with a
    clue whose answer matches the grid, within the watch's caps."""
    if not grid:
        return None
    rows, cols = len(grid), len(grid[0])
    if rows > MAX_DIM or cols > MAX_DIM:
        return None
    if require_15 and (rows != 15 or cols != 15):
        return None
    for r in grid:
        if not re.fullmatch(r'[A-Z#]+', r):
            return None

    def white(r, c):
        return 0 <= r < rows and 0 <= c < cols and grid[r][c] != '#'

    n = 1
    acr, dwn = {}, {}
    for r in range(rows):
        for c in range(cols):
            if grid[r][c] == '#':
                continue
            sa = (not white(r, c - 1)) and white(r, c + 1)
            sd = (not white(r - 1, c)) and white(r + 1, c)
            if sa or sd:
                if sa:
                    L = 0
                    while white(r, c + L):
                        L += 1
                    acr[n] = (r, c, L)
                if sd:
                    L = 0
                    while white(r + L, c):
                        L += 1
                    dwn[n] = (r, c, L)
                n += 1
    entries = []   # (dir, number, r, c, len, clue)
    for d, table, key in ((0, acr, 'A'), (1, dwn, 'D')):
        for num, (r, c, L) in table.items():
            cl = clues.get((key, num))
            want = ''.join(grid[r][c + k] if d == 0 else grid[r + k][c] for k in range(L))
            if not cl or cl[1] != want or not cl[0] or len(cl[0].encode('utf-8')) > MAX_CLUE:
                return None
            entries.append((d, num, r, c, L, cl[0]))
    if not entries or len(entries) > MAX_ENTRIES or n > 256:
        return None
    if max(e[4] for e in entries) > MAX_ANS:
        return None
    return rows, cols, entries


# ---------------------------------------------------------------------------
# Record pack / unpack
# ---------------------------------------------------------------------------
def pack_record(title, rows, cols, grid, entries, tier, clue_codec=None):
    """clue_codec(str)->bytes compresses each clue; default stores UTF-8 as-is."""
    tb = title.encode('utf-8')[:255]
    out = bytearray()
    out += struct.pack('<BBBBB', rows, cols, len(entries), len(tb), tier)
    out += tb
    for r in range(rows):
        for c in range(cols):
            ch = grid[r][c]
            out.append(0 if ch == '#' else ord(ch))
    for (d, num, r, c, L, clue) in entries:
        cb = clue_codec(clue) if clue_codec else clue.encode('utf-8')
        cb = cb[:255]
        out += struct.pack('<BBBBBB', d, num, r, c, L, len(cb))
        out += cb
    return bytes(out)


def unpack_record(rec, version=2):
    """Inverse of pack_record for the RAW (uncompressed) clue bytes. If the
    record was packed with a clue_codec (the bundle's XWD3 token compression),
    the returned clue strings are the compressed bytes, not readable text; decode
    them with tools/cluepack.decompress and the blob's dictionary. Version 1
    records have no tier byte. Returns dict(title, rows, cols, grid, entries, tier)."""
    if version == 2:
        rows, cols, ne, tl, tier = struct.unpack_from('<BBBBB', rec, 0)
        p = 5
    else:
        rows, cols, ne, tl = struct.unpack_from('<BBBB', rec, 0)
        tier = 1
        p = 4
    title = rec[p:p + tl].decode('utf-8', 'replace')
    p += tl
    g = rec[p:p + rows * cols]
    p += rows * cols
    grid = [''.join('#' if g[r * cols + c] == 0 else chr(g[r * cols + c]) for c in range(cols))
            for r in range(rows)]
    entries = []
    for _ in range(ne):
        d, num, r, c, L, cl = struct.unpack_from('<BBBBBB', rec, p)
        p += 6
        clue = rec[p:p + cl].decode('utf-8', 'replace')
        p += cl
        entries.append((d, num, r, c, L, clue))
    return dict(title=title, rows=rows, cols=cols, grid=grid, entries=entries, tier=tier)


def answers_of(grid, entries):
    out = []
    for (d, num, r, c, L, clue) in entries:
        out.append(''.join(grid[r][c + k] if d == 0 else grid[r + k][c] for k in range(L)))
    return out


# ---------------------------------------------------------------------------
# Difficulty
#
# Pre-1964 NYT dailies ramp only slightly across the week (Monday about 9.5%
# rare answers, Saturday about 12.4%) while puzzle-to-puzzle spread is wide
# (5% to 16% between the 10th and 90th percentile), so weekday alone is a poor
# signal. Difficulty is scored from the fill itself: the share of answers that
# are rare across the whole library (appear at most twice), with the mean log
# frequency as a tiebreaker. Puzzles are then cut into terciles.
# ---------------------------------------------------------------------------
def answer_frequencies(all_answer_lists):
    freq = {}
    for answers in all_answer_lists:
        for a in answers:
            freq[a] = freq.get(a, 0) + 1
    return freq


def difficulty_score(answers, freq):
    if not answers:
        return 0.0
    rare = sum(1 for a in answers if freq.get(a, 0) <= 2) / len(answers)
    mlf = sum(math.log(max(freq.get(a, 1), 1)) for a in answers) / len(answers)
    # rare share dominates; a lower mean frequency nudges toward harder.
    return rare * 10.0 - mlf * 0.1


def score_tiers(scores):
    """scores: {id: score}. Returns {id: tier} by terciles (0 easy .. 2 hard)."""
    ordered = sorted(scores, key=lambda k: scores[k])
    n = len(ordered)
    tiers = {}
    for i, pid in enumerate(ordered):
        tiers[pid] = 0 if i < n / 3 else 1 if i < 2 * n / 3 else 2
    return tiers


def tier_of_name(name, default=1):   # default 1 = challenging
    name = (name or '').strip().lower()
    return TIER_NAMES.index(name) if name in TIER_NAMES else default


def write_xd(path, title, grid, entries, tier, extra_meta=None):
    """Emit a human-readable .xd file (the bundle's source of truth)."""
    lines = ["Title: %s" % title]
    for k, v in (extra_meta or {}).items():
        lines.append("%s: %s" % (k, v))
    lines.append("Difficulty: %s" % TIER_NAMES[tier])
    lines.append("Size: %dx%d" % (len(grid), len(grid[0])))
    lines.append("")
    lines.append("")
    lines += grid
    lines.append("")
    lines.append("")
    acr = [e for e in entries if e[0] == 0]
    dwn = [e for e in entries if e[0] == 1]
    ans = dict(zip(entries, answers_of(grid, entries)))
    for e in acr:
        lines.append("A%d. %s ~ %s" % (e[1], e[5], ans[e]))
    lines.append("")
    for e in dwn:
        lines.append("D%d. %s ~ %s" % (e[1], e[5], ans[e]))
    lines.append("")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines))
