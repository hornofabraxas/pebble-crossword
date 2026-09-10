/*
 * puzzle.c: the bundled flash library and the puzzle record parser.
 *
 * Blob (RESOURCE_ID_PUZZLES, packed by tools/xd2blob.py): "XWD3", u16 count,
 * u16 dict_bytes, the clue-text dictionary (u8 nentries, then per entry u8 len +
 * bytes), u32 offset[count], then the records, ordered by tier. Only the offset
 * table and the dictionary are kept in RAM; a record is read into g->pbuf when it
 * is played, and its clue bytes are token-expanded through the dictionary.
 *
 * Record v2 (tools/xwrecord.py): u8 rows, cols, nentries, titlelen, tier; title;
 * rows*cols solution bytes (0 = block, else 'A'..'Z'); then per entry u8 dir,
 * number, row, col, len, cluelen + clue bytes (not NUL-terminated).
 *
 * Each puzzle also gets an id: an FNV-1a hash of its rows, cols, tier and
 * solution grid, computed from flash at init. Ids survive a repack (a clue edit
 * or an added puzzle moves records around; the grid does not change), so
 * store.c keys the solved bitmap and the progress slots by id across releases.
 */
#include "crossword.h"

#define REC_HDR 5   // rows, cols, nentries, titlelen, tier

static uint32_t rd_u32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t rd_u16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }

static size_t read_res(uint32_t off, void *buf, size_t n) {
  return resource_load_byte_range(g->res, off, (uint8_t *)buf, n);
}

// FNV-1a over a record's rows, cols, tier and solution grid: the puzzle's
// identity, independent of its title, its clues, and its place in the blob.
static uint32_t hash_record(int i) {
  uint8_t hdr[REC_HDR];
  read_res(g->offsets[i], hdr, REC_HDR);
  uint32_t h = 2166136261u;
  h = (h ^ hdr[0]) * 16777619u;
  h = (h ^ hdr[1]) * 16777619u;
  h = (h ^ (hdr[4] > 1 ? 1 : hdr[4])) * 16777619u;
  uint32_t off = g->offsets[i] + REC_HDR + hdr[3], left = (uint32_t)hdr[0] * hdr[1];
  if (g->offsets[i] + REC_HDR + hdr[3] + left > g->offsets[i + 1]) return h;   // truncated: hashed as far as it goes
  uint8_t chunk[32];
  while (left) {
    uint32_t n = left < sizeof(chunk) ? left : sizeof(chunk);
    if (read_res(off, chunk, n) < n) break;
    for (uint32_t k = 0; k < n; k++) h = (h ^ chunk[k]) * 16777619u;
    off += n; left -= n;
  }
  return h;
}

bool puzzle_init_library(void) {
  g->res = resource_get_handle(RESOURCE_ID_PUZZLES);
  g->res_size = resource_size(g->res);
  if (g->res_size < 8) return false;
  uint8_t head[8];
  if (read_res(0, head, 8) < 8) return false;
  if (memcmp(head, "XWD3", 4) != 0) return false;
  g->puzzle_count = rd_u16(head + 4);
  if (g->puzzle_count == 0) return false;
  // The solved bitmap is 32 bytes and slots index bundled puzzles with a u8.
  if (g->puzzle_count > 255) g->puzzle_count = 255;
  int cnt = g->puzzle_count;

  // Clue-text dictionary: u8 nentries, then per entry u8 len + bytes.
  uint16_t dict_bytes = rd_u16(head + 6);
  uint32_t tbl_off = 8 + dict_bytes;
  if (dict_bytes > 0) {
    g->dict = malloc(dict_bytes);
    if (!g->dict) return false;
    read_res(8, g->dict, dict_bytes);
    g->dict_n = g->dict[0];
    if (g->dict_n > 128) return false;
    uint32_t p = 1;
    for (int i = 0; i < g->dict_n; i++) {
      if (p >= dict_bytes) return false;
      uint8_t L = g->dict[p++];
      g->dict_eoff[i] = p;
      g->dict_elen[i] = L;
      p += L;
      if (p > dict_bytes) return false;
    }
  }

  g->offsets = malloc((cnt + 1) * sizeof(uint32_t));
  if (!g->offsets) return false;
  uint8_t *tbl = malloc(cnt * 4);
  if (!tbl) return false;
  read_res(tbl_off, tbl, cnt * 4);
  for (int i = 0; i < cnt; i++) g->offsets[i] = rd_u32(tbl + 4 * i);
  free(tbl);
  g->offsets[cnt] = g->res_size;

  g->ids = malloc(cnt * sizeof(uint32_t));
  if (!g->ids) return false;
  for (int i = 0; i < cnt; i++) g->ids[i] = hash_record(i);
  return true;
}

uint32_t puzzle_id_of(int i) { return (i >= 0 && i < g->puzzle_count) ? g->ids[i] : 0; }

// A signature of the bundled set as a whole: count, size, titles and tiers.
// store.c compares it with the one it saved; a mismatch means the records may
// have moved, and the id table says where each went. The formula is the one
// every earlier release wrote, so a watch upgrading from one of them matches
// when the blob has not changed and keeps its progress without an id table.
uint32_t puzzle_bundle_signature(void) {
  uint32_t h = 2166136261u ^ g->puzzle_count ^ (g->res_size << 8);
  for (int i = 0; i < g->puzzle_count; i++) {
    char t[TITLE_LEN];
    puzzle_title_of(i, t, sizeof(t));
    for (const char *p = t; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
    uint8_t hdr[REC_HDR];
    read_res(g->offsets[i], hdr, REC_HDR);
    h = (h ^ (uint32_t)hdr[4]) * 16777619u;   // the raw tier byte
  }
  return h;
}

// Parse the record in g->pbuf into the play model. Resets play state; does NOT
// touch fill or the cur_* identity. Returns false if malformed.
static bool parse_loaded(void) {
  const uint8_t *buf = g->pbuf;
  size_t sz = g->pbuf_size;
  if (!buf || sz < REC_HDR) return false;
  int rows = buf[0], cols = buf[1], nentries = buf[2], tlen = buf[3];
  int ncells = rows * cols;
  if (rows <= 0 || cols <= 0 || nentries <= 0 || rows > MAX_DIM || cols > MAX_DIM) return false;
  if ((size_t)(REC_HDR + tlen + ncells) > sz) return false;
  g->cur_tier = buf[4] > 1 ? 1 : buf[4];

  g->rows = rows;
  g->cols = cols;
  g->nentries = nentries > MAX_ENTRIES ? MAX_ENTRIES : nentries;
  int tn = tlen < TITLE_LEN - 1 ? tlen : TITLE_LEN - 1;
  memcpy(g->title, buf + REC_HDR, tn);
  g->title[tn] = 0;

  g->solution = buf + REC_HDR + tlen;
  const uint8_t *q = g->solution + ncells;
  memset(g->owner, NO_ENTRY, sizeof(g->owner));
  for (int k = 0; k < g->nentries; k++) {
    // Guard a truncated record.
    if ((size_t)((q + 6) - buf) > sz) { g->nentries = k; break; }
    Entry *e = &g->entries[k];
    e->dir = q[0] ? 1 : 0;
    e->number = q[1];
    e->row = q[2];
    e->col = q[3];
    e->len = q[4];
    e->clue_len = q[5];
    e->clue_off = (uint16_t)((q + 6) - buf);
    if ((size_t)(e->clue_off + e->clue_len) > sz) { g->nentries = k; break; }
    q += 6 + e->clue_len;
    // Geometry must sit inside the grid: every consumer indexes fill[] / wrong[]
    // through puzzle_cell() without further checks.
    int end_r = e->dir ? e->row + e->len : e->row + 1;
    int end_c = e->dir ? e->col + 1 : e->col + e->len;
    if (e->len == 0 || e->row >= rows || e->col >= cols || end_r > rows || end_c > cols) return false;
    // Owner table: which entry of each direction runs through each cell.
    for (int i = 0; i < e->len; i++) {
      int r, c;
      puzzle_entry_cell(e, i, &r, &c);
      g->owner[e->dir][r * cols + c] = k;
    }
  }
  if (g->nentries == 0) return false;

  memset(g->fill, 0, sizeof(g->fill));
  memset(g->wrong, 0, sizeof(g->wrong));
  g->cur_entry = 0;
  g->focus_cell = 0;
  g->solved = false;
  g->elapsed = 0;
  g->dirty = false;
  g->banner[0] = 0;
  return true;
}

bool puzzle_load_bundled(int i) {
  if (i < 0 || i >= g->puzzle_count) return false;
  uint32_t off = g->offsets[i];
  uint32_t sz = g->offsets[i + 1] - off;
  if (sz < REC_HDR || sz > REC_MAX) return false;
  uint8_t *buf = malloc(sz);
  if (!buf) return false;
  if (read_res(off, buf, sz) < sz) { free(buf); return false; }
  if (g->pbuf) free(g->pbuf);
  g->pbuf = buf;
  g->pbuf_size = sz;
  if (!parse_loaded()) return false;
  g->cur_bidx = i;
  return true;
}

void puzzle_title_of(int i, char *buf, int cap) {
  uint8_t hdr[REC_HDR];
  read_res(g->offsets[i], hdr, REC_HDR);
  int tlen = hdr[3];
  int n = tlen < cap - 1 ? tlen : cap - 1;
  read_res(g->offsets[i] + REC_HDR, buf, n);
  buf[n] = 0;
}

int puzzle_tier_of(int i) {
  uint8_t hdr[REC_HDR];
  read_res(g->offsets[i], hdr, REC_HDR);
  return hdr[4] > 1 ? 1 : hdr[4];
}

int puzzle_cell(int r, int c) { return r * g->cols + c; }
bool puzzle_is_block(int r, int c) {
  if (r < 0 || c < 0 || r >= g->rows || c >= g->cols) return true;
  return g->solution[puzzle_cell(r, c)] == 0;
}
void puzzle_entry_cell(const Entry *e, int k, int *r, int *c) {
  if (e->dir == 0) { *r = e->row; *c = e->col + k; }
  else { *r = e->row + k; *c = e->col; }
}
const Entry *puzzle_cur(void) { return &g->entries[g->cur_entry]; }

void puzzle_answer(const Entry *e, char *buf, int cap) {
  int n = e->len < cap - 1 ? e->len : cap - 1;
  for (int k = 0; k < n; k++) {
    int r, c;
    puzzle_entry_cell(e, k, &r, &c);
    buf[k] = g->solution[puzzle_cell(r, c)];
  }
  buf[n] = 0;
}

const char *puzzle_clue(const Entry *e, char *buf, int cap) {
  // Clue bytes are token-compressed (XWD3): a byte with the high bit set is
  // token i = byte & 0x7F, which expands to dictionary entry i; other bytes are
  // literal. Expansion is a single pass and stops short of the buffer.
  const uint8_t *src = g->pbuf + e->clue_off;
  int out = 0, lim = cap - 1;
  for (int i = 0; i < e->clue_len && out < lim; i++) {
    uint8_t b = src[i];
    if (b & 0x80) {
      int t = b & 0x7F;
      if (t >= g->dict_n) continue;
      const uint8_t *ent = g->dict + g->dict_eoff[t];
      int L = g->dict_elen[t];
      for (int k = 0; k < L && out < lim; k++) buf[out++] = ent[k];
    } else {
      buf[out++] = b;
    }
  }
  buf[out] = 0;
  return buf;
}

bool puzzle_complete(void) {
  int nc = g->rows * g->cols;
  for (int k = 0; k < nc; k++)
    if (g->solution[k] != 0 && g->fill[k] == 0) return false;
  return true;
}

// Filled cells whose letter (pen or pencil) differs from the solution.
int puzzle_count_wrong(void) {
  int nc = g->rows * g->cols, wrong = 0;
  for (int k = 0; k < nc; k++) {
    char f = g->fill[k];
    if (!f || g->solution[k] == 0) continue;
    if (f >= 'a' && f <= 'z') f -= 32;
    if (f != (char)g->solution[k]) wrong++;
  }
  return wrong;
}

bool puzzle_bundled_done(int i) { return (g->done[i >> 3] >> (i & 7)) & 1; }
void puzzle_set_bundled_done(int i, bool done) {
  if (done) g->done[i >> 3] |= (1 << (i & 7));
  else g->done[i >> 3] &= ~(1 << (i & 7));
}

// A puzzle's class follows from its grid: minis are the small grids.
int puzzle_class_of_rows(int rows) { return rows <= MINI_MAX_ROWS ? CLS_MINI : CLS_REGULAR; }
static bool in_pool(int i, int cls, int tier) {
  uint8_t hdr[REC_HDR];
  read_res(g->offsets[i], hdr, REC_HDR);
  int t = hdr[4] > 1 ? 1 : hdr[4];
  return puzzle_class_of_rows(hdr[0]) == cls && t == tier;
}

// Bundled puzzles of a class and tier that exist at all (played or not).
int puzzle_pool_total(int cls, int tier) {
  int n = 0;
  for (int i = 0; i < g->puzzle_count; i++) if (in_pool(i, cls, tier)) n++;
  return n;
}

// Bundled puzzles of a class and tier the player has solved.
int puzzle_pool_solved(int cls, int tier) {
  int n = 0;
  for (int i = 0; i < g->puzzle_count; i++) if (in_pool(i, cls, tier) && puzzle_bundled_done(i)) n++;
  return n;
}

// Reservoir pick over a pool's unplayed bundled puzzles; if none, over its
// solved ones (a replay: *replay is set, and the solved mark stays, so the
// count of puzzles solved never goes down; a replay already in progress may
// come up again and resumes); -1 if the pool is empty.
int puzzle_pick_bundled(int cls, int tier, bool *replay) {
  int chosen = -1, seen = 0;
  *replay = false;
  for (int i = 0; i < g->puzzle_count; i++) {
    if (!in_pool(i, cls, tier)) continue;
    if (puzzle_bundled_done(i) || store_slot_find_bundled(i) >= 0) continue;
    if ((rand() % (++seen)) == 0) chosen = i;
  }
  if (chosen >= 0) return chosen;
  seen = 0;
  for (int i = 0; i < g->puzzle_count; i++)
    if (in_pool(i, cls, tier) && puzzle_bundled_done(i) && (rand() % (++seen)) == 0) chosen = i;
  if (chosen >= 0) *replay = true;
  return chosen;
}
