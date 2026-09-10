/*
 * Host tests for store.c: the progress slots, the solved bitmap and the id
 * table that carries both across a changed bundle. Persist is an in-memory
 * table here; puzzle.c is stubbed to whatever bundle the test declares.
 * Build and run: bash test/host/run.sh
 */
#include "crossword.h"

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

// ---------------------------------------------------------------------------
// In-memory persist: 256 bytes per value, like the watch.
// ---------------------------------------------------------------------------
#define NKEYS 256
static struct { bool used; int len; uint8_t data[256]; } P[NKEYS];
bool persist_exists(const uint32_t key) { return key < NKEYS && P[key].used; }
int persist_read_int(const uint32_t key) { int32_t v = 0; if (persist_exists(key)) memcpy(&v, P[key].data, 4); return v; }
int persist_read_data(const uint32_t key, void *buffer, const size_t buffer_size) {
  if (!persist_exists(key)) return -1;
  int n = P[key].len < (int)buffer_size ? P[key].len : (int)buffer_size;
  memcpy(buffer, P[key].data, n);
  return n;
}
int persist_write_int(const uint32_t key, const int32_t value) { return persist_write_data(key, &value, 4); }
int persist_write_data(const uint32_t key, const void *data, const size_t size) {
  if (key >= NKEYS) return -1;
  int n = size > 256 ? 256 : (int)size;   // the device truncates to 256 and reports that
  P[key].used = true; P[key].len = n; memcpy(P[key].data, data, n);
  return n;
}
int persist_delete(const uint32_t key) { if (key < NKEYS) P[key].used = false; return 0; }
static void persist_clear(void) { memset(P, 0, sizeof(P)); }

// ---------------------------------------------------------------------------
// The rest of the app, stubbed.
// ---------------------------------------------------------------------------
AppState *g;
static uint32_t s_sig = 1;
static char s_banner[32];
uint32_t puzzle_bundle_signature(void) { return s_sig; }
void puzzle_title_of(int i, char *buf, int cap) { snprintf(buf, cap, "P%d", i); }
void puzzle_set_bundled_done(int i, bool done) {
  if (done) g->done[i >> 3] |= (1 << (i & 7)); else g->done[i >> 3] &= ~(1 << (i & 7));
}
void play_banner(const char *text, GColor bg, GColor fg) { (void)bg; (void)fg; snprintf(s_banner, sizeof(s_banner), "%s", text); }

static bool done_bit(int i) { return (g->done[i >> 3] >> (i & 7)) & 1; }

// A bundle: ids in bundle order. The test rewrites this to simulate a release.
static uint32_t s_ids[255];
static void bundle(const uint32_t *ids, int n, uint32_t sig) {
  memcpy(s_ids, ids, n * sizeof(uint32_t));
  g->ids = s_ids;
  g->puzzle_count = n;
  s_sig = sig;
}
// A 7x7 all-white puzzle open as bundled index bidx, with a fill of one letter
// per square derived from its id (so a fill can be told from another's).
static uint8_t s_solution[49];
static void open_puzzle(int bidx) {
  memset(s_solution, 'A', sizeof(s_solution));
  g->rows = g->cols = 7;
  g->solution = s_solution;
  g->pbuf = s_solution;   // non-NULL is all store.c asks
  g->cur_bidx = bidx;
  g->cur_tier = 1;
  g->solved = false; g->loading = false;
  uint32_t id = g->ids[bidx];
  for (int k = 0; k < 49; k++) g->fill[k] = 'A' + (id + k) % 26;
  g->elapsed = 60 * (bidx + 1);
  g->dirty = true;
}
static bool fill_is_for(int slot, uint32_t id, uint32_t want_elapsed) {
  char buf[MAX_CELLS]; uint32_t el;
  int n = store_slot_fill(slot, buf, sizeof(buf), &el);
  if (n != 49 || el != want_elapsed) return false;
  for (int k = 0; k < 49; k++) if (buf[k] != (char)('A' + (id + k) % 26)) return false;
  return true;
}
static void relaunch(void) {   // what init does on every launch
  memset(g->slots, 0, sizeof(g->slots));
  memset(g->done, 0, sizeof(g->done));
  store_load();
}

static const uint32_t IDS_A[] = { 0x1111, 0x2222, 0x3333, 0x4444, 0x5555, 0x6666 };

static void test_fresh_install(void) {
  persist_clear();
  bundle(IDS_A, 6, 100);
  relaunch();
  CHECK(persist_read_int(10) == 6, "fresh: schema 6 written (got %d)", persist_read_int(10));
  CHECK(persist_exists(20) && P[20].len == 24, "fresh: the id table is written (len %d)", P[20].len);
  CHECK((uint32_t)persist_read_int(14) == 100, "fresh: the bundle signature is written");
  CHECK(g->set.input == INPUT_VOICE && g->set.check == CHECK_EVERY_WORD, "fresh: default settings");
}

static void test_save_and_mru(void) {
  persist_clear();
  bundle(IDS_A, 6, 100);
  relaunch();
  open_puzzle(3);
  store_save_progress();
  CHECK(g->slots[0].used && g->slots[0].bidx == 3, "save: the puzzle takes slot 0");
  CHECK(!g->dirty, "save: clears dirty");
  CHECK(fill_is_for(0, 0x4444, 240), "save: slot 0 holds puzzle 3's fill and timer");
  open_puzzle(5);
  store_save_progress();
  CHECK(g->slots[0].bidx == 5 && g->slots[1].bidx == 3, "mru: the newer puzzle moves to the front");
  CHECK(fill_is_for(0, 0x6666, 360) && fill_is_for(1, 0x4444, 240), "mru: fills follow their slots");
  open_puzzle(3);
  g->fill[0] = 'z';
  store_save_progress();
  CHECK(g->slots[0].bidx == 3 && g->slots[1].bidx == 5, "mru: returning to an older puzzle brings it forward");
  CHECK(fill_is_for(1, 0x6666, 360), "mru: the other fill moved back with its slot");
  store_drop_slot(0);
  CHECK(g->slots[0].bidx == 5 && !g->slots[1].used, "drop: the gap closes");
  CHECK(fill_is_for(0, 0x6666, 360) && !persist_exists(101), "drop: the fill moves up and the tail is deleted");
  // Progress survives a relaunch against the same bundle.
  relaunch();
  CHECK(g->slots[0].used && g->slots[0].bidx == 5 && fill_is_for(0, 0x6666, 360), "relaunch: same bundle keeps the slot");
}

static void test_solved_marks(void) {
  persist_clear();
  bundle(IDS_A, 6, 100);
  relaunch();
  open_puzzle(2);
  store_save_progress();
  store_mark_solved_current();
  CHECK(done_bit(2) && !g->slots[0].used, "solved: marked done and the slot is gone");
  relaunch();
  CHECK(done_bit(2), "solved: the mark survives a relaunch");
}

// A release that inserts a puzzle at the front and appends one at the end:
// every old position shifts. Marks and slots must follow their puzzle.
static void test_remap_reorder(void) {
  persist_clear();
  bundle(IDS_A, 6, 100);
  relaunch();
  open_puzzle(1); store_save_progress();
  open_puzzle(4); store_save_progress();
  open_puzzle(0); store_save_progress(); store_mark_solved_current();
  open_puzzle(5); store_save_progress(); store_mark_solved_current();
  CHECK(g->slots[0].bidx == 4 && g->slots[1].bidx == 1, "setup: two in progress");
  static const uint32_t IDS_B[] = { 0x0AAA, 0x1111, 0x2222, 0x3333, 0x4444, 0x5555, 0x6666, 0x0BBB };
  bundle(IDS_B, 8, 200);
  relaunch();
  CHECK(g->slots[0].used && g->slots[0].bidx == 5, "remap: slot 0 followed 0x5555 to index 5 (got %d)", g->slots[0].bidx);
  CHECK(g->slots[1].used && g->slots[1].bidx == 2, "remap: slot 1 followed 0x2222 to index 2 (got %d)", g->slots[1].bidx);
  CHECK(fill_is_for(0, 0x5555, 300) && fill_is_for(1, 0x2222, 120), "remap: fills untouched");
  CHECK(done_bit(1) && done_bit(6) && !done_bit(0) && !done_bit(7), "remap: solved marks followed 0x1111 and 0x6666");
  CHECK((uint32_t)persist_read_int(14) == 200 && P[20].len == 32, "remap: new signature and id table saved");
  // And again, against the same bundle: nothing moves.
  relaunch();
  CHECK(g->slots[0].bidx == 5 && g->slots[1].bidx == 2 && done_bit(1) && done_bit(6), "remap: stable on the next launch");
}

// A release that removes a puzzle in progress and a solved one.
static void test_remap_removal(void) {
  persist_clear();
  bundle(IDS_A, 6, 100);
  relaunch();
  open_puzzle(1); store_save_progress();
  open_puzzle(4); store_save_progress();
  open_puzzle(2); store_save_progress(); store_mark_solved_current();
  open_puzzle(3); store_save_progress(); store_mark_solved_current();
  static const uint32_t IDS_C[] = { 0x1111, 0x2222, 0x4444, 0x6666 };   // 0x3333 and 0x5555 gone
  bundle(IDS_C, 4, 300);
  relaunch();
  CHECK(g->slots[0].used && g->slots[0].bidx == 1 && !g->slots[1].used, "removal: only 0x2222 stays in progress (slot 0 bidx %d, slot 1 used %d)", g->slots[0].bidx, g->slots[1].used);
  CHECK(fill_is_for(0, 0x2222, 120), "removal: its fill moved up into slot 0");
  CHECK(!persist_exists(101), "removal: the dropped fill's key is gone");
  CHECK(done_bit(2) && !done_bit(1) && !done_bit(3), "removal: 0x4444 stays solved, 0x3333's mark went with it");
}

// A watch upgrading from schema 5 (v0.25 and earlier): the 17-byte slot
// layout is converted, and with the blob unchanged everything is kept.
static void write_v5(int schema, uint32_t sig, bool with_phone_slot) {
  persist_clear();
  persist_write_int(10, schema);
  Settings s = { INPUT_TYPE, CHECK_AT_END, 0, SCROLL_FIXED, HAPTICS_OFF, SPEED_FAST, TYPEBUZZ_OFF, SKIP_ON };
  persist_write_data(11, &s, sizeof(s));
  uint8_t done[32] = { 0 }; done[0] = 1 << 4;   // index 4 solved
  persist_write_data(12, done, 32);
  uint8_t slots[8 * 17]; memset(slots, 0, sizeof(slots));
  slots[0] = 1; slots[1] = 0; slots[2] = 2; slots[3] = 1; slots[4] = 49;          // slot 0: bundled index 2
  if (with_phone_slot) { slots[17] = 1; slots[18] = 1; slots[19] = 0; memcpy(slots + 22, "d19500914", 10); }
  persist_write_data(13, slots, sizeof(slots));
  persist_write_int(14, (int32_t)sig);
  uint8_t fill[53]; for (int k = 0; k < 49; k++) fill[k] = 'A' + (0x3333 + k) % 26;
  fill[49] = 180; fill[50] = fill[51] = fill[52] = 0;
  persist_write_data(100, fill, sizeof(fill));
  if (with_phone_slot) persist_write_data(101, fill, sizeof(fill));
}
static void test_upgrade_from_v5(void) {
  write_v5(5, 100, true);
  bundle(IDS_A, 6, 100);
  relaunch();
  CHECK(g->set.input == INPUT_TYPE && g->set.check == CHECK_AT_END && g->set.skip == SKIP_ON, "v5: settings read through");
  CHECK(g->slots[0].used && g->slots[0].bidx == 2 && g->slots[0].total == 49, "v5: the bundled slot converts (bidx %d)", g->slots[0].bidx);
  CHECK(!g->slots[1].used, "v5: the phone slot reads as unused");
  CHECK(!persist_exists(101), "v5: the phone slot's fill is deleted");
  CHECK(fill_is_for(0, 0x3333, 180), "v5: its fill is intact");
  CHECK(done_bit(4), "v5: the solved mark is kept");
  CHECK(persist_read_int(10) == 6 && P[13].len == (int)sizeof(g->slots), "v5: schema 6 and the new slot layout are written");
  CHECK(persist_exists(20), "v5: the id table is written for next time");
  // The next release then reorders: the converted slot follows its id.
  static const uint32_t IDS_D[] = { 0x6666, 0x5555, 0x4444, 0x3333, 0x2222, 0x1111 };
  bundle(IDS_D, 6, 400);
  relaunch();
  CHECK(g->slots[0].used && g->slots[0].bidx == 3 && fill_is_for(0, 0x3333, 180), "v5 then remap: the slot followed 0x3333 (bidx %d)", g->slots[0].bidx);
  CHECK(done_bit(1) && !done_bit(4), "v5 then remap: the mark followed 0x5555");
}
// Upgrading from schema 5 when the blob also changed: no id table to follow it
// with, so the bundled progress is dropped (as every release before did).
static void test_upgrade_from_v5_new_blob(void) {
  write_v5(5, 100, false);
  bundle(IDS_A, 6, 999);
  relaunch();
  CHECK(!g->slots[0].used && !persist_exists(100), "v5 + new blob: slots dropped");
  CHECK(!done_bit(4), "v5 + new blob: marks dropped");
  CHECK(persist_read_int(10) == 6 && persist_exists(20), "v5 + new blob: schema 6 and the id table written");
}
static void test_unknown_schema_wipes(void) {
  write_v5(3, 100, false);
  bundle(IDS_A, 6, 100);
  relaunch();
  CHECK(!g->slots[0].used && !persist_exists(100) && g->set.input == INPUT_VOICE, "schema 3: everything wiped");
  CHECK(persist_read_int(10) == 6, "schema 3: schema 6 written");
}
// A slot pointing past the bundle (a shorter blob with the same signature can
// only happen by hand, but the index must never be played).
static void test_slot_past_bundle(void) {
  persist_clear();
  bundle(IDS_A, 6, 100);
  relaunch();
  open_puzzle(5); store_save_progress();
  bundle(IDS_A, 4, 100);   // same signature, fewer puzzles: the id table still says where 0x6666 was
  relaunch();
  CHECK(!g->slots[0].used, "past bundle: the slot is dropped");
}
// What play_open_bundled reads back.
static void test_load_progress(void) {
  persist_clear();
  bundle(IDS_A, 6, 100);
  relaunch();
  open_puzzle(2);
  g->nentries = 20; g->cur_entry = 7;   // the solver is on entry 7
  store_save_progress();
  char fill[MAX_CELLS]; uint32_t el = 7; int entry = 55;
  CHECK(store_load_progress(store_slot_find_bundled(2), fill, 49, &el, &entry), "load: the slot's fill reads back");
  CHECK(el == 180 && fill[0] == (char)('A' + 0x3333 % 26), "load: elapsed and letters intact");
  CHECK(entry == 7, "load: the saved entry reads back (got %d)", entry);
  el = 7; entry = 55;
  CHECK(!store_load_progress(-1, fill, 49, &el, &entry) && el == 0 && entry == -1, "load: no slot reads as nothing");
  CHECK(!store_load_progress(store_slot_find_bundled(2), fill, 225, &el, NULL), "load: a fill shorter than the grid is refused");
  // The fill counter in store_slot_fill must ignore the packed entry byte.
  CHECK(fill_is_for(store_slot_find_bundled(2), 0x3333, 180), "load: store_slot_fill still reports 49 cells and the masked time");
  // A record written before the entry was packed (high byte 0) resumes as unknown.
  g->cur_entry = 0; g->nentries = 0;   // (g->cur_entry < g->nentries is false -> entry byte 0)
  open_puzzle(2); g->nentries = 0; store_save_progress();
  entry = 55;
  store_load_progress(store_slot_find_bundled(2), fill, 49, &el, &entry);
  CHECK(entry == -1, "load: an unrecorded entry reads back as -1 (got %d)", entry);
}

// A bundle bigger than one id chunk: the table spans keys 20 and 21, and a
// remap still finds a puzzle past the first chunk.
static void test_ids_span_chunks(void) {
  persist_clear();
  uint32_t big[100];
  for (int i = 0; i < 100; i++) big[i] = 0x1000 + i;
  bundle(big, 100, 100);
  relaunch();
  CHECK(P[20].len == 256 && P[21].len == 36 * 4 && !persist_exists(22), "chunks: 100 ids span two keys (%d, %d)", P[20].len, P[21].len);
  open_puzzle(90); store_save_progress();
  open_puzzle(3); store_save_progress(); store_mark_solved_current();
  uint32_t big2[101];
  big2[0] = 0x9999;
  for (int i = 0; i < 100; i++) big2[i + 1] = 0x1000 + i;
  bundle(big2, 101, 200);
  relaunch();
  CHECK(g->slots[0].used && g->slots[0].bidx == 91, "chunks: a slot past the first chunk followed its id (got %d)", g->slots[0].bidx);
  CHECK(done_bit(4) && !done_bit(3), "chunks: the mark followed too");
  CHECK(P[21].len == 37 * 4, "chunks: the second chunk grew");
  bundle(IDS_A, 6, 300);
  relaunch();
  CHECK(!persist_exists(21), "chunks: shrinking the bundle deletes the spare chunk");
}

static void test_full_table_evicts_oldest(void) {
  persist_clear();
  bundle(IDS_A, 6, 100);
  relaunch();
  static const uint32_t MANY[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };
  bundle(MANY, 9, 100);
  for (int i = 0; i < 9; i++) { open_puzzle(i); store_save_progress(); }
  CHECK(g->slots[0].bidx == 8 && g->slots[7].bidx == 1, "evict: the oldest (0) is gone, 1..8 remain");
  CHECK(fill_is_for(7, 2, 120), "evict: slot 7 holds puzzle 1's fill");
  CHECK(strcmp(s_banner, "Oldest puzzle dropped") == 0, "evict: the banner says so");
}

int main(void) {
  g = calloc(1, sizeof(AppState));
  test_fresh_install();
  test_save_and_mru();
  test_solved_marks();
  test_remap_reorder();
  test_remap_removal();
  test_upgrade_from_v5();
  test_upgrade_from_v5_new_blob();
  test_unknown_schema_wipes();
  test_slot_past_bundle();
  test_load_progress();
  test_ids_span_chunks();
  test_full_table_evicts_oldest();
  if (fails) { printf("test_store: %d failed\n", fails); return 1; }
  printf("test_store: OK\n");
  return 0;
}
