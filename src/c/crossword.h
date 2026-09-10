/*
 * Crossword: an offline, voice-first crossword solver for the Pebble Time 2.
 *
 * Shared state and the contracts between the modules:
 *
 *   puzzle.c  the bundled flash library + the puzzle record parser (owner tables,
 *             entry helpers, per-puzzle ids). Reads one puzzle at a time out of
 *             flash; the whole blob is never resident (emery's app heap is only
 *             ~128 KB).
 *   store.c   persist: settings, the solved bitmap, the progress slots (fill +
 *             timer) for puzzles in progress, and the id table that keeps both
 *             valid when the bundled set is regenerated.
 *   phone.c   AppMessage: the voice normalization round trip.
 *   scrub.c   the Type-mode letter scrubber (finger pixels to letter steps), a
 *             pure state machine shared with the host tests.
 *   play.c    the play window: rendering, buttons, touch, typing, checking.
 *   explain.c the one-screen explainer shown whenever the input mode changes.
 *   menu.c    the home cards and the other list screens.
 *   main.c    init / deinit.
 *
 * The watch shows ONE entry (word + clue) at a time; the whole grid is never
 * drawn. A shared per-cell fill[] (indexed row*cols+col) means a letter entered
 * in one entry is automatically shared with the crossing entry. Confirmed letters
 * are 'A'..'Z'; unconfirmed ("pencil") letters are 'a'..'z'. Wrong marks from a
 * check are a runtime-only bitmap, cleared per cell when that cell changes.
 */
#pragma once
#include <pebble.h>

// ---------------------------------------------------------------------------
// Limits
// ---------------------------------------------------------------------------
#define MAX_DIM     15     // rows and cols: a 16x16 fill plus its timer would not fit a 256-byte persist value
#define MAX_CELLS   256    // buffer size (a 15x15 uses 225)
#define MAX_ENTRIES 96
#define TITLE_LEN   24
#define TRANSCRIPT_LEN 200
#define MAX_SLOTS   8      // puzzles in progress remembered on the watch
#define REC_MAX     8192   // sanity cap on a bundled record (tools/xd2blob.py enforces the same)
#define NO_ENTRY    0xFF   // owner table: no entry runs through this cell

// Puzzle classes. A class follows from the grid size: minis are the 7x7s,
// regulars the 15x15s.
#define CLS_MINI    0
#define CLS_REGULAR 1
#define MINI_MAX_ROWS 7

// ---------------------------------------------------------------------------
// Settings (persisted as bytes; see store.c)
// ---------------------------------------------------------------------------
// The input byte is reserved: it held the v0.5 layout choice, then the Speak /
// Type mode. The controls are modeless now, so it is only kept so the persisted
// Settings layout does not shift. INPUT_VOICE is its default fill.
#define INPUT_VOICE 0
#define INPUT_TYPE  2
#define CHECK_EVERY_WORD 0 // near-miss snapping, pencil letters, check any time
#define CHECK_AT_END     1 // homophone-only correction, check only when complete
#define SCROLL_ADAPTIVE  0 // letter picker: distance per letter follows finger speed (SCRUB_MODE_ADAPTIVE)
#define SCROLL_FIXED     1 // every letter the same distance (SCRUB_MODE_FIXED)
#define HAPTICS_ON       0 // 0 so a saved record from before the setting reads as on
#define HAPTICS_OFF      1
#define SPEED_DEFAULT    0 // letter picker pace (SCRUB_SPEED_*): the tuned distances ...
#define SPEED_SLOW       1 // ... 1.4x the finger travel per letter
#define SPEED_FAST       2 // ... 0.7x
#define TYPEBUZZ_ON      0 // 0 so a saved record from before the setting reads as on
#define TYPEBUZZ_OFF     1
#define SKIP_OFF         0 // 0 default: advancing lands on the next square, filled or not
#define SKIP_ON          1 // skip filled squares to the next empty one

// Bytes are only ever appended: an older, shorter saved record is accepted and
// the new bytes read as 0, the default (see store_load).
typedef struct {
  uint8_t input;           // reserved (was the Speak/Type mode; controls are modeless now)
  uint8_t check;
  uint8_t tier;            // last difficulty picked (0 Peaceful, 1 Challenging)
  uint8_t scroll;          // SCROLL_*
  uint8_t haptics;         // HAPTICS_*
  uint8_t speed;           // SPEED_*
  uint8_t buzz_type;       // TYPEBUZZ_* (the letter-commit buzz in Type mode)
  uint8_t skip;            // SKIP_* (skip filled squares when advancing)
} Settings;

// ---------------------------------------------------------------------------
// Puzzle model
// ---------------------------------------------------------------------------
typedef struct {
  uint8_t dir;             // 0 = Across, 1 = Down
  uint8_t number;
  uint8_t row, col, len;
  uint8_t clue_len;
  uint16_t clue_off;       // offset of clue bytes within pbuf (not NUL-terminated)
} Entry;

// A puzzle in progress. bidx is its index in the current bundle; store_load
// remaps it through the id table when the bundle changes.
typedef struct {
  bool used;
  uint8_t bidx;
  uint8_t tier;
  uint8_t total;           // white squares (so the Continue list can show filled/total)
} Slot;

typedef struct {
  // Bundled library (flash resource, read on demand).
  ResHandle res;
  uint32_t res_size;
  uint16_t puzzle_count;
  uint32_t *offsets;       // [puzzle_count + 1]; last = res_size
  uint32_t *ids;           // [puzzle_count]: a hash of each puzzle's grid and tier (stable across repacks)
  // Clue-text dictionary (XWD3): token byte 0x80|i expands to dict entry i.
  uint8_t *dict;           // packed: per entry u8 len then bytes
  uint16_t dict_eoff[128]; // byte offset of entry i's bytes within g->dict
  uint8_t  dict_elen[128]; // entry i length
  int dict_n;
  uint8_t done[32];        // bitmap: bundled puzzle solved

  Settings set;
  Slot slots[MAX_SLOTS];   // slots[0] is the most recently played

  // Current puzzle (record bytes in pbuf; solution/clues point into it).
  uint8_t *pbuf;
  size_t pbuf_size;
  uint8_t cur_bidx;
  uint8_t cur_tier;
  uint8_t rows, cols;
  const uint8_t *solution; // rows*cols bytes within pbuf (0 = block)
  char title[TITLE_LEN];
  Entry entries[MAX_ENTRIES];
  uint8_t nentries;
  uint8_t owner[2][MAX_CELLS];  // [dir][cell] -> entry index or NO_ENTRY

  // Play state.
  char fill[MAX_CELLS];    // 0 = empty/block, 'A'..'Z' pen, 'a'..'z' pencil
  uint8_t wrong[MAX_CELLS / 8]; // bitmap: marked wrong by the last check
  int cur_entry;
  int focus_cell;          // 0..len-1 within cur_entry
  bool solved;
  uint32_t elapsed;        // seconds spent on this puzzle (persisted with the fill)
  time_t appear_at;        // when the play window last appeared (0 = not showing)
  bool dirty;              // fill changed since the last save

  // Transient banner (timed overlay).
  char banner[32];
  GColor banner_bg;
  GColor banner_fg;
  AppTimer *banner_timer;

  // Cached geometry for touch hit-testing (set on each render).
  GRect lay_band;          // the word block: letter rows plus any ledger rows
  int lay_x0, lay_y0;      // origin of the first letter row
  int lay_box;             // box size
  int lay_rows;            // 1, or 2 when the word wraps
  int lay_cols1;           // letters in the first row
  int lay_n;               // letters in the word

  // Touch tracking.
  int16_t td_x, td_y;
  bool touching;
  uint8_t td_zone;         // ZONE_*

  // Voice. seq tags each request so a late reply can't fill another entry.
  DictationSession *dictation;
  char transcript[TRANSCRIPT_LEN];
  int voice_seq;
  int voice_entry;

  // "More" menu action, run in did_close (clean window stack).
  int pending_action;

  // The play window's failure screen ("Bad puzzle", "No puzzle yet").
  bool loading;
  char loading_msg[40];
} AppState;

extern AppState *g;

// ---------------------------------------------------------------------------
// puzzle.c
// ---------------------------------------------------------------------------
bool puzzle_init_library(void);          // open the flash blob, read the offset table, hash the ids
uint32_t puzzle_bundle_signature(void);  // changes when the bundled set is regenerated
uint32_t puzzle_id_of(int i);            // stable id of bundled puzzle i (grid + tier hash)
bool puzzle_load_bundled(int i);         // load + parse bundled puzzle i into pbuf
void puzzle_title_of(int i, char *buf, int cap);
int  puzzle_cell(int r, int c);
bool puzzle_is_block(int r, int c);
void puzzle_entry_cell(const Entry *e, int k, int *r, int *c);
const Entry *puzzle_cur(void);
void puzzle_answer(const Entry *e, char *buf, int cap);
const char *puzzle_clue(const Entry *e, char *buf, int cap);
bool puzzle_complete(void);
int  puzzle_count_wrong(void);           // filled cells that differ from the solution
bool puzzle_bundled_done(int i);
void puzzle_set_bundled_done(int i, bool done);
int  puzzle_tier_of(int i);
int  puzzle_class_of_rows(int rows);
int  puzzle_pool_total(int cls, int tier);
int  puzzle_pool_solved(int cls, int tier);
int  puzzle_pick_bundled(int cls, int tier, bool *replay);   // random unplayed bundled puzzle, or -1

// ---------------------------------------------------------------------------
// store.c
// ---------------------------------------------------------------------------
bool store_onboard_seen(void);           // has the "how to play" screen been shown before?
void store_onboard_mark(void);           // remember it has, so first run shows it once
void store_load(void);                   // schema check, settings, done bitmap, slots, bundle remap
void store_save_settings(void);
int  store_slot_find_bundled(int bidx);
int  store_slot_touch_current(void);     // ensure a slot for the current puzzle, move to front
void store_save_progress(void);          // fill + elapsed for the current puzzle (if any fill)
bool store_load_progress(int slot, char *fill, int ncells, uint32_t *elapsed, int *entry);  // entry: last entry or -1
void store_drop_slot(int slot);          // forget a puzzle in progress
int  store_slot_fill(int slot, char *buf, int cap, uint32_t *elapsed);  // saved fill for a slot; returns cells
void store_slot_title(int slot, char *buf, int cap);
void store_mark_solved_current(void);

// ---------------------------------------------------------------------------
// phone.c
// ---------------------------------------------------------------------------
void phone_init(void);
void phone_send_normalize(void);

// ---------------------------------------------------------------------------
// play.c
// ---------------------------------------------------------------------------
void play_open_bundled(int i);
void play_open_loading(void);            // push the play window in its failure state
void play_load_failed(const char *msg);
void play_voice_result(const char *letters, int status, int conf, int seq);
void play_redraw(void);
void play_banner(const char *text, GColor bg, GColor fg);
void play_goto_entry(int idx);           // from the clue list
uint32_t play_elapsed(void);             // live elapsed seconds
void play_deinit(void);

// ---------------------------------------------------------------------------
// menu.c
// ---------------------------------------------------------------------------
void menu_push_home(void);
void menu_push_clues(void);              // the all-clues list for the current puzzle
void menu_deinit(void);
void explain_push(bool onboarding);      // the Controls cards: onboarding=true records them seen on exit (first install); false = review from Settings
