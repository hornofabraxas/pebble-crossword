/*
 * slide.c: the snapshot "page turn" (see slide.h).
 *
 * A slide runs in two paints. First paint: the content layer draws the new state
 * at rest, then the snapshot layer (just above it) copies the framebuffer into an
 * offscreen bitmap and blanks the ground. From then on the content layer holds the
 * blank ground (slide_blanking() is true) and the snapshot layer blits the captured
 * bitmap at an animating offset, so each frame is a cheap blit instead of a full
 * re-render. On memory pressure the content layer itself is animated as a fallback.
 */
#include "slide.h"

static void slide_stopped(Animation *a, bool finished, void *context);

// Return to the idle resting state (content draws itself again).
static void slide_settle(SlideView *sv) {
  sv->phase = 0;
  sv->active = false;
  if (sv->snap) layer_set_frame(sv->snap, sv->full);
  if (sv->content) { layer_set_frame(sv->content, sv->full); layer_mark_dirty(sv->content); }
}

void slide_cancel(SlideView *sv) {
  if (sv->anim) {
    animation_unschedule(property_animation_get_animation(sv->anim));   // runs slide_stopped
    sv->anim = NULL;
  }
  slide_settle(sv);
}

static void slide_stopped(Animation *a, bool finished, void *context) {
  SlideView *sv = context;
  sv->anim = NULL;
  slide_settle(sv);
}

// Phase 1, inside the snapshot layer's update proc: the content has just drawn the
// new state, so copy the framebuffer into bmp, blank the ground, and animate the
// snapshot in from (dx, dy).
static void slide_capture(SlideView *sv, GContext *ctx) {
  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (fb && sv->bmp) {
    for (int y = 0; y < sv->full.size.h; y++) {
      GBitmapDataRowInfo src = gbitmap_get_data_row_info(fb, y);
      GBitmapDataRowInfo dst = gbitmap_get_data_row_info(sv->bmp, y);
      int lo = src.min_x > dst.min_x ? src.min_x : dst.min_x;
      int hi = src.max_x < dst.max_x ? src.max_x : dst.max_x;
      if (hi >= lo) memcpy(dst.data + lo, src.data + lo, hi - lo + 1);
    }
  }
  if (fb) graphics_release_frame_buffer(ctx, fb);
  graphics_context_set_fill_color(ctx, GColorWhite);   // the blank ground the slide starts from
  graphics_fill_rect(ctx, layer_get_bounds(sv->snap), 0, GCornerNone);
  sv->phase = 2;                          // from here the content stays blank; the snapshot carries it
  layer_mark_dirty(sv->content);
  GRect from = sv->full;
  from.origin.x += sv->dx;
  from.origin.y += sv->dy;
  sv->anim = property_animation_create_layer_frame(sv->snap, &from, &sv->full);
  if (!sv->anim) { slide_settle(sv); return; }   // no memory for the animation: just show the new content
  Animation *a = property_animation_get_animation(sv->anim);
  animation_set_duration(a, sv->duration);
  animation_set_curve(a, AnimationCurveEaseOut);
  animation_set_handlers(a, (AnimationHandlers){ .stopped = slide_stopped }, sv);
  layer_set_frame(sv->snap, from);
  animation_schedule(a);
}

static void slide_snap_update(Layer *layer, GContext *ctx) {
  SlideView *sv = *(SlideView **)layer_get_data(layer);
  if (sv->phase == 1) slide_capture(sv, ctx);
  else if (sv->phase == 2 && sv->bmp) graphics_draw_bitmap_in_rect(ctx, sv->bmp, layer_get_bounds(layer));
}

void slide_init(SlideView *sv, Layer *content, uint32_t duration_ms) {
  sv->content = content;
  sv->full = layer_get_frame(content);
  sv->bmp = NULL;
  sv->anim = NULL;
  sv->duration = duration_ms;
  sv->phase = 0;
  sv->dx = sv->dy = 0;
  sv->active = false;
  sv->snap = layer_create_with_data(sv->full, sizeof(SlideView *));
  if (!sv->snap) return;   // without the snapshot layer, slide_go falls back to animating content
  *(SlideView **)layer_get_data(sv->snap) = sv;
  layer_set_update_proc(sv->snap, slide_snap_update);
  layer_insert_above_sibling(sv->snap, content);
}

void slide_deinit(SlideView *sv) {
  slide_cancel(sv);
  if (sv->snap) { layer_destroy(sv->snap); sv->snap = NULL; }
  if (sv->bmp) { gbitmap_destroy(sv->bmp); sv->bmp = NULL; }
}

void slide_go(SlideView *sv, int dx, int dy) {
  slide_cancel(sv);
  if (sv->snap && !sv->bmp) sv->bmp = gbitmap_create_blank(sv->full.size, GBitmapFormat8Bit);   // one buffer, reused
  sv->active = true;
  if (sv->snap && sv->bmp) {
    // Smooth path: draw the new content once, capture it, then blit it in.
    sv->dx = dx;
    sv->dy = dy;
    sv->phase = 1;                    // capture on the next paint, after content draws the new state at rest
    layer_set_frame(sv->snap, sv->full);
    layer_mark_dirty(sv->content);    // draw the new state (the snapshot captures it)
    layer_mark_dirty(sv->snap);       // then slide_capture() runs, above the content
    return;
  }
  // Fallback: animate the content layer's own frame (re-renders per frame).
  GRect from = sv->full;
  from.origin.x += dx;
  from.origin.y += dy;
  sv->anim = property_animation_create_layer_frame(sv->content, &from, &sv->full);
  if (!sv->anim) { sv->active = false; return; }   // no animation: the state still changed, without motion
  Animation *a = property_animation_get_animation(sv->anim);
  animation_set_duration(a, sv->duration);
  animation_set_curve(a, AnimationCurveEaseOut);
  animation_set_handlers(a, (AnimationHandlers){ .stopped = slide_stopped }, sv);
  layer_set_frame(sv->content, from);
  layer_mark_dirty(sv->content);
  animation_schedule(a);
}

bool slide_active(const SlideView *sv) { return sv->active; }
bool slide_blanking(const SlideView *sv) { return sv->phase == 2; }
