"""cluepack.py: a static shared-dictionary packer for clue text.

Pebble has no zlib, so instead of per-record DEFLATE we build ONE dictionary of
the most space-saving byte-strings across every clue and replace each with a
single token byte (0x80..0xFF). Clue text is 7-bit ASCII, so bytes >= 0x80 are
free as tokens. Decompression on the watch is a single pass: copy a literal
byte, or emit the dictionary entry a token stands for. Dictionary entries hold
literal bytes only (never tokens), so expansion never recurses.

This exploits redundancy ACROSS clues (shared vocabulary), which per-record
DEFLATE cannot see, and the decoder is a dozen lines of C with no allocation.

Format of the dictionary blob: u8 nentries, then nentries x (u8 len, len bytes).
"""
MAX_ENTRIES = 128          # tokens 0x80..0xFF
MIN_LEN, MAX_LEN = 2, 12   # candidate substring lengths

def build_dict(clues):
    import collections
    # Work on a mutable list of clue byte arrays; iteratively pick the substring
    # with the greatest total saving (occurrences * (len - 1)), reserve a token
    # for it, and mask its occurrences so later picks do not double-count.
    texts = [c.encode('utf-8') for c in clues]
    for c in texts:
        assert all(b < 0x80 for b in c), "clue has non-ASCII byte: %r" % c
    entries = []
    MASK = 0xFF  # a byte value that cannot appear in ASCII text, used to blank picked spans
    for _ in range(MAX_ENTRIES):
        counts = collections.Counter()
        for c in texts:
            n = len(c)
            for L in range(MIN_LEN, MAX_LEN + 1):
                for i in range(0, n - L + 1):
                    span = c[i:i+L]
                    if MASK in span: continue
                    counts[bytes(span)] += 1
        best, best_score = None, 0
        for span, freq in counts.items():
            if freq < 2: continue
            score = freq * (len(span) - 1)
            if score > best_score: best, best_score = span, score
        if not best or best_score <= 2: break
        entries.append(best)
        # mask occurrences of `best` in every text so overlaps are not re-picked
        b = best; L = len(b)
        for ci, c in enumerate(texts):
            out = bytearray(); i = 0
            while i < len(c):
                if c[i:i+L] == b: out += bytes([MASK]) * L; i += L
                else: out.append(c[i]); i += 1
            texts[ci] = bytes(out)
    return entries

def compress(entries, s):
    b = s.encode('utf-8')
    # tokens tried longest-first so a longer entry wins over a shorter prefix
    order = sorted(range(len(entries)), key=lambda i: -len(entries[i]))
    out = bytearray(); i = 0
    while i < len(b):
        hit = None
        for idx in order:
            e = entries[idx]
            if b[i:i+len(e)] == e: hit = idx; break
        if hit is not None:
            out.append(0x80 | hit); i += len(entries[hit])
        else:
            out.append(b[i]); i += 1
    return bytes(out)

def decompress(entries, data):
    out = bytearray()
    for byte in data:
        if byte & 0x80: out += entries[byte & 0x7F]
        else: out.append(byte)
    return out.decode('utf-8')

def pack_dict(entries):
    out = bytearray([len(entries)])
    for e in entries:
        assert len(e) < 256
        out.append(len(e)); out += e
    return bytes(out)
