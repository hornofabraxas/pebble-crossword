/*
 * store.c: persistent state.
 *
 * Pebble persist is 256 bytes per value and about 4 KB per app in total, so the
 * layout is budgeted:
 *
 *   PKEY_SCHEMA      int      schema version (an unknown one wipes everything)
 *   PKEY_SETTINGS    bytes    Settings
 *   PKEY_DONE        bytes    solved bitmap for the bundled set (32 B), by bundle index
 *   PKEY_SLOTS       bytes    Slot[MAX_SLOTS] directory, most recent first
 *   PKEY_BUNDLE      int      signature of the bundled set the bitmap and slots refer to
 *   PKEY_IDS + k     bytes    the bundle's puzzle ids in bundle order, IDS_PER_KEY per key
 *   PKEY_FILL + s    bytes    fill[rows*cols] + u32 packed for slot s (<= 229 B)
 *
 * The trailing u32 is elapsed seconds in its low 24 bits; the high byte carries
 * the entry the solver was last on, plus one (0 = not recorded, e.g. a record
 * written before v0.37), so opening a puzzle resumes where it was left. 24 bits
 * of seconds is 194 days of play time, far more than any puzzle accrues, so the
 * two never collide. The record length is unchanged, so older saves and the
 * Continue list's fill counter keep working.
 *
 * 8 slots x 229 + directory + bitmap + ids + settings is about 2.3 KB. The fill
 * write checks its result and a failure is surfaced as a banner rather than
 * silently losing progress; the small bookkeeping writes are best effort.
 *
 * The bitmap and the slots are positional (by bundle index) because that is
 * cheap; the id table is what makes them survive a new bundle. When the saved
 * signature no longer matches the blob, each old position is looked up by its
 * id in the new bundle: solved marks and in-progress puzzles follow their
 * puzzle wherever it moved, and only a puzzle that was actually removed loses
 * its progress. Before v0.26 there was no id table and a mismatch dropped
 * everything; a watch upgrading from then keeps its progress as long as the
 * blob itself is the one it saved against (the signature still matches).
 *
 * Schema 6 (v0.26) shrinks Slot to its four live bytes (an unused phone-puzzle
 * id field was dropped). A schema 5 directory is converted on
 * load; anything older is wiped.
 */
#include "crossword.h"

#define PERSIST_SCHEMA 6
#define PKEY_SCHEMA    10
#define PKEY_SETTINGS  11
#define PKEY_DONE      12
#define PKEY_SLOTS     13
#define PKEY_BUNDLE    14
#define PKEY_ONBOARD   15    // bool: the "how to play" screen has been shown once
#define PKEY_IDS       20    // + chunk, up to 4 chunks (255 puzzles)
#define IDS_PER_KEY    64    // 256 bytes of u32 per key
#define PKEY_FILL      100   // + slot

#define SLOT5_SIZE     17    // schema 5 Slot: used, src, bidx, tier, total, id[12]
#define ELAPSED_MASK   0x00FFFFFFu   // the fill record's trailing u32: low 24 bits = seconds ...
#define ENTRY_SHIFT    24            // ... high byte = last entry + 1 (0 = not recorded)

static void wipe_all(void) {
  for (uint32_t k = 1; k < 32; k++) persist_delete(k);
  for (uint32_t k = 100; k < 100 + 16; k++) persist_delete(k);
  for (uint32_t k = 200; k < 200 + 8; k++) persist_delete(k);   // v0.3/v0.4 phone fills
}

static void slots_save(void) {
  persist_write_data(PKEY_SLOTS, g->slots, sizeof(g->slots));
}
static void done_save(void) {
  persist_write_data(PKEY_DONE, g->done, sizeof(g->done));
}

void store_save_settings(void) {
  persist_write_data(PKEY_SETTINGS, &g->set, sizeof(g->set));
}

bool store_onboard_seen(void) {
  return persist_exists(PKEY_ONBOARD) && persist_read_int(PKEY_ONBOARD);
}
void store_onboard_mark(void) {
  persist_write_int(PKEY_ONBOARD, 1);
}

// ---------------------------------------------------------------------------
// The id table: the bundle's puzzle ids in bundle order, as saved with the
// bitmap and slots. Read back on load to remap them onto a changed bundle.
// ---------------------------------------------------------------------------
static int ids_read(uint32_t *out, int cap) {
  int n = 0;
  for (int k = 0; k < 4 && n < cap; k++) {
    if (!persist_exists(PKEY_IDS + k)) break;
    int want = cap - n < IDS_PER_KEY ? cap - n : IDS_PER_KEY;
    int got = persist_read_data(PKEY_IDS + k, out + n, want * sizeof(uint32_t)) / (int)sizeof(uint32_t);
    if (got <= 0) break;
    n += got;
    if (got < IDS_PER_KEY) break;
  }
  return n;
}
static void ids_write(void) {
  int cnt = g->puzzle_count;
  for (int k = 0; k < 4; k++) {
    int n = cnt - k * IDS_PER_KEY;
    if (n > IDS_PER_KEY) n = IDS_PER_KEY;
    if (n > 0) persist_write_data(PKEY_IDS + k, g->ids + k * IDS_PER_KEY, n * sizeof(uint32_t));
    else persist_delete(PKEY_IDS + k);
  }
}
static int find_id(uint32_t id) {
  for (int i = 0; i < g->puzzle_count; i++) if (g->ids[i] == id) return i;
  return -1;
}

// Compact the slot table in memory so the used slots sit first in their
// current order, moving each fill value to its slot's new key. Fills move
// downwards only and in increasing order, so nothing is overwritten before it
// is read. The directory itself is not written here (see store_load).
static void slots_compact(void) {
  int n = 0;
  uint8_t fill[MAX_CELLS + 4];
  for (int s = 0; s < MAX_SLOTS; s++) {
    if (!g->slots[s].used) continue;
    if (s != n) {
      g->slots[n] = g->slots[s];
      int have = persist_exists(PKEY_FILL + s) ? persist_read_data(PKEY_FILL + s, fill, sizeof(fill)) : 0;
      if (have > 0) persist_write_data(PKEY_FILL + n, fill, have); else persist_delete(PKEY_FILL + n);
    }
    n++;
  }
  for (int s = n; s < MAX_SLOTS; s++) {
    memset(&g->slots[s], 0, sizeof(Slot));
    persist_delete(PKEY_FILL + s);
  }
}

// The bundle changed under a saved bitmap and slot table: carry each solved
// mark and each in-progress puzzle to the puzzle's new position, by id. A
// puzzle no longer in the bundle loses its mark and its slot. In memory only
// (plus the fill moves); the caller writes the directory, the bitmap, the
// signature and the id table back to back, so an interrupted launch leaves
// the old signature with the old table and simply remaps again next time.
static void remap_bundle(const uint32_t *old, int n_old) {
  uint8_t done[32];
  memset(done, 0, sizeof(done));
  for (int i = 0; i < n_old; i++) {
    if (!((g->done[i >> 3] >> (i & 7)) & 1)) continue;
    int j = find_id(old[i]);
    if (j >= 0) done[j >> 3] |= (1 << (j & 7));
  }
  memcpy(g->done, done, sizeof(g->done));
  for (int s = 0; s < MAX_SLOTS; s++) {
    Slot *sl = &g->slots[s];
    if (!sl->used) continue;
    int j = sl->bidx < n_old ? find_id(old[sl->bidx]) : -1;
    if (j < 0) sl->used = false;
    else sl->bidx = j;
  }
  slots_compact();
}

// Drop every mark and slot: the bundle changed and there is no id table to
// follow it with (a watch upgrading from before v0.26 whose blob also changed).
static void drop_bundle_progress(void) {
  memset(g->done, 0, sizeof(g->done));
  memset(g->slots, 0, sizeof(g->slots));
  for (int s = 0; s < MAX_SLOTS; s++) persist_delete(PKEY_FILL + s);
}

// Schema 5 kept a 17-byte Slot with a source byte and a phone id. Read that
// layout into the live one; a phone slot (none since v0.20) reads as unused.
static void slots_load_v5(void) {
  uint8_t raw[MAX_SLOTS * SLOT5_SIZE];
  int n = persist_read_data(PKEY_SLOTS, raw, sizeof(raw));
  for (int s = 0; s < MAX_SLOTS; s++) {
    const uint8_t *p = raw + s * SLOT5_SIZE;
    Slot *sl = &g->slots[s];
    if ((s + 1) * SLOT5_SIZE > n) break;
    sl->used = p[0] && p[1] == 0;
    sl->bidx = p[2];
    sl->tier = p[3];
    sl->total = p[4];
  }
}

void store_load(void) {
  g->set.input = INPUT_VOICE;
  g->set.check = CHECK_EVERY_WORD;
  g->set.tier = 1;
  g->set.scroll = SCROLL_ADAPTIVE;
  g->set.haptics = HAPTICS_ON;
  g->set.speed = SPEED_DEFAULT;
  g->set.buzz_type = TYPEBUZZ_ON;
  g->set.skip = SKIP_ON;
  memset(g->done, 0, sizeof(g->done));
  memset(g->slots, 0, sizeof(g->slots));

  int schema = persist_exists(PKEY_SCHEMA) ? persist_read_int(PKEY_SCHEMA) : 0;
  uint32_t sig = puzzle_bundle_signature();
  if (schema != PERSIST_SCHEMA && schema != PERSIST_SCHEMA - 1) {
    wipe_all();
    persist_write_int(PKEY_SCHEMA, PERSIST_SCHEMA);
    store_save_settings();
    persist_write_int(PKEY_BUNDLE, (int32_t)sig);
    ids_write();
    return;
  }
  if (persist_exists(PKEY_SETTINGS)) {
    // A record from an older version may be shorter (bytes are only appended);
    // the first three fields are the v0.5 shape, the rest default to 0.
    Settings s;
    memset(&s, 0, sizeof(s));
    if (persist_read_data(PKEY_SETTINGS, &s, sizeof(s)) >= 3) {
      g->set.input = s.input == INPUT_TYPE ? INPUT_TYPE : INPUT_VOICE;
      g->set.check = s.check > CHECK_AT_END ? CHECK_EVERY_WORD : s.check;
      g->set.tier = s.tier > 1 ? 1 : s.tier;
      g->set.scroll = s.scroll == SCROLL_FIXED ? SCROLL_FIXED : SCROLL_ADAPTIVE;
      g->set.haptics = s.haptics == HAPTICS_OFF ? HAPTICS_OFF : HAPTICS_ON;
      g->set.speed = s.speed > SPEED_FAST ? SPEED_DEFAULT : s.speed;
      g->set.buzz_type = s.buzz_type == TYPEBUZZ_OFF ? TYPEBUZZ_OFF : TYPEBUZZ_ON;
      g->set.skip = s.skip == SKIP_ON ? SKIP_ON : SKIP_OFF;
    }
  }
  if (persist_exists(PKEY_DONE)) persist_read_data(PKEY_DONE, g->done, sizeof(g->done));
  if (persist_exists(PKEY_SLOTS)) {
    if (schema == PERSIST_SCHEMA) persist_read_data(PKEY_SLOTS, g->slots, sizeof(g->slots));
    else {
      // Convert the directory and stamp the schema at once, so every later
      // write of the new layout happens under schema 6; then tidy any fill a
      // phone slot left behind.
      slots_load_v5();
      slots_save();
      persist_write_int(PKEY_SCHEMA, PERSIST_SCHEMA);
      slots_compact();
      slots_save();
    }
  }

  // Follow the bundle. The saved signature names the blob the bitmap and slots
  // were written against; if the blob is a different one, remap by id (or, with
  // no id table to remap through, start over). Positions past the bundle (a
  // shorter blob under the same signature) cannot be played and go too.
  bool same = persist_exists(PKEY_BUNDLE) && (uint32_t)persist_read_int(PKEY_BUNDLE) == sig;
  bool past = false;
  for (int s = 0; s < MAX_SLOTS; s++)
    if (g->slots[s].used && g->slots[s].bidx >= g->puzzle_count) { g->slots[s].used = false; past = true; }
  if (!same) {
    uint32_t *old = malloc(255 * sizeof(uint32_t));
    int n_old = old ? ids_read(old, 255) : 0;
    if (n_old > 0) remap_bundle(old, n_old);
    else drop_bundle_progress();
    if (old) free(old);
  } else if (past) {
    slots_compact();
  }
  if (!same || past) {
    done_save();
    slots_save();
  }
  if (!same) persist_write_int(PKEY_BUNDLE, (int32_t)sig);
  ids_write();
  if (schema != PERSIST_SCHEMA) persist_write_int(PKEY_SCHEMA, PERSIST_SCHEMA);
}

int store_slot_find_bundled(int bidx) {
  for (int s = 0; s < MAX_SLOTS; s++)
    if (g->slots[s].used && g->slots[s].bidx == bidx) return s;
  return -1;
}
static int slot_find_current(void) {
  return store_slot_find_bundled(g->cur_bidx);
}

// Move slot s to the front, carrying its fill value with it (fills are keyed by
// slot position, so a reorder swaps persist values too).
static void slot_move_front(int s) {
  if (s <= 0) return;
  Slot tmp = g->slots[s];
  uint8_t fill[MAX_CELLS + 4];
  int have = persist_exists(PKEY_FILL + s) ? persist_read_data(PKEY_FILL + s, fill, sizeof(fill)) : 0;
  for (int i = s; i > 0; i--) {
    g->slots[i] = g->slots[i - 1];
    uint8_t f2[MAX_CELLS + 4];
    int n2 = persist_exists(PKEY_FILL + i - 1) ? persist_read_data(PKEY_FILL + i - 1, f2, sizeof(f2)) : 0;
    if (n2 > 0) persist_write_data(PKEY_FILL + i, f2, n2); else persist_delete(PKEY_FILL + i);
  }
  g->slots[0] = tmp;
  if (have > 0) persist_write_data(PKEY_FILL, fill, have); else persist_delete(PKEY_FILL);
}

// Ensure the current puzzle owns slot 0. A full table evicts the oldest.
int store_slot_touch_current(void) {
  int s = slot_find_current();
  if (s < 0) {
    s = -1;
    for (int i = 0; i < MAX_SLOTS; i++) if (!g->slots[i].used) { s = i; break; }
    if (s < 0) {
      s = MAX_SLOTS - 1;
      persist_delete(PKEY_FILL + s);
      play_banner("Oldest puzzle dropped", GColorLightGray, GColorBlack);
    }
    Slot *sl = &g->slots[s];
    sl->used = true;
    sl->bidx = g->cur_bidx;
    sl->tier = g->cur_tier;
    int nc = g->rows * g->cols, white = 0;
    for (int k = 0; k < nc; k++) if (g->solution[k]) white++;
    sl->total = white > 255 ? 255 : white;
  }
  slot_move_front(s);
  slots_save();
  return 0;
}

// Persist the current fill + elapsed. Skips a solved puzzle (its slot is gone)
// and an all-empty grid that has no slot yet (an untouched puzzle stays "new").
void store_save_progress(void) {
  if (g->solved || g->loading || !g->pbuf) return;
  int nc = g->rows * g->cols;
  if (nc <= 0 || nc > MAX_CELLS) return;
  bool any = false;
  for (int k = 0; k < nc; k++) if (g->fill[k]) { any = true; break; }
  if (!any && slot_find_current() < 0) return;
  int s = store_slot_touch_current();
  uint8_t buf[MAX_CELLS + 4];
  memcpy(buf, g->fill, nc);
  // Low 24 bits seconds, high byte the current entry + 1 (see the header note).
  uint32_t entry = (g->cur_entry >= 0 && g->cur_entry < g->nentries) ? (uint32_t)g->cur_entry + 1 : 0;
  uint32_t packed = (g->elapsed & ELAPSED_MASK) | (entry << ENTRY_SHIFT);
  buf[nc] = packed & 0xFF;
  buf[nc + 1] = (packed >> 8) & 0xFF;
  buf[nc + 2] = (packed >> 16) & 0xFF;
  buf[nc + 3] = (packed >> 24) & 0xFF;
  int wrote = persist_write_data(PKEY_FILL + s, buf, nc + 4);
  if (wrote != nc + 4) play_banner("Save failed", GColorMelon, GColorBlack);
  else g->dirty = false;
}

bool store_load_progress(int slot, char *fill, int ncells, uint32_t *elapsed, int *entry) {
  *elapsed = 0;
  if (entry) *entry = -1;   // -1 = not recorded; resume falls back to the first gap
  if (slot < 0 || !persist_exists(PKEY_FILL + slot)) return false;
  uint8_t buf[MAX_CELLS + 4];
  int n = persist_read_data(PKEY_FILL + slot, buf, sizeof(buf));
  if (n < ncells) return false;
  memcpy(fill, buf, ncells);
  if (n >= ncells + 4) {
    uint32_t raw = (uint32_t)buf[ncells] | ((uint32_t)buf[ncells + 1] << 8) |
                   ((uint32_t)buf[ncells + 2] << 16) | ((uint32_t)buf[ncells + 3] << 24);
    *elapsed = raw & ELAPSED_MASK;
    int hi = (raw >> ENTRY_SHIFT) & 0xFF;
    if (entry && hi) *entry = hi - 1;
  }
  return true;
}

void store_drop_slot(int slot) {
  if (slot < 0 || slot >= MAX_SLOTS) return;
  // Close the gap so the directory stays MRU-ordered and fills stay aligned.
  for (int i = slot; i < MAX_SLOTS - 1; i++) {
    g->slots[i] = g->slots[i + 1];
    uint8_t f2[MAX_CELLS + 4];
    int n2 = persist_exists(PKEY_FILL + i + 1) ? persist_read_data(PKEY_FILL + i + 1, f2, sizeof(f2)) : 0;
    if (n2 > 0) persist_write_data(PKEY_FILL + i, f2, n2); else persist_delete(PKEY_FILL + i);
  }
  memset(&g->slots[MAX_SLOTS - 1], 0, sizeof(Slot));
  persist_delete(PKEY_FILL + MAX_SLOTS - 1);
  slots_save();
}

// Copy a slot's saved fill (without the trailing elapsed bytes) and its
// elapsed seconds; returns cells.
int store_slot_fill(int slot, char *buf, int cap, uint32_t *elapsed) {
  *elapsed = 0;
  if (slot < 0 || !persist_exists(PKEY_FILL + slot)) return 0;
  uint8_t tmp[MAX_CELLS + 4];
  int n = persist_read_data(PKEY_FILL + slot, tmp, sizeof(tmp));
  if (n <= 0) return 0;
  int cells = n >= 4 ? n - 4 : n;
  if (n >= 4)
    *elapsed = ((uint32_t)tmp[cells] | ((uint32_t)tmp[cells + 1] << 8) |
               ((uint32_t)tmp[cells + 2] << 16) | ((uint32_t)tmp[cells + 3] << 24)) & ELAPSED_MASK;
  if (cells > cap) cells = cap;
  memcpy(buf, tmp, cells);
  return cells;
}

void store_slot_title(int slot, char *buf, int cap) {
  puzzle_title_of(g->slots[slot].bidx, buf, cap);
}

// The current puzzle was solved: forget its slot and mark it done.
void store_mark_solved_current(void) {
  int s = slot_find_current();
  if (s >= 0) store_drop_slot(s);
  puzzle_set_bundled_done(g->cur_bidx, true);
  done_save();
}
