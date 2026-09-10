/*
 * explain.c: the "Controls" screens. Three cards you page through, using the same
 * page-turn slide as the play view (slide.c): swipe gestures, the physical
 * buttons, and how to reach other clues. Dots at the bottom mark which card you
 * are on.
 *
 * Advancing is the through-line: a forward swipe, a tap, SELECT or DOWN steps to
 * the next card, and stepping off the last card closes the set. A back swipe or UP
 * steps to the previous card (the first card is the backstop). BACK is the only
 * button that exits outright. There is no wrap, so paging forward always shows
 * every card. The two entry points differ only in bookkeeping: onboarding (first
 * install, pushed over the home menu) records the cards as seen on exit so it does
 * not reappear; review (from the Settings row) does not.
 */
#include "crossword.h"
#include "slide.h"

static Window *s_win;
static Layer *s_layer;
static SlideView s_sv;   // the card page-turn slide
static bool s_onboard;   // first-install walkthrough (vs review from Settings)
static bool s_armed;     // a touch began on this window (the opening tap's liftoff must not close it)
static int s_card;       // 0 swipe, 1 buttons, 2 other clues
static int s_tx, s_ty;   // touch-down point, for telling a swipe from a tap
#define N_CARDS 3

// A filled triangle pointing up / down / left / right, in the current fill
// colour, with independent half-width and half-height so it can be squat (the
// native action-bar arrows are 12x7, wider than they are tall).
static void tri2(GContext *ctx, int x, int y, char dir, int hw, int hh) {
  GPoint pts[3];
  switch (dir) {
    case 'u': pts[0] = GPoint(x, y - hh); pts[1] = GPoint(x - hw, y + hh); pts[2] = GPoint(x + hw, y + hh); break;
    case 'd': pts[0] = GPoint(x, y + hh); pts[1] = GPoint(x - hw, y - hh); pts[2] = GPoint(x + hw, y - hh); break;
    case 'l': pts[0] = GPoint(x - hw, y); pts[1] = GPoint(x + hw, y - hh); pts[2] = GPoint(x + hw, y + hh); break;
    default:  pts[0] = GPoint(x + hw, y); pts[1] = GPoint(x - hw, y - hh); pts[2] = GPoint(x - hw, y + hh); break;
  }
  GPathInfo info = { 3, pts };
  GPath *p = gpath_create(&info);
  if (p) { gpath_draw_filled(ctx, p); gpath_destroy(p); }
}

// A symmetric triangle, for the swipe glyphs.
static void tri(GContext *ctx, int x, int y, char dir, int s) { tri2(ctx, x, y, dir, s, s); }

static void title(GContext *ctx, GRect b, const char *s) {
  graphics_context_set_fill_color(ctx, GColorJaegerGreen);
  graphics_fill_rect(ctx, GRect(0, 0, b.size.w, 32), 0, GCornerNone);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, s, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD), GRect(0, -2, b.size.w, 32),
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

// Card 1: the two swipe axes, each a glyph pair over its label.
static void card_swipe(GContext *ctx, GRect b) {
  title(ctx, b, "SWIPE CONTROLS");
  int cx = b.size.w / 2;
  GFont f = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  graphics_context_set_fill_color(ctx, GColorJaegerGreen);
  tri(ctx, cx, 52, 'u', 9);
  tri(ctx, cx, 78, 'd', 9);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, "CHANGE CLUE", f, GRect(0, 90, b.size.w, 28), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  graphics_context_set_fill_color(ctx, GColorJaegerGreen);
  tri(ctx, cx - 16, 150, 'l', 9);
  tri(ctx, cx + 16, 150, 'r', 9);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, "TYPE LETTERS", f, GRect(0, 162, b.size.w, 28), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

// A checkmark, scaled to fit `box`. The SELECT glyph.
static void draw_check(GContext *ctx, GRect box, GColor color) {
  int s = box.size.w < box.size.h ? box.size.w : box.size.h;
  int cx = box.origin.x + box.size.w / 2, cy = box.origin.y + box.size.h / 2;
  int t = s * 18 / 100; if (t < 4) t = 4;
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, t);
  graphics_draw_line(ctx, GPoint(cx - s * 34 / 100, cy + s * 2 / 100),
                          GPoint(cx - s * 8 / 100,  cy + s * 26 / 100));
  graphics_draw_line(ctx, GPoint(cx - s * 8 / 100,  cy + s * 26 / 100),
                          GPoint(cx + s * 36 / 100, cy - s * 30 / 100));
  graphics_context_set_stroke_width(ctx, 1);
}

// A left-pointing arrow (shaft + two barbs), scaled to fit `box`. The BACK glyph.
static void draw_back_arrow(GContext *ctx, GRect box, GColor color) {
  int s = box.size.w < box.size.h ? box.size.w : box.size.h;
  int cx = box.origin.x + box.size.w / 2, cy = box.origin.y + box.size.h / 2;
  int t = s * 15 / 100; if (t < 3) t = 3;
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, t);
  int hw = s * 34 / 100; GPoint tip = GPoint(cx - hw, cy);
  int a = s * 26 / 100;
  graphics_draw_line(ctx, tip, GPoint(cx + hw, cy));          // shaft
  graphics_draw_line(ctx, tip, GPoint(tip.x + a, cy - a));    // upper barb
  graphics_draw_line(ctx, tip, GPoint(tip.x + a, cy + a));    // lower barb
  graphics_context_set_stroke_width(ctx, 1);
}

// A native-style button chip: a tall JaegerGreen plate flush against the screen
// edge nearest its physical button, rounded only on the two inner corners (so it
// reads as sitting under the button), carrying a white glyph. `left` puts it on
// the left edge (BACK); otherwise the right edge (UP / SELECT / DOWN). Glyph:
// 'u'/'d' the squat native arrows, 'c' the SELECT check, 'b' the BACK arrow.
#define CHIP_W 30
#define CHIP_H 48
static void chip(GContext *ctx, GRect b, int cy, bool left, char glyph) {
  int x = left ? 0 : b.size.w - CHIP_W;
  graphics_context_set_fill_color(ctx, GColorJaegerGreen);
  graphics_fill_rect(ctx, GRect(x, cy - CHIP_H / 2, CHIP_W, CHIP_H), 11,
                     left ? GCornersRight : GCornersLeft);
  int gx = x + CHIP_W / 2;
  graphics_context_set_fill_color(ctx, GColorWhite);
  if (glyph == 'c')      draw_check(ctx, GRect(gx - 13, cy - 13, 26, 26), GColorWhite);
  else if (glyph == 'b') draw_back_arrow(ctx, GRect(gx - 13, cy - 13, 26, 26), GColorWhite);
  else                   tri2(ctx, gx, cy, glyph, 9, 6);   // UP / DOWN
}

// Card 2: a "BUTTON CONTROLS" title like the other cards, then the chips flush to
// the screen edges near their physical buttons. The title takes the top 32, so
// the cluster sits just below it (centres 62 / 118 / 174, evenly 56 apart) rather
// than at the bare-screen button heights, leaving a clear gap under the green
// bar. UP / SELECT / DOWN ride the right edge with what they do beside them; BACK
// rides the left edge level with UP, where the physical back button is. Every
// label is Gothic 24 bold, right-aligned and vertically centred on its button.
static void card_buttons(GContext *ctx, GRect b) {
  title(ctx, b, "BUTTON CONTROLS");
  int tw = b.size.w - CHIP_W - 12;   // label width: x=6 to 6px before the right chip
  GFont f = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  graphics_context_set_text_color(ctx, GColorBlack);
  chip(ctx, b, 62, false, 'u');
  chip(ctx, b, 62, true, 'b');    // BACK: left edge, level with UP, where the physical button is
  graphics_draw_text(ctx, "Prev clue", f, GRect(6, 62 - 16, tw, 28), GTextOverflowModeFill, GTextAlignmentRight, NULL);
  chip(ctx, b, 118, false, 'c');   // SELECT = say the answer; hold for the menu
  graphics_draw_text(ctx, "Say answer", f, GRect(6, 118 - 27, tw, 28), GTextOverflowModeFill, GTextAlignmentRight, NULL);
  graphics_draw_text(ctx, "Hold: menu", f, GRect(6, 118 - 1, tw, 28), GTextOverflowModeFill, GTextAlignmentRight, NULL);
  chip(ctx, b, 174, false, 'd');
  graphics_draw_text(ctx, "Next clue", f, GRect(6, 174 - 16, tw, 28), GTextOverflowModeFill, GTextAlignmentRight, NULL);
}

// Card 3: how to reach the crossing word and the full clue list.
static void card_clues(GContext *ctx, GRect b) {
  title(ctx, b, "OTHER CLUES");
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, "TAP A LETTER\nBOX TO VIEW THE\nCROSSING WORD", fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(0, 44, b.size.w, 90), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  graphics_context_set_stroke_color(ctx, GColorLightGray);
  graphics_draw_line(ctx, GPoint(20, 140), GPoint(b.size.w - 20, 140));
  graphics_context_set_text_color(ctx, GColorJaegerGreen);
  graphics_draw_text(ctx, "ALL CLUES IN MENU", fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(0, 148, b.size.w, 30), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

static void dots(GContext *ctx, GRect b) {
  int gap = 16, y = b.size.h - 12, x0 = b.size.w / 2 - (N_CARDS - 1) * gap / 2;
  for (int i = 0; i < N_CARDS; i++) {
    graphics_context_set_fill_color(ctx, i == s_card ? GColorJaegerGreen : GColorLightGray);
    graphics_fill_circle(ctx, GPoint(x0 + i * gap, y), 3);
  }
}

static void update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  if (slide_blanking(&s_sv)) return;   // mid-slide: the snapshot layer carries the card
  if (s_card == 0) card_swipe(ctx, b);
  else if (s_card == 1) card_buttons(ctx, b);
  else card_clues(ctx, b);
  dots(ctx, b);
}

// Close the cards, returning to whatever pushed them: the main menu for the
// first-install onboarding (main.c pushes it over the home window), or the
// Settings list for a review from the Controls row. Onboarding also records the
// cards as seen, so leaving by any route (paging off the end, or BACK) means it
// will not reappear.
static void finish(void) {
  if (s_onboard) store_onboard_mark();
  window_stack_pop(true);
}
// Page by `d` (+1 next, -1 previous), sliding the card in. There is no wrap:
// advancing off the last card closes the set, and the first card is the backstop
// going the other way. Same in both modes, so every card gets seen unless the
// player exits with BACK.
static void go(int d) {
  if (slide_active(&s_sv)) return;                          // one page turn at a time
  if (d > 0 && s_card == N_CARDS - 1) { finish(); return; } // off the end: done
  if (d < 0 && s_card == 0) return;                         // nowhere before the first card
  s_card += d;
  int w = layer_get_bounds(s_layer).size.w;
  slide_go(&s_sv, d > 0 ? w : -w, 0);                       // next in from the right, previous from the left
}
static void advance_click(ClickRecognizerRef r, void *ctx) { go(+1); }   // SELECT: advance (off the end closes)
static void back_click(ClickRecognizerRef r, void *ctx) { finish(); }    // BACK: the only button that exits
static void up_click(ClickRecognizerRef r, void *ctx) { go(-1); }
static void down_click(ClickRecognizerRef r, void *ctx) { go(+1); }
static void click_config(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_UP, up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, advance_click);
  window_single_click_subscribe(BUTTON_ID_BACK, back_click);
}
// The tap that opened this (a Settings row, a menu item) may still be lifting
// off when the handler is installed, so only a touch that began here acts. A
// swipe pages either way; a plain tap advances (and off the last card, closes).
static void touch(const TouchEvent *ev, void *ctx) {
  if (ev->type == TouchEvent_Touchdown) { s_armed = true; s_tx = ev->x; s_ty = ev->y; return; }
  if (ev->type != TouchEvent_Liftoff || !s_armed) return;
  s_armed = false;
  int dx = ev->x - s_tx, dy = ev->y - s_ty;
  int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
  if (adx > 24 && adx > ady) { go(dx < 0 ? +1 : -1); return; }   // swipe left = next, right = previous
  if (ady > 24 && ady > adx) { go(dy < 0 ? +1 : -1); return; }   // swipe up = next, down = previous
  go(+1);                                                        // a plain tap advances
}
static void load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_layer, update);
  layer_add_child(root, s_layer);
  slide_init(&s_sv, s_layer, SLIDE_DEFAULT_MS);
  window_set_click_config_provider(w, click_config);
}
static void appear(Window *w) {
  s_armed = false;
  if (touch_service_is_enabled()) touch_service_subscribe(touch, NULL);
  light_enable_interaction();
}
static void disappear(Window *w) { touch_service_unsubscribe(); }
static void unload(Window *w) {
  slide_deinit(&s_sv);
  layer_destroy(s_layer); s_layer = NULL;
  window_destroy(s_win); s_win = NULL;
}

void explain_push(bool onboarding) {
  s_card = 0;
  s_onboard = onboarding;
  if (s_win) { layer_mark_dirty(s_layer); return; }
  s_win = window_create();
  if (!s_win) return;   // memory pressure: only the controls screen is skipped
  window_set_window_handlers(s_win, (WindowHandlers){
    .load = load, .appear = appear, .disappear = disappear, .unload = unload });
  window_stack_push(s_win, true);
}
