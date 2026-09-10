/*
 * menu.c: the list screens.
 *
 * Home: three cards, Continue (greyed while nothing is in progress), New puzzle,
 *   Settings; hand drawn, not a MenuLayer.
 * Continue: one row per puzzle in progress, most recent first, with the time
 *   spent on it, filled / total squares and difficulty. Hold SELECT on a row for
 *   "Abandon puzzle".
 * New puzzle: the class, then the difficulty picker.
 * Difficulty picker: Peaceful / Challenging, each with a one-line description
 *   and solved / total for that shelf (a tier with no puzzles reads "Coming
 *   soon"). Selecting opens a random unplayed bundled puzzle of that class and
 *   tier, or replays a solved one and says so.
 * Settings: a row per setting with its value; selecting opens that setting's
 *   choices under a header, each with a line on what it does.
 * All clues: every entry of the current puzzle, its number in a left column
 *   (the section says Across or Down) and the clue big beside it with the letter
 *   count in parentheses (two lines when it needs them); filled entries dimmed
 *   and wrong-marked ones in red; selecting one jumps to it.
 * Every list scrolls by drag and selects by tap (see app_touch_navigation_enable
 *   in main.c). The UP / DOWN buttons wrap at either end of a list; a drag does
 *   not (the touch bridge pans a MenuLayer rather than pressing buttons).
 * Once a puzzle opens, the lists it was opened through are dropped from under
 *   the play window, so BACK from a puzzle always lands on Home.
 * Nothing on these screens is drawn smaller than Gothic 24: the rows and section
 *   headers are drawn here rather than by menu_cell_basic_draw (whose subtitle is
 *   Gothic 18 and header Gothic 14).
 */
#include "crossword.h"

#define ROW_H      58   // title + subtitle
#define HDR_H      26

static const char *TIER_LABEL[2] = { "Peaceful", "Challenging" };

// ---------------------------------------------------------------------------
// List buttons: UP at the first row wraps to the last, DOWN at the last wraps
// to the first. Only the physical buttons reach this: the touch bridge scrolls
// a MenuLayer as a pan and taps as SELECT (firmware touch_nav.c), so a drag
// past the end of a list stops there, as it should.
// ---------------------------------------------------------------------------
typedef struct {
  MenuLayer *menu;
  uint16_t (*sections)(MenuLayer *m, void *c);              // NULL = one section
  uint16_t (*rows)(MenuLayer *m, uint16_t sec, void *c);
  void (*select)(MenuLayer *m, MenuIndex *i, void *c);
  void (*select_long)(MenuLayer *m, MenuIndex *i, void *c); // optional
} ListNav;

static uint16_t nav_sections(ListNav *n) { return n->sections ? n->sections(n->menu, NULL) : 1; }
static bool nav_first(ListNav *n, MenuIndex *out) {
  uint16_t ns = nav_sections(n);
  for (uint16_t s = 0; s < ns; s++) if (n->rows(n->menu, s, NULL) > 0) { *out = (MenuIndex){ s, 0 }; return true; }
  return false;
}
static bool nav_last(ListNav *n, MenuIndex *out) {
  uint16_t ns = nav_sections(n);
  for (int s = ns - 1; s >= 0; s--) {
    uint16_t r = n->rows(n->menu, s, NULL);
    if (r > 0) { *out = (MenuIndex){ s, r - 1 }; return true; }
  }
  return false;
}
static bool nav_same(MenuIndex a, MenuIndex b) { return a.section == b.section && a.row == b.row; }

static void nav_up(ClickRecognizerRef r, void *ctx) {
  ListNav *n = ctx;
  MenuIndex cur = menu_layer_get_selected_index(n->menu), first, last;
  if (nav_first(n, &first) && nav_same(cur, first) && nav_last(n, &last))
    menu_layer_set_selected_index(n->menu, last, MenuRowAlignCenter, true);
  else
    menu_layer_set_selected_next(n->menu, true, MenuRowAlignCenter, true);
}
static void nav_down(ClickRecognizerRef r, void *ctx) {
  ListNav *n = ctx;
  MenuIndex cur = menu_layer_get_selected_index(n->menu), first, last;
  if (nav_last(n, &last) && nav_same(cur, last) && nav_first(n, &first))
    menu_layer_set_selected_index(n->menu, first, MenuRowAlignCenter, true);
  else
    menu_layer_set_selected_next(n->menu, false, MenuRowAlignCenter, true);
}
static void nav_select(ClickRecognizerRef r, void *ctx) {
  ListNav *n = ctx;
  MenuIndex cur = menu_layer_get_selected_index(n->menu);
  if (n->select) n->select(n->menu, &cur, NULL);
}
static void nav_select_long(ClickRecognizerRef r, void *ctx) {
  ListNav *n = ctx;
  MenuIndex cur = menu_layer_get_selected_index(n->menu);
  if (n->select_long) n->select_long(n->menu, &cur, NULL);
}
static void nav_click_config(void *ctx) {
  ListNav *n = ctx;
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 150, nav_up);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 150, nav_down);
  window_single_click_subscribe(BUTTON_ID_SELECT, nav_select);
  if (n->select_long) window_long_click_subscribe(BUTTON_ID_SELECT, 0, nav_select_long, NULL);
}
static void nav_attach(Window *w, ListNav *n) {
  window_set_click_config_provider_with_context(w, nav_click_config, n);
}

// A row: title in Gothic 24 Bold over a subtitle in Gothic 24, or a title alone
// in Gothic 28 Bold. The text colour is the MenuLayer's for the row's state. An
// optional short figure (a time, a count) sits right-aligned on the title line.
static void cell_draw_figure(GContext *ctx, const Layer *cell, const char *title, const char *sub, const char *figure) {
  GRect b = layer_get_bounds(cell);
  if (sub) {
    GFont f = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
    int fw = 0;
    if (figure && figure[0]) {
      fw = graphics_text_layout_get_content_size(figure, f, GRect(0, 0, 100, 28), GTextOverflowModeFill,
                                                 GTextAlignmentRight).w + 6;
      graphics_draw_text(ctx, figure, f, GRect(b.size.w - 6 - fw, 0, fw, 28), GTextOverflowModeFill,
                         GTextAlignmentRight, NULL);
    }
    graphics_draw_text(ctx, title, f, GRect(6, 0, b.size.w - 12 - fw, 28),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    graphics_draw_text(ctx, sub, fonts_get_system_font(FONT_KEY_GOTHIC_24), GRect(6, 26, b.size.w - 12, 28),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  } else {
    graphics_draw_text(ctx, title, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD), GRect(6, 4, b.size.w - 12, 32),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }
}
static void cell_draw(GContext *ctx, const Layer *cell, const char *title, const char *sub) {
  cell_draw_figure(ctx, cell, title, sub, NULL);
}
// A section header: capitals in Gothic 24 Bold on light grey.
static void hdr_draw(GContext *ctx, const Layer *cell, const char *text) {
  GRect b = layer_get_bounds(cell);
  graphics_context_set_fill_color(ctx, GColorLightGray);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, text, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), GRect(6, -3, b.size.w - 12, 28),
                     GTextOverflowModeFill, GTextAlignmentLeft, NULL);
}

// ---------------------------------------------------------------------------
// Home: three cards, hand drawn (a MenuLayer scrolls even when its rows fit):
// Continue (greyed while nothing is in progress), New puzzle, Settings. The
// selected card is solid green; UP / DOWN move the selection and wrap, SELECT
// opens. A finger on a card selects it, and a tap opens it.
// ---------------------------------------------------------------------------
static Window *s_win;
static Layer *s_home_layer;
static int s_home_sel;
static bool s_home_armed;        // a touch began on this window
static GPath *s_play_path;       // the Continue glyph

typedef struct { int slot; char line[TITLE_LEN]; char sub[32]; char time[12]; } Row;
static Row s_rows[MAX_SLOTS];
static int s_nrows;

static void build_rows(void) {
  s_nrows = 0;
  for (int s = 0; s < MAX_SLOTS; s++) {
    if (!g->slots[s].used) continue;
    Row *r = &s_rows[s_nrows++];
    r->slot = s;
    store_slot_title(s, r->line, sizeof(r->line));
    char fill[MAX_CELLS];
    uint32_t el;
    int n = store_slot_fill(s, fill, sizeof(fill), &el);
    int filled = 0, total = g->slots[s].total;
    for (int k = 0; k < n; k++) if (fill[k]) filled++;
    // The title already names the puzzle; the time spent sits beside it, and the
    // subtitle adds progress and difficulty.
    if (el) snprintf(r->time, sizeof(r->time), "%lu:%02lu", (unsigned long)(el / 60), (unsigned long)(el % 60));
    else r->time[0] = 0;
    int t = g->slots[s].tier;
    const char *tier = TIER_LABEL[t > 1 ? 1 : t];
    if (total > 0) snprintf(r->sub, sizeof(r->sub), "%d/%d \xC2\xB7 %s", filled, total, tier);
    else snprintf(r->sub, sizeof(r->sub), "%d filled \xC2\xB7 %s", filled, tier);
  }
}

static bool has_ip(void) { return s_nrows > 0; }
// Home cards, always three: Continue, New puzzle, Settings. Continue is shown
// greyed and inert when nothing is in progress rather than hidden.
enum { HOME_CONTINUE, HOME_NEW, HOME_SETTINGS };
static int home_n(void) { return 3; }
static int home_kind(int i) { return i; }
static bool home_enabled(int kind) { return kind != HOME_CONTINUE || has_ip(); }
// The next selectable card in a direction, skipping any that are disabled.
static int home_step(int from, int dir) {
  int i = from;
  for (int s = 0; s < home_n(); s++) {
    i = (i + dir + home_n()) % home_n();
    if (home_enabled(i)) return i;
  }
  return from;
}

#define CARD_PAD  10         // screen edge to the cards
#define CARD_GAP  10         // between cards
#define CARD_GLYPH 46        // the glyph column at the left of a card
static GRect card_rect(GRect b, int i) {
  int n = home_n();
  int h = (b.size.h - 2 * CARD_PAD - (n - 1) * CARD_GAP) / n;
  return GRect(CARD_PAD, CARD_PAD + i * (h + CARD_GAP), b.size.w - 2 * CARD_PAD, h);
}
static int card_at(GRect b, int y) {
  for (int i = 0; i < home_n(); i++) {
    GRect r = card_rect(b, i);
    if (y >= r.origin.y - CARD_GAP / 2 && y < r.origin.y + r.size.h + CARD_GAP / 2) return i;
  }
  return -1;
}

static const GPathInfo PLAY_PATH = { 3, (GPoint[]){ {0, 0}, {0, 22}, {19, 11} } };

// The glyphs: a play triangle, a plus, three sliders. Drawn in the card's ink.
static void draw_glyph(GContext *ctx, int kind, int cx, int cy, GColor ink) {
  graphics_context_set_fill_color(ctx, ink);
  graphics_context_set_stroke_color(ctx, ink);
  switch (kind) {
    case HOME_CONTINUE:
      if (s_play_path) { gpath_move_to(s_play_path, GPoint(cx - 8, cy - 11)); gpath_draw_filled(ctx, s_play_path); }
      break;
    case HOME_NEW:
      graphics_fill_rect(ctx, GRect(cx - 3, cy - 12, 6, 24), 0, GCornerNone);
      graphics_fill_rect(ctx, GRect(cx - 12, cy - 3, 24, 6), 0, GCornerNone);
      break;
    default:
      graphics_context_set_stroke_width(ctx, 3);
      for (int i = -1; i <= 1; i++) {
        int y = cy + i * 9;
        graphics_draw_line(ctx, GPoint(cx - 12, y), GPoint(cx + 12, y));
      }
      graphics_fill_circle(ctx, GPoint(cx - 5, cy - 9), 4);
      graphics_fill_circle(ctx, GPoint(cx + 6, cy), 4);
      graphics_fill_circle(ctx, GPoint(cx - 2, cy + 9), 4);
      graphics_context_set_stroke_width(ctx, 1);
      break;
  }
}
static const char *const HOME_LABEL[3] = { "Continue", "New puzzle", "Settings" };

static void home_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  for (int i = 0; i < home_n(); i++) {
    GRect r = card_rect(b, i);
    int kind = home_kind(i);
    bool en = home_enabled(kind);
    bool sel = en && i == s_home_sel;   // a disabled card never shows selected
    if (sel) {
      graphics_context_set_fill_color(ctx, GColorJaegerGreen);
      graphics_fill_rect(ctx, r, 10, GCornersAll);
    } else {
      graphics_context_set_stroke_color(ctx, GColorLightGray);
      graphics_context_set_stroke_width(ctx, 2);
      graphics_draw_round_rect(ctx, GRect(r.origin.x + 1, r.origin.y + 1, r.size.w - 2, r.size.h - 2), 10);
      graphics_context_set_stroke_width(ctx, 1);
    }
    GColor ink = !en ? GColorLightGray : (sel ? GColorWhite : GColorBlack);
    int cy = r.origin.y + r.size.h / 2;
    draw_glyph(ctx, kind, r.origin.x + CARD_GLYPH / 2 + 2, cy, ink);
    graphics_context_set_text_color(ctx, ink);
    graphics_draw_text(ctx, HOME_LABEL[kind], fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
                       GRect(r.origin.x + CARD_GLYPH, cy - 19, r.size.w - CARD_GLYPH - 4, 34),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }
}

static void push_settings(void);
static void push_continue(void);
static void push_new(void);

static void home_open(void) {
  if (!home_enabled(home_kind(s_home_sel))) return;   // the greyed Continue card is inert
  switch (home_kind(s_home_sel)) {
    case HOME_CONTINUE: push_continue(); break;
    case HOME_NEW:      push_new(); break;
    default:            push_settings(); break;
  }
}
static void home_select(int i) {
  if (i < 0 || i >= home_n() || i == s_home_sel || !home_enabled(home_kind(i))) return;
  s_home_sel = i;
  if (s_home_layer) layer_mark_dirty(s_home_layer);
}
static void home_up(ClickRecognizerRef r, void *ctx)     { home_select(home_step(s_home_sel, -1)); }
static void home_down(ClickRecognizerRef r, void *ctx)   { home_select(home_step(s_home_sel, +1)); }
static void home_click(ClickRecognizerRef r, void *ctx)  { home_open(); }
static void home_click_config(void *ctx) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 150, home_up);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 150, home_down);
  window_single_click_subscribe(BUTTON_ID_SELECT, home_click);
}
// A finger selects the card under it; lifting without moving opens it. The
// touch that closed the previous window may still be lifting off when this
// handler is installed, so only a touch that began here counts.
static int s_home_td_x, s_home_td_y;
static void home_touch(const TouchEvent *ev, void *ctx) {
  if (ev->non_navigational || !s_home_layer) return;
  GRect b = layer_get_bounds(s_home_layer);
  if (ev->type == TouchEvent_Touchdown) {
    s_home_armed = true;
    s_home_td_x = ev->x; s_home_td_y = ev->y;
    home_select(card_at(b, ev->y));
    return;
  }
  if (!s_home_armed) return;
  if (ev->type == TouchEvent_PositionUpdate) { home_select(card_at(b, ev->y)); return; }
  if (ev->type != TouchEvent_Liftoff) return;
  s_home_armed = false;
  int dx = ev->x - s_home_td_x, dy = ev->y - s_home_td_y;
  if (dx < 0) dx = -dx;
  if (dy < 0) dy = -dy;
  if (dx < 12 && dy < 12 && card_at(b, ev->y) == s_home_sel) home_open();
}

static void home_appear(Window *w) {
  build_rows();
  s_home_sel = has_ip() ? HOME_CONTINUE : HOME_NEW;   // never rest on the greyed Continue
  s_home_armed = false;
  if (touch_service_is_enabled()) touch_service_subscribe(home_touch, NULL);
  if (s_home_layer) layer_mark_dirty(s_home_layer);
}
static void home_disappear(Window *w) { touch_service_unsubscribe(); }
static void home_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_home_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_home_layer, home_update);
  layer_add_child(root, s_home_layer);
  s_play_path = gpath_create(&PLAY_PATH);
  window_set_click_config_provider(w, home_click_config);
}
static void home_unload(Window *w) {
  layer_destroy(s_home_layer); s_home_layer = NULL;
  if (s_play_path) { gpath_destroy(s_play_path); s_play_path = NULL; }
}

void menu_push_home(void) {
  s_win = window_create();
  window_set_window_handlers(s_win, (WindowHandlers){
    .load = home_load, .appear = home_appear, .disappear = home_disappear, .unload = home_unload });
  window_stack_push(s_win, true);
}

// ---------------------------------------------------------------------------
// Continue: one row per puzzle in progress, most recent first. Hold SELECT on a
// row for "Abandon puzzle". Opening a puzzle drops this list from under the
// play window, so BACK from the puzzle lands on Home, as it does for a new one.
// ---------------------------------------------------------------------------
static Window *s_cont_win;
static MenuLayer *s_cont_menu;
static ListNav s_cont_nav;

static uint16_t cont_rows(MenuLayer *m, uint16_t sec, void *c) { return s_nrows; }
static int16_t cont_row_h(MenuLayer *m, MenuIndex *i, void *c) { return ROW_H; }
static int16_t cont_hdr_h(MenuLayer *m, uint16_t sec, void *c) { return HDR_H; }
static void cont_hdr(GContext *ctx, const Layer *cell, uint16_t sec, void *c) { hdr_draw(ctx, cell, "CONTINUE"); }
static void cont_draw(GContext *ctx, const Layer *cell, MenuIndex *i, void *c) {
  if (i->row >= s_nrows) return;
  Row *r = &s_rows[i->row];
  cell_draw_figure(ctx, cell, r->line, r->sub, r->time);
}
static void cont_select(MenuLayer *m, MenuIndex *i, void *c) {
  if (i->row >= s_nrows) return;
  Slot *sl = &g->slots[s_rows[i->row].slot];
  play_open_bundled(sl->bidx);
  if (s_cont_win) window_stack_remove(s_cont_win, false);
}

// Hold SELECT on a row: an ActionMenu with "Abandon puzzle". The slot is dropped
// once the menu has closed (a clean window stack, as in play.c); the list closes
// with its last row.
static ActionMenuLevel *s_abandon_level;
static int s_abandon_slot = -1;      // the row held
static int s_abandon_pick = -1;      // set by the action, applied in did_close
static void am_abandon_row(ActionMenu *m, const ActionMenuItem *i, void *c) { s_abandon_pick = s_abandon_slot; }
static void abandon_closed(ActionMenu *m, const ActionMenuItem *performed, void *ctx) {
  int s = s_abandon_pick;
  s_abandon_pick = -1;
  s_abandon_slot = -1;
  if (s < 0) return;
  store_drop_slot(s);
  build_rows();
  if (!s_cont_win) return;
  if (s_nrows == 0) { window_stack_pop(true); return; }
  menu_layer_reload_data(s_cont_menu);
  menu_layer_set_selected_index(s_cont_menu, (MenuIndex){ 0, 0 }, MenuRowAlignCenter, false);
}
static void cont_select_long(MenuLayer *m, MenuIndex *i, void *c) {
  if (i->row >= s_nrows) return;
  s_abandon_slot = s_rows[i->row].slot;
  if (s_abandon_level) { action_menu_hierarchy_destroy(s_abandon_level, NULL, NULL); s_abandon_level = NULL; }
  s_abandon_level = action_menu_level_create(1);
  action_menu_level_add_action(s_abandon_level, "Abandon puzzle", am_abandon_row, NULL);
  ActionMenuConfig cfg = {
    .root_level = s_abandon_level,
    .colors = { .background = GColorJaegerGreen, .foreground = GColorWhite },
    .align = ActionMenuAlignCenter,
    .did_close = abandon_closed,
  };
  action_menu_open(&cfg);
}
static void cont_appear(Window *w) {
  build_rows();
  if (s_cont_menu) {
    menu_layer_reload_data(s_cont_menu);
    menu_layer_set_selected_index(s_cont_menu, (MenuIndex){ 0, 0 }, MenuRowAlignCenter, false);
  }
}
static void cont_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_cont_menu = menu_layer_create(layer_get_bounds(root));
  menu_layer_set_callbacks(s_cont_menu, NULL, (MenuLayerCallbacks){
    .get_num_rows = cont_rows, .draw_row = cont_draw, .get_cell_height = cont_row_h,
    .get_header_height = cont_hdr_h, .draw_header = cont_hdr,
    .select_click = cont_select });
  menu_layer_set_normal_colors(s_cont_menu, GColorWhite, GColorBlack);
  menu_layer_set_highlight_colors(s_cont_menu, GColorJaegerGreen, GColorWhite);
  s_cont_nav = (ListNav){ .menu = s_cont_menu, .sections = NULL, .rows = cont_rows,
                          .select = cont_select, .select_long = cont_select_long };
  nav_attach(w, &s_cont_nav);
  layer_add_child(root, menu_layer_get_layer(s_cont_menu));
}
static void cont_unload(Window *w) {
  menu_layer_destroy(s_cont_menu); s_cont_menu = NULL;
  window_destroy(s_cont_win); s_cont_win = NULL;
}
static void push_continue(void) {
  if (s_cont_win || s_nrows == 0) return;
  s_cont_win = window_create();
  window_set_window_handlers(s_cont_win, (WindowHandlers){
    .load = cont_load, .appear = cont_appear, .unload = cont_unload });
  window_stack_push(s_cont_win, true);
}

// ---------------------------------------------------------------------------
// New puzzle: the class, then the difficulty picker. Like the picker, this list
// is dropped from under the play window once a puzzle opens.
// ---------------------------------------------------------------------------
static Window *s_new_win;
static MenuLayer *s_new_menu;
static ListNav s_new_nav;
static void push_picker(int cls);

static uint16_t new_rows(MenuLayer *m, uint16_t sec, void *c) { return 2; }
static int16_t new_row_h(MenuLayer *m, MenuIndex *i, void *c) { return ROW_H; }
static int16_t new_hdr_h(MenuLayer *m, uint16_t sec, void *c) { return HDR_H; }
static void new_hdr(GContext *ctx, const Layer *cell, uint16_t sec, void *c) { hdr_draw(ctx, cell, "NEW PUZZLE"); }
static void new_draw(GContext *ctx, const Layer *cell, MenuIndex *i, void *c) {
  if (i->row == 0) cell_draw(ctx, cell, "Mini (7x7)", "A few minutes");
  else cell_draw(ctx, cell, "Regular (15x15)", "A bigger sit-down");
}
static void new_select(MenuLayer *m, MenuIndex *i, void *c) {
  push_picker(i->row == 0 ? CLS_MINI : CLS_REGULAR);
}
static void new_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_new_menu = menu_layer_create(layer_get_bounds(root));
  menu_layer_set_callbacks(s_new_menu, NULL, (MenuLayerCallbacks){
    .get_num_rows = new_rows, .draw_row = new_draw, .get_cell_height = new_row_h,
    .get_header_height = new_hdr_h, .draw_header = new_hdr,
    .select_click = new_select });
  menu_layer_set_normal_colors(s_new_menu, GColorWhite, GColorBlack);
  menu_layer_set_highlight_colors(s_new_menu, GColorJaegerGreen, GColorWhite);
  s_new_nav = (ListNav){ .menu = s_new_menu, .sections = NULL, .rows = new_rows, .select = new_select };
  nav_attach(w, &s_new_nav);
  layer_add_child(root, menu_layer_get_layer(s_new_menu));
}
static void new_unload(Window *w) {
  menu_layer_destroy(s_new_menu); s_new_menu = NULL;
  window_destroy(s_new_win); s_new_win = NULL;
}
static void push_new(void) {
  if (s_new_win) return;
  s_new_win = window_create();
  window_set_window_handlers(s_new_win, (WindowHandlers){ .load = new_load, .unload = new_unload });
  window_stack_push(s_new_win, true);
}

// ---------------------------------------------------------------------------
// Settings: one row per setting with its current value. Selecting a row opens
// that setting's choices under a header naming it, each choice with a line on
// what it does; the choice in force is selected when the list opens, and
// picking one saves it and returns here. Speak / Type also opens the explainer.
// ---------------------------------------------------------------------------
static Window *s_set_win;
static MenuLayer *s_set_menu;
static ListNav s_set_nav;

typedef struct { const char *title; const char *what; } Choice;
#define MAX_CHOICES 3
typedef struct {
  const char *name;        // the Settings row
  const char *header;      // the options list's header (short enough for one line)
  int n;
  Choice choice[MAX_CHOICES];
  uint8_t value[MAX_CHOICES];   // the setting's byte for each choice (see setting_field)
} Setting;
#define N_SETTINGS 6
static const Setting SETTINGS[N_SETTINGS] = {
  { "Check answers", "CHECK ANSWERS", 2,
    { { "After every word", "Right words lock, wrong ones turn red" },
      { "When the grid is full", "No hints until every square is filled" } },
    { CHECK_EVERY_WORD, CHECK_AT_END } },
  { "Vibration", "VIBRATION", 2,
    { { "On", "A buzz for a right word, a wrong one, and solving" },
      { "Off", "Never vibrate" } },
    { HAPTICS_ON, HAPTICS_OFF } },
  { "Letter scroll", "LETTER SCROLL", 2,
    { { "Adaptive", "Fast drag skips ahead, slow drag steps" },
      { "Fixed", "Every drag moves letters at one pace" } },
    { SCROLL_ADAPTIVE, SCROLL_FIXED } },
  { "Scroll speed", "SCROLL SPEED", 3,
    { { "Default", "The pace as tuned" },
      { "Slow", "More finger travel per letter" },
      { "Fast", "Less finger travel per letter" } },
    { SPEED_DEFAULT, SPEED_SLOW, SPEED_FAST } },
  { "Typing buzz", "TYPING BUZZ", 2,
    { { "On", "A short buzz when you keep a typed letter" },
      { "Off", "No buzz as each letter is kept" } },
    { TYPEBUZZ_ON, TYPEBUZZ_OFF } },
  { "Skip filled squares", "SKIP FILLED", 2,
    { { "Off", "The next square, filled or not" },
      { "On", "Jump past filled squares to the next empty" } },
    { SKIP_OFF, SKIP_ON } },
};
#define OPT_ROW_H 82      // title + two lines of what it does

static uint8_t *setting_field(int k) {
  switch (k) {
    case 0:  return &g->set.check;
    case 1:  return &g->set.haptics;
    case 2:  return &g->set.scroll;
    case 3:  return &g->set.speed;
    case 4:  return &g->set.buzz_type;
    default: return &g->set.skip;
  }
}
// Which choice a setting holds; an unknown byte reads as the first.
static int setting_choice(int k) {
  uint8_t v = *setting_field(k);
  for (int i = 1; i < SETTINGS[k].n; i++) if (SETTINGS[k].value[i] == v) return i;
  return 0;
}

// One row past the settings is a "Controls" action that reopens the gestures
// screens.
static uint16_t set_rows(MenuLayer *m, uint16_t sec, void *c) { return N_SETTINGS + 1; }
static int16_t set_row_h(MenuLayer *m, MenuIndex *i, void *c) { return ROW_H; }
static void set_draw(GContext *ctx, const Layer *cell, MenuIndex *i, void *c) {
  if (i->row == N_SETTINGS) { cell_draw(ctx, cell, "Controls", "Explains input methods"); return; }
  if (i->row > N_SETTINGS) return;
  const Setting *st = &SETTINGS[i->row];
  cell_draw(ctx, cell, st->name, st->choice[setting_choice(i->row)].title);
}

// The choices of one setting.
static Window *s_opt_win;
static MenuLayer *s_opt_menu;
static ListNav s_opt_nav;
static int s_opt_k;

static uint16_t opt_rows(MenuLayer *m, uint16_t sec, void *c) { return SETTINGS[s_opt_k].n; }
static int16_t opt_row_h(MenuLayer *m, MenuIndex *i, void *c) { return OPT_ROW_H; }
static int16_t opt_hdr_h(MenuLayer *m, uint16_t sec, void *c) { return HDR_H; }
static void opt_hdr(GContext *ctx, const Layer *cell, uint16_t sec, void *c) {
  hdr_draw(ctx, cell, SETTINGS[s_opt_k].header);
}
static void opt_draw(GContext *ctx, const Layer *cell, MenuIndex *i, void *c) {
  if (i->row >= SETTINGS[s_opt_k].n) return;
  const Choice *ch = &SETTINGS[s_opt_k].choice[i->row];
  GRect b = layer_get_bounds(cell);
  graphics_draw_text(ctx, ch->title, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), GRect(6, 0, b.size.w - 12, 28),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  graphics_draw_text(ctx, ch->what, fonts_get_system_font(FONT_KEY_GOTHIC_24), GRect(6, 26, b.size.w - 12, 54),
                     GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
}
static void opt_select(MenuLayer *m, MenuIndex *i, void *c) {
  if (i->row >= SETTINGS[s_opt_k].n) return;
  int k = s_opt_k;
  *setting_field(k) = SETTINGS[k].value[i->row];
  store_save_settings();
  if (s_set_menu) menu_layer_reload_data(s_set_menu);
  window_stack_pop(true);
}
static void opt_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_opt_menu = menu_layer_create(layer_get_bounds(root));
  menu_layer_set_callbacks(s_opt_menu, NULL, (MenuLayerCallbacks){
    .get_num_rows = opt_rows, .draw_row = opt_draw, .get_cell_height = opt_row_h,
    .get_header_height = opt_hdr_h, .draw_header = opt_hdr,
    .select_click = opt_select });
  menu_layer_set_normal_colors(s_opt_menu, GColorWhite, GColorBlack);
  menu_layer_set_highlight_colors(s_opt_menu, GColorJaegerGreen, GColorWhite);
  s_opt_nav = (ListNav){ .menu = s_opt_menu, .sections = NULL, .rows = opt_rows, .select = opt_select };
  nav_attach(w, &s_opt_nav);
  layer_add_child(root, menu_layer_get_layer(s_opt_menu));
  menu_layer_set_selected_index(s_opt_menu, (MenuIndex){ 0, setting_choice(s_opt_k) }, MenuRowAlignCenter, false);
}
static void opt_unload(Window *w) {
  menu_layer_destroy(s_opt_menu); s_opt_menu = NULL;
  window_destroy(s_opt_win); s_opt_win = NULL;
}
static void push_options(int k) {
  if (s_opt_win) return;
  s_opt_k = k;
  s_opt_win = window_create();
  window_set_window_handlers(s_opt_win, (WindowHandlers){ .load = opt_load, .unload = opt_unload });
  window_stack_push(s_opt_win, true);
}

static void set_select(MenuLayer *m, MenuIndex *i, void *c) {
  if (i->row == N_SETTINGS) explain_push(false);   // review from Settings: paging wraps, any tap closes
  else if (i->row < N_SETTINGS) push_options(i->row);
}
static void set_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_set_menu = menu_layer_create(layer_get_bounds(root));
  menu_layer_set_callbacks(s_set_menu, NULL, (MenuLayerCallbacks){
    .get_num_rows = set_rows, .draw_row = set_draw, .get_cell_height = set_row_h,
    .select_click = set_select });
  menu_layer_set_normal_colors(s_set_menu, GColorWhite, GColorBlack);
  menu_layer_set_highlight_colors(s_set_menu, GColorJaegerGreen, GColorWhite);
  s_set_nav = (ListNav){ .menu = s_set_menu, .sections = NULL, .rows = set_rows, .select = set_select };
  nav_attach(w, &s_set_nav);
  layer_add_child(root, menu_layer_get_layer(s_set_menu));
}
static void set_unload(Window *w) {
  menu_layer_destroy(s_set_menu); s_set_menu = NULL;
  window_destroy(s_set_win); s_set_win = NULL;
}
static void push_settings(void) {
  if (s_set_win) return;
  s_set_win = window_create();
  window_set_window_handlers(s_set_win, (WindowHandlers){ .load = set_load, .unload = set_unload });
  window_stack_push(s_set_win, true);
}

// ---------------------------------------------------------------------------
// Difficulty picker (for one class)
// ---------------------------------------------------------------------------
static Window *s_pick_win;
static MenuLayer *s_pick_menu;
static ListNav s_pick_nav;
static int s_pick_cls;
static int s_totals[2][2];       // bundled puzzles that exist per class x tier ...
static int s_solved[2][2];       // ... and how many of them the player has solved
static const char *TIER_DESC[2] = { "Gentle, straight clues", "Tricky, misdirecting clues" };

static uint16_t pick_rows(MenuLayer *m, uint16_t sec, void *c) { return 2; }
static int16_t pick_row_h(MenuLayer *m, MenuIndex *i, void *c) { return ROW_H; }
static int16_t pick_hdr_h(MenuLayer *m, uint16_t sec, void *c) { return HDR_H; }
static void pick_hdr(GContext *ctx, const Layer *cell, uint16_t sec, void *c) {
  hdr_draw(ctx, cell, "CHOOSE DIFFICULTY");
}
static void pick_draw(GContext *ctx, const Layer *cell, MenuIndex *i, void *c) {
  // A tier with no bundled puzzles at all reads "Coming soon"; otherwise its
  // one-line description. (Solving them all does not empty the pool: the pick
  // falls back to replaying a solved puzzle.)
  int total = s_totals[s_pick_cls][i->row];
  char count[24] = "";
  if (total > 0) snprintf(count, sizeof(count), "%d/%d", s_solved[s_pick_cls][i->row], total);
  cell_draw_figure(ctx, cell, TIER_LABEL[i->row], total == 0 ? "Coming soon" : TIER_DESC[i->row], total > 0 ? count : NULL);
}
static void pick_select(MenuLayer *m, MenuIndex *i, void *c) {
  g->set.tier = i->row;
  store_save_settings();
  bool replay;
  int b = puzzle_pick_bundled(s_pick_cls, i->row, &replay);
  if (b >= 0) {
    play_open_bundled(b);
    if (replay) play_banner("All done. Replaying", GColorLightGray, GColorBlack);
  } else { play_open_loading(); play_load_failed("No puzzle yet. Press BACK."); }
  // The play window (or the loading screen) is now on top; drop the picker and
  // the New puzzle list beneath it so BACK from the puzzle lands on Home.
  if (s_pick_win) window_stack_remove(s_pick_win, false);
  if (s_new_win) window_stack_remove(s_new_win, false);
}
static void pick_appear(Window *w) {
  for (int c = 0; c < 2; c++)
    for (int t = 0; t < 2; t++) { s_totals[c][t] = puzzle_pool_total(c, t); s_solved[c][t] = puzzle_pool_solved(c, t); }
  if (s_pick_menu) {
    menu_layer_reload_data(s_pick_menu);
    int sel = g->set.tier > 1 ? 1 : g->set.tier;
    menu_layer_set_selected_index(s_pick_menu, (MenuIndex){ 0, sel }, MenuRowAlignCenter, false);
  }
}
static void pick_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_pick_menu = menu_layer_create(layer_get_bounds(root));
  menu_layer_set_callbacks(s_pick_menu, NULL, (MenuLayerCallbacks){
    .get_num_rows = pick_rows, .draw_row = pick_draw, .get_cell_height = pick_row_h,
    .get_header_height = pick_hdr_h, .draw_header = pick_hdr,
    .select_click = pick_select });
  menu_layer_set_normal_colors(s_pick_menu, GColorWhite, GColorBlack);
  menu_layer_set_highlight_colors(s_pick_menu, GColorJaegerGreen, GColorWhite);
  s_pick_nav = (ListNav){ .menu = s_pick_menu, .sections = NULL, .rows = pick_rows, .select = pick_select };
  nav_attach(w, &s_pick_nav);
  layer_add_child(root, menu_layer_get_layer(s_pick_menu));
}
static void pick_unload(Window *w) {
  menu_layer_destroy(s_pick_menu); s_pick_menu = NULL;
  window_destroy(s_pick_win); s_pick_win = NULL;
}
static void push_picker(int cls) {
  if (s_pick_win) return;
  s_pick_cls = cls;
  s_pick_win = window_create();
  window_set_window_handlers(s_pick_win, (WindowHandlers){
    .load = pick_load, .appear = pick_appear, .unload = pick_unload });
  window_stack_push(s_pick_win, true);
}

// ---------------------------------------------------------------------------
// All clues
// ---------------------------------------------------------------------------
static Window *s_clue_win;
static MenuLayer *s_clue_menu;
static ListNav s_clue_nav;
static int s_n_across;
static uint8_t s_clue_h[MAX_ENTRIES];   // row heights, measured once at load

#define CL_COL      46      // left column: entry number
#define CL_TEXT_Y   2       // clue and number rect top (both Gothic 24 Bold, so they align)
#define CL_HDR_H    26
#define CL_ROW_1    36      // one clue line
#define CL_ROW_2    62      // two clue lines
#define CL_SIDE     6

static bool entry_filled(const Entry *e) {
  for (int k = 0; k < e->len; k++) {
    int r, c; puzzle_entry_cell(e, k, &r, &c);
    if (!g->fill[puzzle_cell(r, c)]) return false;
  }
  return true;
}
static bool entry_wrong(const Entry *e) {
  for (int k = 0; k < e->len; k++) {
    int r, c; puzzle_entry_cell(e, k, &r, &c);
    int cell = puzzle_cell(r, c);
    if ((g->wrong[cell >> 3] >> (cell & 7)) & 1) return true;
  }
  return false;
}
static int clue_entry_index(MenuIndex *i) { return i->section == 0 ? i->row : s_n_across + i->row; }

static uint16_t clue_sections(MenuLayer *m, void *c) { return 2; }
static uint16_t clue_rows(MenuLayer *m, uint16_t sec, void *c) {
  return sec == 0 ? s_n_across : g->nentries - s_n_across;
}
static int16_t clue_hdr_h(MenuLayer *m, uint16_t sec, void *c) { return CL_HDR_H; }
static void clue_hdr(GContext *ctx, const Layer *cell, uint16_t sec, void *c) {
  GRect b = layer_get_bounds(cell);
  hdr_draw(ctx, cell, sec == 0 ? "ACROSS" : "DOWN");
  char t[16];
  uint32_t el = play_elapsed();
  snprintf(t, sizeof(t), "%lu:%02lu", (unsigned long)(el / 60), (unsigned long)(el % 60));
  graphics_draw_text(ctx, t, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), GRect(b.size.w - 86, -3, 80, 28),
                     GTextOverflowModeFill, GTextAlignmentRight, NULL);
}
static int16_t clue_row_h(MenuLayer *m, MenuIndex *i, void *c) { return s_clue_h[clue_entry_index(i)]; }

// The clue with its letter count, crossword style: "Frozen water. (3)".
static void clue_text(const Entry *e, char *buf, int cap) {
  char clue[200];
  puzzle_clue(e, clue, sizeof(clue));
  snprintf(buf, cap, "%s (%d)", clue, e->len);
}

// Measure every clue once: one or two lines of Gothic 24 Bold beside the column.
static void clue_measure(int w) {
  GFont f = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  GRect box = GRect(0, 0, w - CL_COL - CL_SIDE, 100);
  char clue[208];
  for (int k = 0; k < g->nentries; k++) {
    clue_text(&g->entries[k], clue, sizeof(clue));
    GSize sz = graphics_text_layout_get_content_size(clue, f, box, GTextOverflowModeWordWrap, GTextAlignmentLeft);
    s_clue_h[k] = sz.h > 30 ? CL_ROW_2 : CL_ROW_1;
  }
}

static void clue_draw(GContext *ctx, const Layer *cell, MenuIndex *i, void *c) {
  const Entry *e = &g->entries[clue_entry_index(i)];
  GRect b = layer_get_bounds(cell);
  MenuIndex si = menu_layer_get_selected_index(s_clue_menu);
  bool sel = si.section == i->section && si.row == i->row;
  bool filled = entry_filled(e), wrong = entry_wrong(e);
  GColor fg = sel ? GColorWhite : (wrong ? GColorRed : filled ? GColorDarkGray : GColorBlack);
  char id[8], clue[208];
  snprintf(id, sizeof(id), "%d", e->number);   // the section header carries Across / Down
  graphics_context_set_text_color(ctx, fg);
  graphics_draw_text(ctx, id, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(CL_SIDE, CL_TEXT_Y, CL_COL - CL_SIDE, 28), GTextOverflowModeFill,
                     GTextAlignmentLeft, NULL);
  clue_text(e, clue, sizeof(clue));
  graphics_draw_text(ctx, clue, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(CL_COL, CL_TEXT_Y, b.size.w - CL_COL - CL_SIDE, b.size.h - 4),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}
static void clue_select(MenuLayer *m, MenuIndex *i, void *c) {
  play_goto_entry(clue_entry_index(i));
  window_stack_pop(true);
}
static void clue_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_n_across = 0;
  for (int k = 0; k < g->nentries; k++) if (g->entries[k].dir == 0) s_n_across++;
  clue_measure(layer_get_bounds(root).size.w);
  s_clue_menu = menu_layer_create(layer_get_bounds(root));
  menu_layer_set_callbacks(s_clue_menu, NULL, (MenuLayerCallbacks){
    .get_num_sections = clue_sections, .get_num_rows = clue_rows, .draw_row = clue_draw,
    .get_cell_height = clue_row_h, .get_header_height = clue_hdr_h, .draw_header = clue_hdr,
    .select_click = clue_select });
  menu_layer_set_normal_colors(s_clue_menu, GColorWhite, GColorBlack);
  menu_layer_set_highlight_colors(s_clue_menu, GColorJaegerGreen, GColorWhite);
  s_clue_nav = (ListNav){ .menu = s_clue_menu, .sections = clue_sections, .rows = clue_rows, .select = clue_select };
  nav_attach(w, &s_clue_nav);
  layer_add_child(root, menu_layer_get_layer(s_clue_menu));
  int cur = g->cur_entry;
  MenuIndex mi = cur < s_n_across ? (MenuIndex){ 0, cur } : (MenuIndex){ 1, cur - s_n_across };
  menu_layer_set_selected_index(s_clue_menu, mi, MenuRowAlignCenter, false);
}
static void clue_unload(Window *w) {
  menu_layer_destroy(s_clue_menu); s_clue_menu = NULL;
  window_destroy(s_clue_win); s_clue_win = NULL;
}
void menu_push_clues(void) {
  if (!g->pbuf || s_clue_win) return;
  s_clue_win = window_create();
  window_set_window_handlers(s_clue_win, (WindowHandlers){ .load = clue_load, .unload = clue_unload });
  window_stack_push(s_clue_win, true);
}

void menu_deinit(void) {
  if (s_abandon_level) { action_menu_hierarchy_destroy(s_abandon_level, NULL, NULL); s_abandon_level = NULL; }
  if (s_win) { window_destroy(s_win); s_win = NULL; }
}
