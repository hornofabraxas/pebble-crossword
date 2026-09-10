/*
 * play.c: the play window. One entry at a time: the word in boxes and its clue,
 * with the words that cross it one gesture away.
 *
 * Input is modeless: every gesture means one thing, so there is no Speak / Type
 * mode to switch between.
 *   SELECT         dictate the current word (with no phone: a note, nothing else)
 *   drag L / R on the clue   scroll the focused letter through the alphabet
 *   UP / DOWN      previous / next entry with an empty square (hold to repeat)
 *   SELECT hold    menu: Clear, All clues, Reveal word, Abandon
 *   BACK           close the picker (undoing the scroll), else leave the puzzle
 *   tap CLEAR WORD a red button under the clue while the word has a wrong (red)
 *                  square: clears the letters that are still yours to clear
 *   tap a square   focus it (any square; typing into a locked one is blocked);
 *                  tap the square already focused = its crossing word, and back
 *   swipe up / down on the word or the clue   next / previous entry
 *   tap the header                            the all-clues list
 * A small grip (three dots) sits between the word and the clue, marking the clue
 *   strip as the place to drag a letter; the dot toward the drag lights.
 * Dragging left / right on the clue opens the PICKER: a big previous / current /
 *   next readout in place of the word, which stays up across as many swipes as it
 *   takes, with a bead riding the header's progress bar to mark where in the
 *   alphabet you are. Tap the readout = keep the letter and move to the next
 *   square; BACK closes the picker and puts back what the square held when it
 *   opened, so a scroll can be walked away from. With the picker closed, a tap on
 *   the clue = keep the letter and move on.
 *
 * Motion: a change of entry slides the body of the screen in (the header stays):
 * up from below for the next entry, down from above for the previous, so the
 * screen follows the swipe; the crossing word slides in from the right, the axis
 * clue-to-clue navigation leaves free. Touch is ignored while a slide runs. This
 * is a one-shot transition, unlike the per-step picker animation dropped in v0.13.
 *
 * Layout: one adaptive layout, solved on every redraw (see "Rendering" below).
 * Every word is drawn horizontally. The boxes take whatever room the clue leaves;
 * long answers wrap onto two rows; the neighbour squares of the crossing words
 * ("ledger" rows) appear when there is room for them. Nothing is drawn smaller
 * than Gothic 24 Bold.
 *
 * Haptics, one meaning per pattern: one short buzz = yes (a word checked right),
 * two short buzzes = no (a word checked wrong, the full-grid check finding errors,
 * a dictation not caught), a longer rising pattern = solved. Navigation and the
 * letter picker are silent: per-letter ticks and a bump animation were tried in
 * v0.12 and made the scroll jumpy, so the readout is the whole feedback.
 *
 * First run opens a one-time "how to play" screen (explain.c) naming the four
 * gestures; it can be reopened any time from Settings.
 *
 * Checking (Settings): in CHECK_EVERY_WORD every new letter is pencil (grey
 * square) and a word is checked the moment it is committed complete: right, its
 * letters lock (black on white), it flashes green and the next entry comes up;
 * wrong, it flashes red, the squares at fault turn red and the letters stay in
 * pencil. In CHECK_AT_END every letter is pen and the grid checks itself once it
 * is full, and UP / DOWN then tour the entries that still hold a wrong square. In
 * both modes the header counts the red squares until they are changed.
 */
#include "crossword.h"
#include "slide.h"
#include "scrub.h"

#define SWIPE_MIN 24
#define HEADER_H 30          // id + progress, bar at y 25..28
#define BANNER_MS 2000
#define FLASH_OK_MS 500      // a right word: white on green ...
#define HOLD_OK_MS 1400      // ... then as it will stay, locked, with the check, before the advance
#define FLASH_NO_MS 700      // a wrong word: white on red, no advance

#define ACT_NONE    0
#define ACT_CLEAR   1
#define ACT_ABANDON 2
#define ACT_CLUES   4
#define ACT_REVEAL  5

static Window *s_win;
static Layer *s_layer;           // the body: word, rail, clue, readout, banner
static Layer *s_hdr;             // the header, on its own layer so the body can slide under it
static GRect s_full;             // the body's resting frame
static SlideView s_sv;           // the body's page-turn slide (see slide.c)
static bool s_clear_shown;       // the CLEAR WORD button is drawn ...
static GRect s_clear_rect;       // ... here (a tap on it clears the word)
static ActionMenuLevel *s_more;
static AppTimer *s_adv_timer;    // the flash / hold timer
static bool s_flash;             // the current word is drawn in s_flash_col
static GColor s_flash_col;
static bool s_flash_advance;     // go to the next entry when the flash (and hold) ends
static bool s_holding;           // the flash is over, the locked word is on show before the advance
static bool s_show_cross;        // reserved: the inline crossing-clue peek, no longer triggered
static bool s_td_on_focus;       // the touch-down landed on the already-focused square (a second tap dives into its cross)
static bool s_scrub_armed;       // a drag on the clue has become a letter scroll
static Scrub s_scrub;            // the scrubber's state (scrub.c turns finger pixels into steps)
static int s_rail_dir;           // -1 / +1 while a finger drags the clue that way, else 0
static bool s_picker;            // the letter picker is open: the readout covers the word
static bool s_check_pending;     // CHECK_AT_END: the grid filled under the picker; check when it closes
static int s_pick_cell;          // the square the picker opened on ...
static char s_pick_prev;         // ... what it held then ...
static uint8_t s_pick_wrong[MAX_CELLS / 8];   // ... and the wrong marks then (BACK puts all back)
static GRect s_readout;          // the picker's letter box (tap = keep the letter, next square)
static GRect s_pad_rect;         // the picker's drag pad (drag = scroll, tap left / right = one letter back / on)
static void flash_cancel(void);
static void silent_check(void);
static bool grid_check_due(void);
static void deferred_grid_check(void);
static bool picker_close_keep(void);

static bool pencil_mode(void) { return g->set.check == CHECK_EVERY_WORD; }
static char as_pen(char ch) { return (ch >= 'a' && ch <= 'z') ? ch - 32 : ch; }
static bool is_pencil(char ch) { return ch >= 'a' && ch <= 'z'; }

static bool wrong_at(int cell) { return (g->wrong[cell >> 3] >> (cell & 7)) & 1; }
static void wrong_set(int cell, bool w) {
  if (w) g->wrong[cell >> 3] |= (1 << (cell & 7)); else g->wrong[cell >> 3] &= ~(1 << (cell & 7));
}

void play_redraw(void) {
  if (s_layer) layer_mark_dirty(s_layer);
  if (s_hdr) layer_mark_dirty(s_hdr);
}

// ---------------------------------------------------------------------------
// Slide: a change of entry brings the new body in over the white ground, which
// reads as a page turn. The smooth snapshot mechanics live in slide.c; here we
// just drive it. The body layer holds the blank ground while the snapshot carries
// the content, and the header (added above the snapshot layer in win_load) stays
// fixed, so the body slides under it. The slide length (SLIDE_DEFAULT_MS) spans
// several of the ~30fps frames; a shorter slide would read as a jump.
//
// Direction encodes meaning: sequential (prev/next) clues slide vertically, while
// a jump to a crossing or listed clue slides horizontally by its orientation, so
// Across enters from the left and Down from the right (see slide_dx / slide_in).
// ---------------------------------------------------------------------------
static void slide_in(int dx, int dy) {
  if (!s_layer || !s_win || window_stack_get_top_window() != s_win) return;   // not on show: no motion
  slide_go(&s_sv, dx, dy);
  if (s_hdr) layer_mark_dirty(s_hdr);   // the header is above the snapshot; redraw it for the new entry
}
// Horizontal offset for a clue arriving by a jump: Across from the left, Down from
// the right, so which way it comes in tells you the new clue's orientation.
static int slide_dx(const Entry *e) { return e->dir ? s_full.size.w : -s_full.size.w; }

// ---------------------------------------------------------------------------
// Banner (timed overlay)
// ---------------------------------------------------------------------------
static void banner_expire(void *d) {
  g->banner_timer = NULL;
  g->banner[0] = 0;
  play_redraw();
}
void play_banner(const char *text, GColor bg, GColor fg) {
  snprintf(g->banner, sizeof(g->banner), "%s", text);
  g->banner_bg = bg;
  g->banner_fg = fg;
  if (g->banner_timer) app_timer_cancel(g->banner_timer);
  g->banner_timer = app_timer_register(BANNER_MS, banner_expire, NULL);
  light_enable_interaction();
  play_redraw();
}

// ---------------------------------------------------------------------------
// Timer (elapsed seconds; accumulated while the window is showing)
// ---------------------------------------------------------------------------
static void clock_start(void) { if (!g->solved && !g->loading) g->appear_at = time(NULL); }
// Bank the running interval into g->elapsed. clock_fold keeps the clock running
// (used before a save so the persisted total is current); clock_stop banks and
// stops. Both run even after solving: the earlier "!g->solved" guard dropped the
// final interval, so a puzzle solved in one sitting showed 0:00.
static void clock_fold(void) {
  if (g->appear_at) {
    time_t now = time(NULL);
    if (now > g->appear_at) g->elapsed += (uint32_t)(now - g->appear_at);
    g->appear_at = now;
  }
}
static void clock_stop(void) {
  if (g->appear_at) {
    time_t now = time(NULL);
    if (now > g->appear_at) g->elapsed += (uint32_t)(now - g->appear_at);
  }
  g->appear_at = 0;
}

// ---------------------------------------------------------------------------
// Letters
//
// fill[] holds 'A'..'Z' (pen) or 'a'..'z' (pencil). In CHECK_EVERY_WORD pen
// means LOCKED: the letter belongs to a word that checked correct and cannot be
// changed or cleared; every new letter lands in pencil. In CHECK_AT_END every
// letter is pen and nothing is locked.
// ---------------------------------------------------------------------------
static void set_cell(int cell, char ch) {
  if (g->fill[cell] == ch) return;
  g->fill[cell] = ch;
  wrong_set(cell, false);
  g->dirty = true;
}
static bool locked(int cell) { return pencil_mode() && g->fill[cell] >= 'A' && g->fill[cell] <= 'Z'; }
static int entry_cell(const Entry *e, int k) {
  int r, c; puzzle_entry_cell(e, k, &r, &c);
  return puzzle_cell(r, c);
}
static int focus_cell_index(void) { return entry_cell(puzzle_cur(), g->focus_cell); }
static bool entry_has_empty(const Entry *e) {
  for (int k = 0; k < e->len; k++) if (!g->fill[entry_cell(e, k)]) return true;
  return false;
}
static bool entry_filled(const Entry *e) { return !entry_has_empty(e); }
static bool entry_has_pencil(const Entry *e) {
  for (int k = 0; k < e->len; k++) if (is_pencil(g->fill[entry_cell(e, k)])) return true;
  return false;
}
static bool entry_correct(const Entry *e) {
  for (int k = 0; k < e->len; k++) {
    int cell = entry_cell(e, k);
    if (!g->fill[cell] || as_pen(g->fill[cell]) != (char)g->solution[cell]) return false;
  }
  return true;
}
static bool entry_has_wrong(const Entry *e) {
  for (int k = 0; k < e->len; k++) if (wrong_at(entry_cell(e, k))) return true;
  return false;
}
static int first_empty(const Entry *e) {
  for (int k = 0; k < e->len; k++) if (!g->fill[entry_cell(e, k)]) return k;
  return 0;
}
static int first_wrong_in(const Entry *e) {
  for (int k = 0; k < e->len; k++) if (wrong_at(entry_cell(e, k))) return k;
  return 0;
}
// The first cell of an entry that still needs attention: an empty square, else
// one marked wrong, else an unlocked (pencil) square; 0 if the word is done.
static int first_todo(const Entry *e) {
  for (int k = 0; k < e->len; k++) if (!g->fill[entry_cell(e, k)]) return k;
  for (int k = 0; k < e->len; k++) if (wrong_at(entry_cell(e, k))) return k;
  for (int k = 0; k < e->len; k++) if (is_pencil(g->fill[entry_cell(e, k)])) return k;
  return 0;
}
static bool any_wrong(void) {
  for (unsigned i = 0; i < sizeof(g->wrong); i++) if (g->wrong[i]) return true;
  return false;
}

static void save_if_dirty(void) { if (g->dirty) { clock_fold(); store_save_progress(); } }

// The haptic vocabulary. Custom patterns throughout so nothing here matches a
// system notification's shape.
static void buzz(const uint32_t *seg, int n) {
  if (g->set.haptics == HAPTICS_OFF) return;
  vibes_enqueue_custom_pattern((VibePattern){ .durations = seg, .num_segments = n });
}
static void buzz_yes(void) {
  static const uint32_t seg[] = {50};
  buzz(seg, 1);
}
static void buzz_no(void) {
  static const uint32_t seg[] = {60, 80, 60};
  buzz(seg, 3);
}
static void buzz_solved(void) {
  static const uint32_t seg[] = {60, 70, 60, 70, 260};
  buzz(seg, 5);
}
// A short pulse the moment a typed letter is kept (Type mode), never while
// scrolling the picker. Gated by its own setting, and by the master Vibration
// setting through buzz().
static void buzz_letter(void) {
  if (g->set.buzz_type == TYPEBUZZ_OFF) return;
  static const uint32_t seg[] = {35};
  buzz(seg, 1);
}

static void finish_solved(void) {
  g->solved = true;
  g->touching = false;          // a gesture in flight ends here; no Liftoff will follow
  s_scrub_armed = false;
  s_picker = false;
  s_rail_dir = 0;
  s_show_cross = false;
  memset(g->wrong, 0, sizeof(g->wrong));
  clock_stop();
  store_mark_solved_current();
  buzz_solved();
  light_enable_interaction();
  play_redraw();
}

// Mark every wrong filled cell; returns the count.
static int mark_wrong(void) {
  int nc = g->rows * g->cols, n = 0;
  for (int k = 0; k < nc; k++) {
    char f = g->fill[k];
    bool w = f && g->solution[k] && as_pen(f) != (char)g->solution[k];
    wrong_set(k, w);
    if (w) n++;
  }
  return n;
}

// Where UP / DOWN and the clue swipes land. While a check has left wrong marks
// (CHECK_AT_END) they tour the entries that still hold one. In CHECK_EVERY_WORD
// a correct word locks (all pen) and a wrong or part-done word keeps a pencil
// square, so visiting every entry that still has an empty OR a pencil square
// reaches exactly the words left to fix, even once the grid is full. Otherwise
// they skip entries with no empty square.
static bool entry_wants_visit(const Entry *e, bool skip_full) {
  if (!pencil_mode() && any_wrong()) return entry_has_wrong(e);
  if (pencil_mode()) return !skip_full || entry_has_empty(e) || entry_has_pencil(e);
  return !skip_full || entry_has_empty(e);
}

// Move to the first entry (searching forward from the current one) that holds a
// wrong-marked cell, focusing that cell.
static void goto_first_wrong(void) {
  flash_cancel();
  int n = g->nentries;
  for (int step = 0; step < n; step++) {
    int ei = (g->cur_entry + step) % n;
    const Entry *e = &g->entries[ei];
    if (entry_has_wrong(e)) { g->cur_entry = ei; g->focus_cell = first_wrong_in(e); return; }
  }
}

// CHECK_AT_END: the grid just became complete. Solved, or the wrong squares are
// marked; the count stays in the header until they are fixed.
static void check_complete(void) {
  int wrong = mark_wrong();
  if (wrong == 0) { finish_solved(); return; }
  buzz_no();
  goto_first_wrong();
}
// A full CHECK_AT_END grid arriving from storage is graded quietly on load (no
// buzz): the red marks are not persisted, and a grid completed under the picker
// just as the app was torn down was saved before its deferred check could run.
static void grade_loaded(void) {
  if (!grid_check_due()) return;
  if (mark_wrong() == 0) { g->solved = true; store_mark_solved_current(); memset(g->wrong, 0, sizeof(g->wrong)); }
  else goto_first_wrong();
}

// After a letter changed. In CHECK_AT_END a full grid checks itself (and again
// after every edit while it stays full); in CHECK_EVERY_WORD words are checked
// on commit (word_check), not here.
static void after_edit(void) {
  if (grid_check_due()) {
    if (s_picker) s_check_pending = true;   // graded once the letter is kept (see picker_open)
    else deferred_grid_check();
  }
  play_redraw();
}

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------
static void go_entry(int delta, bool skip_full) {
  int n = g->nentries;
  if (n <= 0) return;
  s_show_cross = false;
  flash_cancel();
  if (picker_close_keep()) return;
  silent_check();
  if (g->solved) return;
  save_if_dirty();
  int start = g->cur_entry;
  int ei = start;
  for (int step = 0; step < n; step++) {
    ei = ((ei + delta) % n + n) % n;
    if (entry_wants_visit(&g->entries[ei], skip_full)) break;
  }
  if (!entry_wants_visit(&g->entries[ei], skip_full)) ei = ((start + delta) % n + n) % n;
  g->cur_entry = ei;
  g->focus_cell = (!pencil_mode() && any_wrong()) ? first_wrong_in(puzzle_cur())
                : pencil_mode() ? first_todo(puzzle_cur())
                : first_empty(puzzle_cur());
  slide_in(0, delta > 0 ? s_full.size.h : -s_full.size.h);   // next up from below, previous down from above
  play_redraw();
}

// Into the word crossing the current one at the focused square (and back). It
// slides in from the right, the horizontal axis clue-to-clue navigation leaves
// free, so the two motions stay distinct.
static void jump_cross(void) {
  const Entry *e = puzzle_cur();
  int r, c; puzzle_entry_cell(e, g->focus_cell, &r, &c);
  int cell = puzzle_cell(r, c);
  int ci = g->owner[e->dir ? 0 : 1][cell];
  if (ci == NO_ENTRY) return;
  s_show_cross = false;
  if (picker_close_keep()) return;   // the letter is kept; a pending grid check decides the focus
  flash_cancel();
  silent_check();
  if (g->solved) return;
  save_if_dirty();
  const Entry *ce = &g->entries[ci];
  g->cur_entry = ci;
  g->focus_cell = ce->dir == 0 ? c - ce->col : r - ce->row;
  slide_in(slide_dx(ce), 0);   // enters by its orientation: Across from the left, Down from the right
  play_redraw();
}

// ---------------------------------------------------------------------------
// Flash: the current word drawn in one colour, then optionally on to the next
// entry. A right word shows white on green for FLASH_OK_MS and then as it will
// stay, locked, for HOLD_OK_MS more, so the finished word is seen at rest before
// it goes; a wrong word shows red for FLASH_NO_MS and stays. Cancelled by any
// navigation; flash_flush() skips straight to the advance.
// ---------------------------------------------------------------------------
static void flash_cancel(void) {
  if (s_adv_timer) { app_timer_cancel(s_adv_timer); s_adv_timer = NULL; }
  s_flash = false;
  s_holding = false;
}
static void advance_fire(void *d) {
  s_adv_timer = NULL;
  s_flash = false;
  if (g->solved || g->loading || !g->pbuf) return;
  if (s_flash_advance && !s_holding) {   // the flash is over: hold the locked word
    s_holding = true;
    s_adv_timer = app_timer_register(HOLD_OK_MS, advance_fire, NULL);
    play_redraw();
    return;
  }
  s_holding = false;
  if (s_flash_advance) go_entry(+1, true);
  else play_redraw();
}
static void flash_start(GColor col, bool advance, uint32_t ms) {
  flash_cancel();
  s_flash = true;
  s_flash_col = col;
  s_flash_advance = advance;
  light_enable_interaction();
  s_adv_timer = app_timer_register(ms, advance_fire, NULL);
  play_redraw();
}
// A pending advance happens now (before dictation starts on a word that is
// about to leave, or the reply would land on the next one).
static void flash_flush(void) {
  if (!s_adv_timer || !s_flash_advance) return;
  flash_cancel();
  go_entry(+1, true);
}

// ---------------------------------------------------------------------------
// Word check (CHECK_EVERY_WORD)
//
// A word is checked when it is committed complete: by a voice fill, or by a tap
// / SELECT while typing. Right: its letters lock, the word flashes green and the
// next entry comes up. Wrong: it flashes red and the letters stay in pencil.
// Every other word that has become complete and correct through crossings locks
// quietly at the same time, so a complete word still in pencil is always wrong.
// ---------------------------------------------------------------------------
static void lock_entry(const Entry *e) {
  for (int k = 0; k < e->len; k++) {
    int cell = entry_cell(e, k);
    if (is_pencil(g->fill[cell])) { g->fill[cell] = as_pen(g->fill[cell]); wrong_set(cell, false); g->dirty = true; }
  }
}
static void lock_sweep(void) {
  for (int i = 0; i < g->nentries; i++) {
    const Entry *e = &g->entries[i];
    if (entry_has_pencil(e) && entry_filled(e) && entry_correct(e)) lock_entry(e);
  }
}
static bool all_locked(void) {
  int nc = g->rows * g->cols;
  for (int k = 0; k < nc; k++) if (g->solution[k] && (!g->fill[k] || is_pencil(g->fill[k]))) return false;
  return true;
}
// Leaving a word by any route (UP / DOWN, a swipe, a crossing): lock whatever
// has become complete and correct, with no flash, and notice a finished grid.
// Without this a puzzle typed entirely with swipes would never lock or solve.
static void silent_check(void) {
  if (!pencil_mode() || g->solved || g->loading || !g->pbuf) return;
  lock_sweep();
  if (all_locked()) { save_if_dirty(); finish_solved(); }
}

// A loaded fill is only bytes: the checking mode may have changed since it was
// saved, and pen letters written under CHECK_AT_END were never checked. Under
// CHECK_EVERY_WORD every letter starts as pencil and only correct complete words
// lock; under CHECK_AT_END every letter is pen.
static void normalize_fill(void) {
  int nc = g->rows * g->cols;
  for (int k = 0; k < nc; k++) {
    char ch = g->fill[k];
    if (!ch) continue;
    if (as_pen(ch) < 'A' || as_pen(ch) > 'Z') { g->fill[k] = 0; g->dirty = true; continue; }   // not a letter: drop it
    char want = pencil_mode() ? (ch >= 'A' && ch <= 'Z' ? ch + 32 : ch) : as_pen(ch);
    if (want != ch) { g->fill[k] = want; g->dirty = true; }
  }
  if (pencil_mode()) lock_sweep();
}

static void word_check(void) {
  bool ok = entry_correct(puzzle_cur());
  lock_sweep();
  save_if_dirty();
  if (ok && all_locked()) { finish_solved(); return; }
  if (ok) { buzz_yes(); flash_start(GColorJaegerGreen, true, FLASH_OK_MS); return; }
  // Wrong: the squares at fault turn red (checked as a whole word, never as
  // letters are typed), and stay red until each is changed.
  const Entry *e = puzzle_cur();
  for (int k = 0; k < e->len; k++) {
    int cell = entry_cell(e, k);
    wrong_set(cell, as_pen(g->fill[cell]) != (char)g->solution[cell]);
  }
  buzz_no();
  flash_start(GColorRed, false, FLASH_NO_MS);
}

// ---------------------------------------------------------------------------
// Voice
// ---------------------------------------------------------------------------
void play_voice_result(const char *letters, int status, int conf, int seq) {
  (void)conf;   // the phone's confidence no longer matters: the word is checked here
  if (g->loading || g->solved || !g->pbuf) return;
  if (seq != g->voice_seq || g->cur_entry != g->voice_entry) return;   // stale
  if (status != 0 || !letters || !letters[0]) {
    play_banner("Not caught. Try again", GColorLightGray, GColorBlack);
    buzz_no();
    return;
  }
  const Entry *e = puzzle_cur();
  int k = 0;
  for (const char *p = letters; *p && k < e->len; p++) {
    char ch = as_pen(*p);
    if (ch < 'A' || ch > 'Z') continue;
    int cell = entry_cell(e, k);
    if (!locked(cell)) set_cell(cell, pencil_mode() ? ch + 32 : ch);
    k++;
  }
  if (k == 0) return;
  s_picker = false;   // a reply landing on an open picker closes it
  s_show_cross = false;
  g->focus_cell = k < e->len ? k : e->len - 1;
  if (pencil_mode()) {
    if (entry_filled(e)) word_check(); else { save_if_dirty(); play_redraw(); }
    return;
  }
  after_edit();
}

static void dictation_cb(DictationSession *s, DictationSessionStatus st, char *transcript, void *ctx) {
  if (st == DictationSessionStatusSuccess && transcript) {
    strncpy(g->transcript, transcript, sizeof(g->transcript) - 1);
    g->transcript[sizeof(g->transcript) - 1] = 0;
    phone_send_normalize();
  } else if (st != DictationSessionStatusFailureTranscriptionRejected) {
    bool phone = connection_service_peek_pebble_app_connection();
    play_banner(phone ? "Not caught. Try again" : "No phone. Typing", GColorLightGray, GColorBlack);
    if (phone) buzz_no();
  }
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------
// The controls are modeless: SELECT always dictates, a drag on the clue always
// scrolls a letter, so there is no Speak/Type mode to switch between.

// Clear the letters of this word that are still yours to clear: not locked, and
// not part of a crossing word that is already complete (unless marked wrong).
// From the menu, or the CLEAR WORD button while the word has a red square.
static void clear_word(void) {
  flash_cancel();
  if (picker_close_keep()) return;   // the check moved the focus; ask again to clear
  s_show_cross = false;
  const Entry *e = puzzle_cur();
  for (int k = 0; k < e->len; k++) {
    int cell = entry_cell(e, k);
    if (locked(cell)) continue;
    int ci = g->owner[e->dir ? 0 : 1][cell];
    if (!wrong_at(cell) && ci != NO_ENTRY && entry_filled(&g->entries[ci])) continue;   // a red square always goes
    set_cell(cell, 0);
  }
  g->focus_cell = first_empty(e);
  clock_fold();
  store_save_progress();
  play_redraw();
}

static void abandon(void) {
  int s = store_slot_find_bundled(g->cur_bidx);
  if (s >= 0) store_drop_slot(s);
  memset(g->fill, 0, sizeof(g->fill));
  g->dirty = false;
  window_stack_pop(true);
}

// Fill the current word with its answer and lock it (from the More menu, behind
// a confirm step). If it completes the grid, the puzzle is solved.
static void reveal_word(void) {
  if (g->solved || g->loading || !g->pbuf) return;
  const Entry *e = puzzle_cur();
  for (int k = 0; k < e->len; k++) {
    int cell = entry_cell(e, k);
    if (locked(cell)) continue;
    char sol = (char)g->solution[cell];
    if (sol < 'A' || sol > 'Z') continue;
    g->fill[cell] = sol;            // pen (uppercase); in CHECK_EVERY_WORD that is locked
    wrong_set(cell, false);
    g->dirty = true;
  }
  s_show_cross = false;
  if (pencil_mode()) {
    lock_sweep();
    if (all_locked()) { save_if_dirty(); finish_solved(); return; }
  } else if (grid_check_due()) {
    deferred_grid_check();          // a full, all-correct grid finishes here
    return;
  }
  save_if_dirty();
  play_redraw();
}

static void am_clear(ActionMenu *m, const ActionMenuItem *i, void *c)   { g->pending_action = ACT_CLEAR; }
static void am_abandon(ActionMenu *m, const ActionMenuItem *i, void *c) { g->pending_action = ACT_ABANDON; }
static void am_clues(ActionMenu *m, const ActionMenuItem *i, void *c)   { g->pending_action = ACT_CLUES; }
static void am_reveal(ActionMenu *m, const ActionMenuItem *i, void *c)  { g->pending_action = ACT_REVEAL; }

// Run the chosen action once the ActionMenu has popped (dictation or a window
// pop from inside the perform callback fails because the menu is still on top).
static void more_closed(ActionMenu *m, const ActionMenuItem *performed, void *ctx) {
  int act = g->pending_action;
  g->pending_action = ACT_NONE;
  switch (act) {
    case ACT_CLUES:   menu_push_clues(); break;
    case ACT_CLEAR:   clear_word(); break;
    case ACT_ABANDON: abandon(); break;
    case ACT_REVEAL:  reveal_word(); break;
    default: break;
  }
}

static void open_more(void) {
  flash_cancel();
  if (picker_close_keep() && g->solved) return;   // the last letter solved it: no menu over SOLVED
  s_show_cross = false;
  if (s_more) { action_menu_hierarchy_destroy(s_more, NULL, NULL); s_more = NULL; }
  s_more = action_menu_level_create(4);
  action_menu_level_add_action(s_more, "Clear this word", am_clear, NULL);
  action_menu_level_add_action(s_more, "All clues", am_clues, NULL);
  // Reveal word sits behind a one-item confirm level so it is not tapped by
  // accident; BACK from the confirm cancels. Freed with the hierarchy below.
  ActionMenuLevel *reveal = action_menu_level_create(1);
  action_menu_level_add_action(reveal, "Reveal the answer", am_reveal, NULL);
  action_menu_level_add_child(s_more, reveal, "Reveal word");
  action_menu_level_add_action(s_more, "Abandon puzzle", am_abandon, NULL);
  g->pending_action = ACT_NONE;
  ActionMenuConfig cfg = {
    .root_level = s_more,
    .colors = { .background = GColorJaegerGreen, .foreground = GColorWhite },
    .align = ActionMenuAlignCenter,
    .did_close = more_closed,
  };
  action_menu_open(&cfg);
}

// ---------------------------------------------------------------------------
// Typing
// ---------------------------------------------------------------------------
// The picker opens on the focused square and remembers what it held and the
// wrong marks of the moment, so BACK can put them back: a drag that lands on
// the wrong letter is undone in one press rather than cleared by hand. The
// undo covers everything since the picker opened, however many swipes.
static void picker_open(void) {
  if (s_picker) return;
  s_check_pending = false;
  s_pick_cell = focus_cell_index();
  s_pick_prev = g->fill[s_pick_cell];
  memcpy(s_pick_wrong, g->wrong, sizeof(s_pick_wrong));
  s_picker = true;
}
// BACK. A finger still down must not keep scrolling into the restored square.
static void picker_cancel(void) {
  s_picker = false;
  s_check_pending = false;
  s_scrub_armed = false;
  g->touching = false;
  s_rail_dir = 0;
  int cell = focus_cell_index();
  if (cell != s_pick_cell || locked(cell)) return;   // the focus moved on: nothing to undo here
  if (g->fill[cell] != s_pick_prev) {
    g->fill[cell] = s_pick_prev;
    g->dirty = true;
  }
  memcpy(g->wrong, s_pick_wrong, sizeof(g->wrong));
}
// In CHECK_AT_END the grid checks itself when it fills, but not while the
// picker is scrolling the last square (every letter tried would be graded, and
// the red would give the answer away). The check waits for the picker to close
// with the letter kept: a tap, SELECT, or a move to another entry (the flag
// survives leaving the screen, so the check is never lost, and never repeats).
static bool grid_check_due(void) { return !pencil_mode() && puzzle_complete(); }
static void deferred_grid_check(void) { s_check_pending = false; save_if_dirty(); check_complete(); play_redraw(); }
// A picker closing with the letter kept: true when the pending check ran.
static bool picker_close_keep(void) {
  s_picker = false;
  if (s_check_pending && grid_check_due()) { deferred_grid_check(); return true; }
  s_check_pending = false;
  return false;
}
static void cycle_letter(int dir) {
  int cell = focus_cell_index();
  if (locked(cell)) return;
  char cur = as_pen(g->fill[cell]), next;
  if (cur == 0) next = dir > 0 ? 'A' : 'Z';
  else { next = cur + dir; if (next > 'Z') next = 'A'; if (next < 'A') next = 'Z'; }
  set_cell(cell, pencil_mode() ? next + 32 : next);
  after_edit();
}

// The next square in direction dir that can still take a letter, wrapping
// within the word; -1 if every square is locked (or, with Skip filled squares
// on, already filled). Skip falls back to nothing only when no empty remains,
// which reads as at_end and hands over to the next entry.
static int next_open(const Entry *e, int from, int dir) {
  bool skip = g->set.skip == SKIP_ON;
  for (int step = 1; step <= e->len; step++) {
    int k = ((from + dir * step) % e->len + e->len) % e->len;
    int cell = entry_cell(e, k);
    if (locked(cell)) continue;
    if (skip && g->fill[cell]) continue;   // jump past filled squares to the next empty
    return k;
  }
  return -1;
}

// Keep the letter and move on. In CHECK_EVERY_WORD a complete word that is
// right locks at once, wherever the focus is; a complete word that is wrong is
// only called out (red) when the tap lands on its last open square, so fixing a
// letter in the middle does not nag on every step. In CHECK_AT_END a complete
// word hands over to the next entry from its last square. Otherwise the focus
// moves to the next square that can take a letter.
static void typing_commit(void) {
  // A tap while a checked word is still on show: a right word goes now (the tap
  // skips the wait), a wrong one just stops flashing. Neither is checked again.
  if (s_adv_timer) {
    if (s_flash_advance) { flash_flush(); return; }
    flash_cancel();
    play_redraw();
    return;
  }
  s_show_cross = false;
  if (picker_close_keep()) return;
  const Entry *e = puzzle_cur();
  int next = next_open(e, g->focus_cell, +1);
  bool at_end = next < 0 || next <= g->focus_cell;   // no open square after this one
  if (entry_filled(e)) {
    if (pencil_mode() && entry_has_pencil(e) && (at_end || entry_correct(e))) { word_check(); return; }
    if (!pencil_mode() && at_end) { go_entry(+1, true); return; }
    if (!entry_has_pencil(e) && pencil_mode()) { go_entry(+1, true); return; }   // all locked
  }
  if (g->fill[focus_cell_index()]) buzz_letter();   // a letter was kept: the confirmation pulse
  if (next >= 0) g->focus_cell = next;
  play_redraw();
}

// ---------------------------------------------------------------------------
// Buttons
// ---------------------------------------------------------------------------
static void up_click(ClickRecognizerRef r, void *ctx) {
  if (g->solved || g->loading) return;
  go_entry(-1, true);
}
static void down_click(ClickRecognizerRef r, void *ctx) {
  if (g->solved || g->loading) return;
  go_entry(+1, true);
}
// SELECT always dictates the current word. Typing is always available by
// dragging the clue, so a missing phone just means no voice: it does not change
// how anything else works.
static void select_click(ClickRecognizerRef r, void *ctx) {
  if (g->solved || g->loading) return;
  if (!connection_service_peek_pebble_app_connection() || !g->dictation) {
    play_banner("No phone. Drag to type", GColorLightGray, GColorBlack);
    return;
  }
  flash_flush();
  if (g->solved) return;
  dictation_session_start(g->dictation);
}
static void select_long(ClickRecognizerRef r, void *ctx) {
  if (g->solved || g->loading) return;
  open_more();
}
static void back_click(ClickRecognizerRef r, void *ctx) {
  if (s_picker) { picker_cancel(); play_redraw(); return; }   // close the picker, undo the scroll
  window_stack_pop(true);
}
#ifdef XW_SHOT
// Emulator review / screenshot harness. UP hold steps through a fixed set of
// scenes on whatever puzzle is open; DOWN hold does the same, so either button
// advances. Each scene stages one README screenshot deterministically.
static void shot_fill_all(void) {   // whole grid from the solution, as pencil
  int nc = g->rows * g->cols;
  for (int k = 0; k < nc; k++) { char s = (char)g->solution[k]; g->fill[k] = (s >= 'A' && s <= 'Z') ? s + 32 : 0; }
}
static int shot_find_len(int len, bool need_space) {
  for (int i = 0; i < g->nentries; i++) {
    const Entry *e = &g->entries[i];
    if (e->len != len) continue;
    if (need_space) { char cb[200]; puzzle_clue(e, cb, sizeof(cb)); if (!strchr(cb, ' ')) continue; }
    return i;
  }
  return -1;
}
static int shot_find_minlen(int len) {
  for (int i = 0; i < g->nentries; i++) if (g->entries[i].len >= len) return i;
  return -1;
}
static void shot_clear_current(void) {
  const Entry *e = puzzle_cur();
  for (int k = 0; k < e->len; k++) g->fill[entry_cell(e, k)] = 0;
}
static void shot_scene(int s) {
  s_picker = false; s_show_cross = false; s_holding = false; flash_cancel();
  if (s == 0) {                       // a 6-letter word, crossings/ledgers filled, the word itself blank
    shot_fill_all();
    int ei = shot_find_len(6, true); if (ei < 0) ei = shot_find_len(6, false);
    if (ei >= 0) { g->cur_entry = ei; shot_clear_current(); g->focus_cell = 0; }
  } else if (s == 1) {                // a wrapping 8+-letter word, filled
    shot_fill_all();
    int ei = shot_find_minlen(8); if (ei >= 0) { g->cur_entry = ei; g->focus_cell = 0; }
  } else {                            // the typing letter picker, mid-scroll
    shot_fill_all();
    int ei = shot_find_len(6, false); if (ei < 0) ei = 0;
    g->cur_entry = ei; shot_clear_current(); g->focus_cell = 2;
    int cell = focus_cell_index(); g->fill[cell] = 'm';   // a letter under the readout
    picker_open();
  }
  play_redraw();
}
static void shot_step(ClickRecognizerRef r, void *ctx) { static int s; shot_scene(s % 3); s++; }
#endif
static void click_config(void *ctx) {
#ifdef XW_SHOT
  window_long_click_subscribe(BUTTON_ID_UP, 0, shot_step, NULL);
  window_long_click_subscribe(BUTTON_ID_DOWN, 0, shot_step, NULL);
#endif
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 150, up_click);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 150, down_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
  window_long_click_subscribe(BUTTON_ID_SELECT, 0, select_long, NULL);
  window_single_click_subscribe(BUTTON_ID_BACK, back_click);
}

// ---------------------------------------------------------------------------
// Touch (one scheme for both modes; the geometry comes from the last render)
//
//   WORD  the boxes (a big target: the whole band). A tap (finger down and up in
//         place) focuses the square under it, applied on RELEASE so a swipe that
//         starts on a box never flickers the selection first. Tapping the square
//         already focused = its crossing word. Swipe up / down across it =
//         previous / next entry.
//   header  tap = the all-clues list.
//   CLUE  everything else. Swipe up / down = previous / next entry. A drag left /
//         right (past a dead zone) opens the PICKER (below); a tap on the clue =
//         keep the current letter and move to the next square.
//
// The picker is a fixed full-screen overlay, so the drag surface is a generous
// size no matter how long the clue is or how big the boxes are:
//   letter box  the letter, big, in a green box up top. A tap SUBMITS it (keeps
//         it and moves to the next square); it is the only thing that submits.
//   drag pad  the lower half. A drag scrolls the alphabet (30 to 6 px per step,
//         times the Scroll speed setting, as the finger speeds up); a tap on the
//         right steps one letter on, a tap on the left one back. Letting go of a
//         drag submits nothing. A bead on the header bar marks the alphabet spot.
//   BACK  closes the picker, putting back what the square held when it opened.
// ---------------------------------------------------------------------------
#define ZONE_CLUE 0
#define ZONE_WORD 1
#define ZONE_READOUT 3       // the picker's letter box: a tap submits the letter and moves on
#define ZONE_CLEAR 4         // the CLEAR WORD button: a tap clears, a drag from it does nothing
#define ZONE_PAD 5           // the picker's drag pad: a drag scrolls, a tap steps one letter (left back, right on)
#define SCRUB_ARM 14         // dead zone before a drag on the clue becomes a scroll (the pace knobs are in scrub.h)

static bool in_rect(GRect r, int x, int y) {
  return x >= r.origin.x && x < r.origin.x + r.size.w && y >= r.origin.y && y < r.origin.y + r.size.h;
}
// The square under a finger anywhere in the word band: the column comes from x,
// the row (for a wrapped word) from y. Above / below the letters lands on the
// nearest row, and the dash slot lands on the last letter of the first row.
static int cell_under(int x, int y) {
  if (g->lay_box <= 0 || g->lay_n <= 0) return g->focus_cell;
  int col = (x - g->lay_x0) / g->lay_box;
  if (x < g->lay_x0) col = 0;
  int row = 0;
  if (g->lay_rows == 2 && y >= g->lay_y0 + g->lay_box + 1) row = 1;
  int k;
  if (row == 0) { k = col; if (k > g->lay_cols1 - 1) k = g->lay_cols1 - 1; }
  else { int cols2 = g->lay_n - g->lay_cols1; k = g->lay_cols1 + (col > cols2 - 1 ? cols2 - 1 : col); }
  if (k < 0) k = 0;
  if (k >= g->lay_n) k = g->lay_n - 1;
  return k;
}
static void focus_under(int x, int y) {
  int k = cell_under(x, y);
  if (k == g->focus_cell) return;   // any square can be focused (to swipe into its crossing word);
  g->focus_cell = k;                // typing into a locked one is blocked separately, in the scrub path
  s_show_cross = false;   // the crossing clue was for the square just left
  play_redraw();
}
static uint32_t now_ms(void) {
  time_t sec; uint16_t ms;
  time_ms(&sec, &ms);
  return (uint32_t)sec * 1000u + ms;
}
// The scrubber (scrub.c) turns the drag's (x, t) samples into letter steps;
// this applies them, and lights the grip dot the finger is heading for.
static void scrub_letters(int x) {
  int steps = scrub_feed(&s_scrub, x, now_ms(), g->set.scroll == SCROLL_FIXED ? SCRUB_MODE_FIXED : SCRUB_MODE_ADAPTIVE,
                         g->set.speed);
  if (s_scrub.dir) s_rail_dir = s_scrub.dir;
  while (steps > 0) { cycle_letter(+1); steps--; }
  while (steps < 0) { cycle_letter(-1); steps++; }
}

static void touch_handler(const TouchEvent *ev, void *ctx) {
  if (ev->non_navigational || g->loading || g->solved || !g->pbuf) return;
  if (slide_active(&s_sv)) {   // no input while the body is in motion, but a lift-off still ends its gesture
    if (ev->type == TouchEvent_Liftoff) {
      g->touching = false;
      s_scrub_armed = false;
      if (s_rail_dir) { s_rail_dir = 0; play_redraw(); }
    }
    return;
  }
  GRect band = g->lay_band;
  GRect wide = GRect(band.origin.x - 10, band.origin.y - 10, band.size.w + 20, band.size.h + 20);

  if (ev->type == TouchEvent_Touchdown) {
    g->td_x = ev->x; g->td_y = ev->y; g->touching = true;
    scrub_reset(&s_scrub, ev->x, now_ms());
    s_scrub_armed = false;
    s_rail_dir = 0;
    if (s_clear_shown && in_rect(s_clear_rect, ev->x, ev->y)) g->td_zone = ZONE_CLEAR;
    else if (s_picker && in_rect(s_readout, ev->x, ev->y)) g->td_zone = ZONE_READOUT;   // the letter box: tap submits
    else if (s_picker) g->td_zone = ZONE_PAD;    // the rest of the overlay is the drag pad
    else if (in_rect(wide, ev->x, ev->y)) g->td_zone = ZONE_WORD;
    else g->td_zone = ZONE_CLUE;
    s_td_on_focus = false;
    if (g->td_zone == ZONE_WORD)   // focus is applied on release (below), not here, so a swipe off a box never selects it
      s_td_on_focus = (cell_under(ev->x, ev->y) == g->focus_cell);   // a tap on the focused square dives into its cross
    return;
  }
  if (!g->touching) return;
  int dx = ev->x - g->td_x, dy = ev->y - g->td_y;
  int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;

  if (ev->type == TouchEvent_PositionUpdate) {
    if (g->td_zone == ZONE_WORD) return;   // focus waits for release, so a swipe that begins on a box never moves it
    if (g->td_zone != ZONE_CLUE && g->td_zone != ZONE_PAD) return;   // the clue arms the picker; the pad keeps scrolling
    if (locked(focus_cell_index())) return;
    if (!s_scrub_armed) {
      if (adx >= SCRUB_ARM && adx > ady) {
        s_scrub_armed = true;
        picker_open();
        scrub_reset(&s_scrub, ev->x, now_ms());
        s_rail_dir = dx > 0 ? +1 : -1;
        cycle_letter(dx > 0 ? +1 : -1);   // the arming stroke is the first step
      }
      return;
    }
    scrub_letters(ev->x);
    return;
  }
  if (ev->type != TouchEvent_Liftoff) return;
  g->touching = false;
  bool scrubbed = s_scrub_armed;
  s_scrub_armed = false;
  if (s_rail_dir) { s_rail_dir = 0; play_redraw(); }

  if (g->td_zone == ZONE_CLEAR) {
    if (adx < 12 && ady < 12) clear_word();
    return;
  }
  if (g->td_zone == ZONE_READOUT) {
    if (adx < 12 && ady < 12) typing_commit();   // tap the letter box: submit, next square
    return;
  }
  if (g->td_zone == ZONE_PAD) {   // the picker's drag pad
    // A tap in the pad (not the header band above the letter box) steps a letter;
    // a drag has already scrolled, and lifting it submits nothing.
    if (!scrubbed && adx < 12 && ady < 12 && in_rect(s_pad_rect, g->td_x, g->td_y) && !locked(focus_cell_index())) {
      int cx = s_pad_rect.origin.x + s_pad_rect.size.w / 2;
      cycle_letter(g->td_x >= cx ? +1 : -1);   // right = on through the alphabet, left = back
      play_redraw();
    }
    return;
  }
  if (g->td_zone == ZONE_WORD) {
    if (ady > SWIPE_MIN && ady > adx) go_entry(dy < 0 ? +1 : -1, true);   // swipe = next / previous entry
    else if (adx < 12 && ady < 12) {   // a tap in place, resolved on release
      if (s_td_on_focus) jump_cross();          // tapped the focused square: into its crossing word
      else focus_under(ev->x, ev->y);           // tapped another square: focus it
    }
    return;
  }
  if (scrubbed) return;   // the picker stays open for the next swipe
  if (adx > SWIPE_MIN && adx > ady) {   // a flick too quick to arm: one letter step, picker open
    if (!locked(focus_cell_index())) { picker_open(); cycle_letter(dx > 0 ? +1 : -1); play_redraw(); }
    return;
  }
  if (ady > SWIPE_MIN && ady > adx) { go_entry(dy < 0 ? +1 : -1, true); return; }   // swipe = next / previous entry
  if (adx < 12 && ady < 12 && !s_picker) {   // taps beside an open picker do nothing
    if (g->td_y < HEADER_H) menu_push_clues();
    else typing_commit();
  }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------
// One layout, solved per redraw. The word is always drawn horizontally. The
// solver takes the largest box that still leaves room for the whole clue, in
// this priority order:
//   1. the boxes are as large as possible;
//   2. the clue is shown in full, in the largest of Gothic 36 / 28 / 24 Bold that
//      fits beside boxes of that size;
//   3. answers of WRAP_AT or more letters wrap onto two rows, a dash the width
//      of a letter ending the first row, instead of shrinking. The shorter half
//      goes first, so the dash's slot never makes the first row the wider one;
//   4. ledger rows (the neighbour squares of every crossing word, LEDGER_H tall
//      above and below a one-row word) rank above the clue font but below the
//      box size (v0.30): at the largest box, the clue font drops 36 -> 28 -> 24
//      to make room for ledgers before they are given up. Box size still wins,
//      so a short word with big boxes drops ledgers only when even 24 won't fit.
// The clue heights depend on the font alone, so they are measured once per
// font and the search itself is arithmetic. Gothic 24 Bold is the floor for
// every glyph on the screen in the normal run of play. Below that, in order of
// what is given up: the grip goes to keep the clue at 24; then the clue
// drops to 18 Bold; then whatever still does not fit clips at the bottom.
#define WORD_TOP  34         // top of the word block
#define BOX_MAX   56
#define BOX_MIN   24
#define WRAP_AT   8
#define ROW_GAP   2
#define GRIP_H    12         // the grip band between the word and the clue: the drag-to-type affordance
#define RAIL_GAP  4          // word-to-clue gap when the grip has given way to a long clue
#define READOUT_H 60         // the letter readout drawn over the word while scrolling
#define SIDE_PAD  2
#define LEDGER_H  26         // ledger squares: a Gothic 24 glyph, as wide as the box

typedef struct {
  int box, rows, cols1, slots;   // slots = boxes across the first row (with the dash)
  bool above, below;             // ledger rows drawn
  int gap;                       // word block to clue: GRIP_H with the grip drawn, RAIL_GAP without
  int ledger_h;                  // ledger square height (0 = none)
  GFont clue_font;
  int clue_h;                    // measured height of the clue in that font
  int x0, y0;                    // origin of the first letter row
} Layout;

// Clue fonts, largest first. Gothic 36 Bold ships in the Core Devices firmware
// but the SDK header does not name it; it is asked for by resource name and
// dropped if the firmware hands back its fallback (which is far shorter).
enum { CF_36, CF_28, CF_24, CF_18, N_CLUE_FONTS };
static GFont s_clue_fonts[N_CLUE_FONTS];
static void clue_fonts_init(void) {
  if (s_clue_fonts[2]) return;
  s_clue_fonts[CF_36] = fonts_get_system_font("RESOURCE_ID_GOTHIC_36_BOLD");
  s_clue_fonts[CF_28] = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  s_clue_fonts[CF_24] = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  s_clue_fonts[CF_18] = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);   // the last resort, below the grip
  GRect probe = GRect(0, 0, 200, 100);
  int h36 = s_clue_fonts[0] ? graphics_text_layout_get_content_size("Ag", s_clue_fonts[0], probe, GTextOverflowModeWordWrap, GTextAlignmentLeft).h : 0;
  int h28 = graphics_text_layout_get_content_size("Ag", s_clue_fonts[1], probe, GTextOverflowModeWordWrap, GTextAlignmentLeft).h;
  if (h36 <= h28) s_clue_fonts[0] = NULL;   // missing, or a fallback font: not the real 36
}

// Box font ladder. Bitham carries capitals and digits only, which is all a
// square holds. dy nudges the glyph towards the visual centre of its square.
typedef struct { int min_box; const char *key; int fonth; int dy; } BoxFont;
static const BoxFont BOX_FONTS[] = {
  { 48, FONT_KEY_BITHAM_42_BOLD,  42, -6 },
  { 34, FONT_KEY_BITHAM_30_BLACK, 30, -4 },
  { 28, FONT_KEY_GOTHIC_28_BOLD,  28, -4 },
  {  0, FONT_KEY_GOTHIC_24_BOLD,  24, -2 },
};
static const BoxFont *box_font(int box) {
  const BoxFont *f = BOX_FONTS;
  while (box < f->min_box) f++;
  return f;
}

static void draw_glyph(GContext *ctx, GRect cr, const BoxFont *bf, char ch, GColor col) {
  if (!ch) return;
  char s[2] = { as_pen(ch), 0 };
  graphics_context_set_text_color(ctx, col);
  graphics_draw_text(ctx, s, fonts_get_system_font(bf->key),
                     GRect(cr.origin.x, cr.origin.y + (cr.size.h - bf->fonth) / 2 + bf->dy, cr.size.w, bf->fonth + 6),
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

// One square of the current word, inside a 2 px border in the accent green (red
// for a wrong square, the flash colour while flashing) so the word being solved
// stands apart from the thin grey ledger tiles. Pen = black on white, pencil =
// dark grey on a grey square, wrong = white on red, focused = white on black,
// flash = white on green or red. While the locked word is held before the
// advance, no square is drawn focused: the word is shown exactly as it will stay.
static void draw_square(GContext *ctx, GRect cr, int cell, bool focused, const BoxFont *bf) {
  char ch = g->fill[cell];
  GColor fill = GColorWhite, col = GColorBlack, border = GColorJaegerGreen;
  bool filled = false;
  if (s_flash)              { fill = s_flash_col; col = GColorWhite; filled = true; border = s_flash_col; }
  else if (focused && !s_holding) { fill = GColorBlack; col = GColorWhite; filled = true; }
  else if (wrong_at(cell))  { fill = GColorRed; col = GColorWhite; filled = true; }
  else if (is_pencil(ch))   { fill = GColorLightGray; col = GColorDarkGray; filled = true; }
  if (!s_flash && wrong_at(cell)) border = GColorRed;
  if (filled) { graphics_context_set_fill_color(ctx, fill); graphics_fill_rect(ctx, cr, 3, GCornersAll); }
  graphics_context_set_stroke_color(ctx, border);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_round_rect(ctx, GRect(cr.origin.x + 1, cr.origin.y + 1, cr.size.w - 2, cr.size.h - 2), 3);
  graphics_context_set_stroke_width(ctx, 1);
  draw_glyph(ctx, cr, bf, ch, col);
}

// The wrap mark: a bold dash filling a letter-width slot at the end of row one,
// so a wrapped word can never be mistaken for two stacked words.
static void draw_dash(GContext *ctx, GRect slot) {
  int w = slot.size.w * 55 / 100, h = slot.size.h / 7;
  if (h < 3) h = 3;
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(slot.origin.x + (slot.size.w - w) / 2, slot.origin.y + (slot.size.h - h) / 2, w, h),
                     1, GCornersAll);
}

// Entry id, progress or the wrong count, and a progress bar beneath.
static void draw_header(GContext *ctx, GRect b, const Entry *e) {
  char idbuf[8];
  snprintf(idbuf, sizeof(idbuf), "%d%s", e->number, e->dir == 0 ? "A" : "D");
  GFont f = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, idbuf, f, GRect(5, -4, 80, 28), GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  int filled = 0, total = 0, nc = g->rows * g->cols;
  for (int k = 0; k < nc; k++) if (g->solution[k]) { total++; if (g->fill[k]) filled++; }
  int wrong = 0;
  for (int k = 0; k < nc; k++) if (wrong_at(k)) wrong++;
  char right[20];
  GColor rc = GColorJaegerGreen;
  if (wrong) { snprintf(right, sizeof(right), "%d wrong", wrong); rc = GColorRed; }
  else snprintf(right, sizeof(right), "%d/%d", filled, total);
  graphics_context_set_text_color(ctx, rc);
  graphics_draw_text(ctx, right, f, GRect(b.size.w - 115, -4, 110, 28), GTextOverflowModeFill, GTextAlignmentRight, NULL);
  graphics_context_set_fill_color(ctx, GColorLightGray);
  graphics_fill_rect(ctx, GRect(0, 25, b.size.w, 3), 0, GCornerNone);
  graphics_context_set_fill_color(ctx, GColorJaegerGreen);
  graphics_fill_rect(ctx, GRect(0, 25, total ? b.size.w * filled / total : 0, 3), 0, GCornerNone);
  // While the letter picker is open, a bead rides this bar to show where in the
  // alphabet the scroll sits (A at the left, Z at the right). It steps with the
  // letter; play_redraw marks this layer on every step.
  if (s_picker) {
    char cur = as_pen(g->fill[focus_cell_index()]);
    if (cur >= 'A' && cur <= 'Z') {
      graphics_context_set_fill_color(ctx, GColorBlack);
      graphics_fill_circle(ctx, GPoint(5 + (b.size.w - 10) * (cur - 'A') / 25, 24), 5);   // r 5 inside HEADER_H
    }
  }
}

// Does the crossing word through square k of e continue before (side 0) /
// after (side 1) it? Returns the crossing entry index or NO_ENTRY.
static int crossing_at(const Entry *e, int k, bool *before, bool *after) {
  int r, c; puzzle_entry_cell(e, k, &r, &c);
  int ci = g->owner[e->dir ? 0 : 1][puzzle_cell(r, c)];
  *before = *after = false;
  if (ci == NO_ENTRY) return NO_ENTRY;
  const Entry *ce = &g->entries[ci];
  int pos = ce->dir == 0 ? c - ce->col : r - ce->row;
  *before = pos > 0;
  *after = pos < ce->len - 1;
  return ci;
}

// A ledger row: for each square of the word, the neighbour square of the word
// crossing there (side 0 = the one before it, side 1 = the one after). Drawn
// recessively (thin grey border, grey letters); the mint tile marks the column
// of the focused square.
static void draw_ledger(GContext *ctx, const Entry *e, int side, int x0, int y, int box, int lh) {
  const BoxFont *lf = box_font(lh);
  for (int k = 0; k < e->len; k++) {
    bool before, after;
    int ci = crossing_at(e, k, &before, &after);
    if (ci == NO_ENTRY || !(side == 0 ? before : after)) continue;
    const Entry *ce = &g->entries[ci];
    int r, c; puzzle_entry_cell(e, k, &r, &c);
    int pos = ce->dir == 0 ? c - ce->col : r - ce->row;
    int nr, nc; puzzle_entry_cell(ce, side == 0 ? pos - 1 : pos + 1, &nr, &nc);
    int ncell = puzzle_cell(nr, nc);
    GRect cr = GRect(x0 + k * box, y, box, lh);
    char ch = g->fill[ncell];
    GColor col = GColorDarkGray;
    bool filled = false; GColor fill = GColorWhite;
    if (k == g->focus_cell && !s_holding) { fill = GColorMintGreen; filled = true; }
    else if (wrong_at(ncell)) { fill = GColorRed; col = GColorWhite; filled = true; }
    else if (is_pencil(ch))   { fill = GColorLightGray; col = GColorDarkGray; filled = true; }
    if (filled) { graphics_context_set_fill_color(ctx, fill); graphics_fill_rect(ctx, cr, 3, GCornersAll); }
    graphics_context_set_stroke_color(ctx, GColorLightGray);
    graphics_draw_round_rect(ctx, cr, 3);
    draw_glyph(ctx, cr, lf, ch, col);
  }
}

static int text_height(const char *s, GFont f, int w) {
  return graphics_text_layout_get_content_size(s, f, GRect(0, 0, w, 400), GTextOverflowModeWordWrap,
                                               GTextAlignmentCenter).h;
}

// The clue's height in each font, measured once per clue: solve() runs on every
// redraw, including each letter step of a drag, and the clue only changes with
// the entry, the focused crossing, or the clear button.
static void clue_heights(const char *clue, int w, int *ch_of) {
  static char s_clue[212]; static int s_w; static int s_h[N_CLUE_FONTS];
  if (w != s_w || strcmp(clue, s_clue)) {
    s_w = w; strncpy(s_clue, clue, sizeof(s_clue) - 1); s_clue[sizeof(s_clue) - 1] = 0;
    for (int i = 0; i < N_CLUE_FONTS; i++) s_h[i] = s_clue_fonts[i] ? text_height(clue, s_clue_fonts[i], w) : 0;
  }
  memcpy(ch_of, s_h, sizeof(s_h));
}

// Stages, each searched box-first then ledgers then font, and each entered
// only when the one before finds nothing: the grip with 36 / 28 / 24; the grip
// given up to keep 24; 18 as the last resort. The last candidate of
// the last stage is taken whether or not it fits: the clue gets what is left
// and clips at the bottom.
static const struct { int cf_lo, cf_hi, gap; } STAGES[] = {
  { CF_36, CF_24, GRIP_H }, { CF_24, CF_24, RAIL_GAP }, { CF_18, CF_18, RAIL_GAP },
};
#define N_STAGES (int)(sizeof(STAGES) / sizeof(STAGES[0]))

static void solve(int n, const char *clue, GRect b, bool any_above, bool any_below, Layout *L) {
  int W = b.size.w, avail = b.size.h;
  memset(L, 0, sizeof(*L));
  L->rows = n >= WRAP_AT ? 2 : 1;
  L->cols1 = L->rows == 2 ? n / 2 : n;
  L->slots = L->rows == 2 ? L->cols1 + 1 : n;
  clue_fonts_init();
  int ch_of[N_CLUE_FONTS];
  clue_heights(clue, W - 12, ch_of);
  bool cross = L->rows == 1 && (any_above || any_below);
  int box_max = (W - 2 * SIDE_PAD) / L->slots;
  if (box_max > BOX_MAX) box_max = BOX_MAX;
  if (box_max < BOX_MIN) box_max = BOX_MIN;   // wider than the screen (never, at MAX_ANS): drawn as is
  for (int st = 0; st < N_STAGES; st++) {
    for (int box = box_max; box >= BOX_MIN; box--) {
      for (int ledger = cross ? 1 : 0; ledger >= 0; ledger--) {
        int block = L->rows * box + (L->rows - 1) * ROW_GAP;
        if (ledger) block += (any_above ? LEDGER_H + ROW_GAP : 0) + (any_below ? LEDGER_H + ROW_GAP : 0);
        int room = avail - (WORD_TOP + block + STAGES[st].gap + 4);
        for (int cf = STAGES[st].cf_lo; cf <= STAGES[st].cf_hi; cf++) {
          if (!s_clue_fonts[cf]) continue;
          bool last = st == N_STAGES - 1 && box == BOX_MIN && ledger == 0 && cf == STAGES[st].cf_hi;
          if (ch_of[cf] > room && !last) continue;
          L->box = box;
          L->above = ledger && any_above;
          L->below = ledger && any_below;
          L->ledger_h = ledger ? LEDGER_H : 0;
          L->gap = STAGES[st].gap;
          L->clue_font = s_clue_fonts[cf];
          L->clue_h = ch_of[cf] > room ? room : ch_of[cf];
          goto placed;
        }
      }
    }
  }
placed:
  L->x0 = (W - L->slots * L->box) / 2;
  L->y0 = WORD_TOP + (L->above ? L->ledger_h + ROW_GAP : 0);
}

// The grip: a small, wordless affordance in the thin band between the word and
// the clue, marking the clue strip as the place to drag a letter. It is the
// only persistent cue for the modeless drag-to-type, so it always shows (in the
// GRIP_H stage); a first run explains the gesture. Three dots, LightGray at
// rest, DarkGray while a drag is live, and the dot toward the drag goes solid
// black so the strip reads as moving under the finger (direction is also in the
// A to Z bead on the header bar).
#define GRIP_DOT 2           // dot radius
#define GRIP_STEP 9          // spacing between the three dots
static void draw_grip(GContext *ctx, GRect b, int y) {
  int cx = b.size.w / 2;
  GColor base = s_picker ? GColorDarkGray : GColorLightGray;
  for (int i = -1; i <= 1; i++) {
    GColor c = base;
    if (s_rail_dir < 0 && i == -1) c = GColorBlack;
    if (s_rail_dir > 0 && i == 1) c = GColorBlack;
    graphics_context_set_fill_color(ctx, c);
    graphics_fill_circle(ctx, GPoint(cx + i * GRIP_STEP, y), GRIP_DOT);
  }
}

// While a right word is held before the advance: a green band with a check in
// the clue's place. The word stays on show above it.
static void draw_check(GContext *ctx, GRect b, int cy) {
  GRect band = GRect(12, cy + 2, b.size.w - 24, 64);
  if (band.origin.y + band.size.h > b.size.h - 2) band.origin.y = b.size.h - 2 - band.size.h;
  graphics_context_set_fill_color(ctx, GColorJaegerGreen);
  graphics_fill_rect(ctx, band, 8, GCornersAll);
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_context_set_stroke_width(ctx, 6);
  int cx = b.size.w / 2, y0 = band.origin.y;
  graphics_draw_line(ctx, GPoint(cx - 22, y0 + 32), GPoint(cx - 6, y0 + 48));
  graphics_draw_line(ctx, GPoint(cx - 6, y0 + 48), GPoint(cx + 24, y0 + 16));
  graphics_context_set_stroke_width(ctx, 1);
}

// The CLEAR WORD button: a red band across the bottom while the current word
// has a wrong square. Big enough to hit; the clue keeps the room above it.
#define CLEAR_H   44
#define CLEAR_GAP 6
static void draw_clear(GContext *ctx, GRect b) {
  s_clear_rect = GRect(6, b.size.h - CLEAR_H - 4, b.size.w - 12, CLEAR_H);
  graphics_context_set_fill_color(ctx, GColorRed);
  graphics_fill_rect(ctx, s_clear_rect, 8, GCornersAll);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, "CLEAR WORD", fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
                     GRect(s_clear_rect.origin.x, s_clear_rect.origin.y + 4, s_clear_rect.size.w, 32),
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

static void render(GContext *ctx, GRect b, const Entry *e) {
  int n = e->len; if (n < 1) n = 1;
  bool any_above = false, any_below = false;
  for (int k = 0; k < n; k++) {
    bool before, after;
    if (crossing_at(e, k, &before, &after) != NO_ENTRY) { any_above |= before; any_below |= after; }
  }
  // The clue slot: this word's clue, or on request the crossing word's, led by
  // its id and in green so it cannot pass for this word's.
  char clue[212];
  bool bfoc, afoc;
  int cross_ci = crossing_at(e, g->focus_cell, &bfoc, &afoc);
  bool showing_cross = s_show_cross && cross_ci != NO_ENTRY;
  if (showing_cross) {
    const Entry *ce = &g->entries[cross_ci];
    char ct[200];
    puzzle_clue(ce, ct, sizeof(ct));
    snprintf(clue, sizeof(clue), "%d%s: %s", ce->number, ce->dir == 0 ? "A" : "D", ct);
  } else {
    puzzle_clue(e, clue, sizeof(clue));
  }

  // While the word has a red square, a CLEAR WORD button takes the bottom of
  // the screen and the layout is solved in what is left above it. It steps aside
  // while the picker is open, so tapping a wrong square and dragging to fix a
  // single letter dismisses the button instead of nagging to clear the word.
  bool clear = !g->solved && !s_holding && !s_picker && entry_has_wrong(e);
  GRect lb = b;
  if (clear) lb.size.h -= CLEAR_H + CLEAR_GAP;
  Layout L;
  solve(n, clue, lb, any_above, any_below, &L);
  const BoxFont *bf = box_font(L.box);
  int block_bottom = L.y0 + L.rows * L.box + (L.rows - 1) * ROW_GAP + (L.below ? ROW_GAP + L.ledger_h : 0);

  g->lay_band = GRect(L.x0, WORD_TOP, L.slots * L.box, block_bottom - WORD_TOP);
  g->lay_x0 = L.x0; g->lay_y0 = L.y0;
  g->lay_box = L.box; g->lay_rows = L.rows; g->lay_cols1 = L.cols1; g->lay_n = n;

  if (L.above) draw_ledger(ctx, e, 0, L.x0, WORD_TOP, L.box, L.ledger_h);
  for (int k = 0; k < n; k++) {
    int r = k < L.cols1 ? 0 : 1, c = r == 0 ? k : k - L.cols1;
    int gr, gc; puzzle_entry_cell(e, k, &gr, &gc);
    GRect cr = GRect(L.x0 + c * L.box, L.y0 + r * (L.box + ROW_GAP), L.box, L.box);
    draw_square(ctx, cr, puzzle_cell(gr, gc), k == g->focus_cell, bf);
  }
  if (L.rows == 2) draw_dash(ctx, GRect(L.x0 + L.cols1 * L.box, L.y0, L.box, L.box));
  if (L.below) draw_ledger(ctx, e, 1, L.x0, L.y0 + L.box + ROW_GAP, L.box, L.ledger_h);

  int cy = block_bottom + L.gap;
  if (L.gap == GRIP_H) draw_grip(ctx, b, cy - GRIP_H / 2);
  s_clear_shown = clear;
  if (clear) draw_clear(ctx, b);
  if (s_holding) { draw_check(ctx, b, cy); return; }   // the clue has done its job
  graphics_context_set_text_color(ctx, showing_cross ? GColorDarkGreen : GColorBlack);
  graphics_draw_text(ctx, clue, L.clue_font, GRect(6, cy, b.size.w - 12, lb.size.h - cy - 4), GTextOverflowModeWordWrap,
                     GTextAlignmentCenter, NULL);
}

static void draw_banner(GContext *ctx, GRect b) {
  if (!g->banner[0]) return;
  GRect band = GRect(0, b.size.h - 32, b.size.w, 32);
  if (s_clear_shown) band.origin.y = s_clear_rect.origin.y - 32 - CLEAR_GAP;   // never over the button
  graphics_context_set_fill_color(ctx, g->banner_bg);
  graphics_fill_rect(ctx, band, 0, GCornerNone);
  graphics_context_set_text_color(ctx, g->banner_fg);
  graphics_draw_text(ctx, g->banner, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(4, band.origin.y - 1, b.size.w - 8, 30), GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentCenter, NULL);
}

// The letter picker: a fixed full-screen overlay while typing, so the drag
// surface stays a generous size no matter the clue length or box size. Top: the
// letter, big in a white box on a green band right under the progress bar, its
// two alphabet neighbours each side; a tap on the band is the only submit.
// Bottom: the drag pad, split down the middle; a drag scrolls the alphabet, a
// tap on the right steps one letter on, a tap on the left one back. The header
// bar carries the A-Z bead (draw_header), so the alphabet position shows there.
#define OV_BAND_TOP HEADER_H   // the letter band sits right under the progress bar / bead
#define OV_BAND_H   78         // the green letter band
static void draw_glyph_minus(GContext *ctx, int cx, int cy) {
  graphics_context_set_stroke_width(ctx, 5);
  graphics_draw_line(ctx, GPoint(cx - 16, cy), GPoint(cx + 16, cy));
  graphics_context_set_stroke_width(ctx, 1);
}
static void draw_glyph_plus(GContext *ctx, int cx, int cy) {
  graphics_context_set_stroke_width(ctx, 5);
  graphics_draw_line(ctx, GPoint(cx - 16, cy), GPoint(cx + 16, cy));
  graphics_draw_line(ctx, GPoint(cx, cy - 16), GPoint(cx, cy + 16));
  graphics_context_set_stroke_width(ctx, 1);
}
static void draw_picker_overlay(GContext *ctx, GRect b) {
  int cx = b.size.w / 2;
  char cur = as_pen(g->fill[focus_cell_index()]);

  // The letter band: green, full width, right under the progress bar. The letter
  // is big and black in a white box, with its two alphabet neighbours each side
  // in white. A tap anywhere on the band submits the letter and moves on.
  GRect band = GRect(0, OV_BAND_TOP, b.size.w, OV_BAND_H);
  s_readout = band;
  graphics_context_set_fill_color(ctx, GColorJaegerGreen);
  graphics_fill_rect(ctx, band, 0, GCornerNone);
  int cy = band.origin.y + band.size.h / 2;
  GRect box = GRect(cx - 27, cy - 27, 54, 54);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, box, 4, GCornersAll);
  char t[2] = { 0, 0 };
  if (cur >= 'A' && cur <= 'Z') {
    t[0] = cur;
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, t, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD),
                       GRect(box.origin.x, cy - 29, box.size.w, 50), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
    GFont f1 = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD), f2 = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
    graphics_context_set_text_color(ctx, GColorWhite);
    for (int i = 1; i <= 2; i++) {   // two neighbours each side, so the drift is easy to read
      GFont f = i == 1 ? f1 : f2;
      int h = i == 1 ? 34 : 30, w = 40;
      t[0] = 'A' + ((cur - 'A' - i) % 26 + 26) % 26;
      graphics_draw_text(ctx, t, f, GRect(cx - 27 - i * 36 - w / 2 + 18, cy - h / 2 - 2, w, h),
                         GTextOverflowModeFill, GTextAlignmentCenter, NULL);
      t[0] = 'A' + (cur - 'A' + i) % 26;
      graphics_draw_text(ctx, t, f, GRect(cx + 27 + i * 36 - w / 2 - 18, cy - h / 2 - 2, w, h),
                         GTextOverflowModeFill, GTextAlignmentCenter, NULL);
    }
  }

  // The drag pad below the band. A white strip carries "DRAG OR TAP" (the body
  // is already white there), then a grey surface split into a back (-) and an on
  // (+) half. A drag scrolls the alphabet; a tap on a half steps one letter. The
  // whole area below the band is touch-active, the white strip included.
  int pad_top = OV_BAND_TOP + OV_BAND_H;
  s_pad_rect = GRect(0, pad_top, b.size.w, b.size.h - pad_top);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, "SWIPE (OR TAP) BELOW", fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     GRect(0, pad_top + 4, b.size.w, 22), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  int btn_top = pad_top + 28;   // the grey buttons start below the white hint strip
  graphics_context_set_fill_color(ctx, GColorLightGray);
  graphics_fill_rect(ctx, GRect(0, btn_top, b.size.w, b.size.h - btn_top), 0, GCornerNone);
  int pcy = (btn_top + b.size.h) / 2;
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_context_set_stroke_width(ctx, 3);
  graphics_draw_line(ctx, GPoint(cx, btn_top + 6), GPoint(cx, b.size.h - 6));
  graphics_context_set_stroke_width(ctx, 1);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  draw_glyph_minus(ctx, cx / 2, pcy);
  draw_glyph_plus(ctx, cx + cx / 2, pcy);
}

// The solved screen: a full green page so finishing feels like an event, not a
// banner over the grid. SOLVED! and CONGRATULATIONS! up top, then the time
// taken. The header layer (below) also goes green, so the whole screen is one
// field of colour.
static void draw_solved(GContext *ctx, GRect b) {
  graphics_context_set_fill_color(ctx, GColorJaegerGreen);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  graphics_context_set_text_color(ctx, GColorWhite);
  clue_fonts_init();   // CF_36 if the firmware has it, else 28 for the hero line
  GFont hero = s_clue_fonts[CF_36] ? s_clue_fonts[CF_36] : fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  graphics_draw_text(ctx, "SOLVED!", hero,
                     GRect(0, 28, b.size.w, 46), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  graphics_draw_text(ctx, "CONGRATULATIONS!", fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     GRect(0, 84, b.size.w, 24), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  graphics_draw_text(ctx, "TIME TAKEN", fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(0, 138, b.size.w, 28), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  char t[16];
  uint32_t el = play_elapsed();
  snprintf(t, sizeof(t), "%lu:%02lu", (unsigned long)(el / 60), (unsigned long)(el % 60));
  graphics_draw_text(ctx, t, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
                     GRect(0, 168, b.size.w, 40), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

static void hdr_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  if (!g->loading && g->pbuf && g->solved) {   // part of the full-page celebration
    graphics_context_set_fill_color(ctx, GColorJaegerGreen);
    graphics_fill_rect(ctx, b, 0, GCornerNone);
    return;
  }
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  if (g->loading || !g->pbuf) return;
  draw_header(ctx, b, puzzle_cur());
}
static void update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  s_clear_shown = false;
  if (slide_blanking(&s_sv)) return;   // mid-slide: the body is the blank ground, the snapshot carries the content

  if (g->loading) {
    graphics_context_set_text_color(ctx, GColorBlack);
    const char *t = g->loading_msg[0] ? g->loading_msg : "Loading...";
    graphics_draw_text(ctx, t, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                       GRect(8, b.size.h / 2 - 30, b.size.w - 16, 60), GTextOverflowModeWordWrap,
                       GTextAlignmentCenter, NULL);
    return;
  }
  if (!g->pbuf) return;
  if (g->solved) { draw_solved(ctx, b); return; }   // a full-page celebration, not an overlay
  const Entry *e = puzzle_cur();
  if (s_picker) draw_picker_overlay(ctx, b);   // the picker owns the whole screen while it is open
  else render(ctx, b, e);
  draw_banner(ctx, b);
}

// ---------------------------------------------------------------------------
// Window lifecycle
// ---------------------------------------------------------------------------
static void win_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  window_set_background_color(w, GColorWhite);   // the ground a sliding body reveals
  s_full = layer_get_bounds(root);
  s_layer = layer_create(s_full);
  layer_set_update_proc(s_layer, update);
  layer_add_child(root, s_layer);
  s_hdr = layer_create(GRect(0, 0, s_full.size.w, HEADER_H));
  layer_set_update_proc(s_hdr, hdr_update);
  layer_add_child(root, s_hdr);
  slide_init(&s_sv, s_layer, SLIDE_DEFAULT_MS);   // inserts its snapshot layer above the body, below the header
  window_set_click_config_provider(w, click_config);
}
static void win_appear(Window *w) {
  if (touch_service_is_enabled()) touch_service_subscribe(touch_handler, NULL);
  if (!g->loading) clock_start();
}
static void win_disappear(Window *w) {
  touch_service_unsubscribe();
  slide_cancel(&s_sv);
  flash_cancel();
  bool was_loading = g->loading;
  g->loading = false;
  clock_stop();
  if (!was_loading && g->pbuf && !g->solved) store_save_progress();
  s_picker = false;
  s_show_cross = false;
  s_scrub_armed = false;
  s_rail_dir = 0;
  g->touching = false;
  g->banner[0] = 0;
}
static void win_unload(Window *w) {
  slide_deinit(&s_sv);
  layer_destroy(s_layer);
  s_layer = NULL;
  layer_destroy(s_hdr);
  s_hdr = NULL;
}

static void ensure_window(void) {
  if (s_win) return;
  s_win = window_create();
  window_set_window_handlers(s_win, (WindowHandlers){
    .load = win_load, .appear = win_appear, .disappear = win_disappear, .unload = win_unload });
  g->dictation = dictation_session_create(TRANSCRIPT_LEN, dictation_cb, NULL);
  if (g->dictation) dictation_session_enable_confirmation(g->dictation, false);
}

static void push(void) {
  ensure_window();
  if (!g->dictation) {   // creation can fail under memory pressure; try again
    g->dictation = dictation_session_create(TRANSCRIPT_LEN, dictation_cb, NULL);
    if (g->dictation) dictation_session_enable_confirmation(g->dictation, false);
  }
  if (!window_stack_contains_window(s_win)) window_stack_push(s_win, true);
  play_redraw();
}

void play_open_bundled(int i) {
  if (!puzzle_load_bundled(i)) { play_open_loading(); play_load_failed("Bad puzzle. Press BACK."); return; }
  int s = store_slot_find_bundled(i);
  int saved_entry = -1;
  if (s >= 0) store_load_progress(s, g->fill, g->rows * g->cols, &g->elapsed, &saved_entry);
  normalize_fill();
  s_check_pending = false;
  g->loading = false;
  if (saved_entry >= 0 && saved_entry < g->nentries) {
    g->cur_entry = saved_entry;          // resume where the solver left off
  } else {
    g->cur_entry = 0;                    // a fresh or pre-v0.37 save: open on the first gap
    if (!entry_has_empty(puzzle_cur())) go_entry(+1, true);
  }
  g->focus_cell = pencil_mode() ? first_todo(puzzle_cur()) : first_empty(puzzle_cur());
  grade_loaded();
  push();
}

void play_open_loading(void) {
  g->loading = true;
  g->loading_msg[0] = 0;
  push();
}

void play_goto_entry(int idx) {
  if (!g->pbuf || idx < 0 || idx >= g->nentries) return;
  save_if_dirty();
  flash_cancel();
  picker_close_keep();   // a pending grid check runs first; the chosen entry still wins
  if (g->solved) return;
  s_show_cross = false;
  g->cur_entry = idx;
  slide_in(slide_dx(puzzle_cur()), 0);   // enters by its orientation: Across from the left, Down from the right
  g->focus_cell = first_empty(puzzle_cur());
  play_redraw();
}

uint32_t play_elapsed(void) {
  uint32_t e = g->elapsed;
  if (g->appear_at && !g->solved) { time_t now = time(NULL); if (now > g->appear_at) e += now - g->appear_at; }
  return e;
}

void play_load_failed(const char *msg) {
  g->loading = true;
  snprintf(g->loading_msg, sizeof(g->loading_msg), "%s", msg);
  play_redraw();
}

void play_deinit(void) {
  if (g->dictation) { dictation_session_destroy(g->dictation); g->dictation = NULL; }
  if (s_more) { action_menu_hierarchy_destroy(s_more, NULL, NULL); s_more = NULL; }
  flash_cancel();
  if (s_win) { window_destroy(s_win); s_win = NULL; }
}
